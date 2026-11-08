//! `tagcore._native`: the Python binding of tagcore's SWD capture and
//! recovery module (`tagcore::recovery`).
//!
//! The Python package `tagcore` (in `python/tagcore`) re-exports this as
//! `tagcore.swd` and `tagcore.recovery`; see the README for the API. The
//! shape follows the Python interface design: values are returned rather
//! than written through output parameters, failures raise
//! [`RecoveryError`], data are `bytes`, sessions and loader sessions are
//! context managers, and every long operation releases the GIL and takes an
//! optional progress callback.
//!
//! A `PySession` owns the base for its lifetime, as a recovery session
//! does in Rust: it attaches with the core halted at its reset vector, so no
//! firmware runs, and leaves as its exit says (a hardware reset by default).
//! A `PyExternalFlash` borrows its session for each call, so the session
//! cannot be used for anything else while a loader command runs.

use std::path::{Path, PathBuf};

use pyo3::create_exception;
use pyo3::exceptions::{PyException, PyValueError};
use pyo3::prelude::*;
use pyo3::types::{PyBytes, PyDict, PyList};

use tagcore::UsbDev;
use tagcore::recovery::rescue::read_uid;
use tagcore::recovery::{
    BuildFiles, CaptureCheck, CaptureOptions, ExternalFlash, IdentityRecord, ImageCheck,
    RescueProgress, Session, SwdExit, TargetImage, capture_tag, find_loader, rescue_erase,
};

create_exception!(
    _native,
    RecoveryError,
    PyException,
    "A recovery operation failed: no base, an attach that did not halt the \
     core, a loader that failed, or a refused rescue. The message says which."
);

/// Converts a recovery error message into a [`RecoveryError`].
fn err(msg: impl Into<String>) -> PyErr {
    RecoveryError::new_err(msg.into())
}

/// The error for a session another Python thread is using: the GIL is
/// released during long operations, so a second thread can reach a session
/// that is busy, and a plain `borrow_mut` would panic there.
fn busy(_: pyo3::pycell::PyBorrowMutError) -> PyErr {
    err("the session is in use by another thread")
}

/// Parses an exit name: `"reset"` (a hardware reset, so the tag boots as
/// after a plain connection) or `"halted"` (the core stays halted).
fn parse_exit(exit: &str) -> PyResult<SwdExit> {
    match exit {
        "reset" => Ok(SwdExit::HardwareReset),
        "halted" => Ok(SwdExit::LeaveHalted),
        other => Err(PyValueError::new_err(format!(
            "exit must be \"reset\" or \"halted\", not {other:?}"
        ))),
    }
}

/// The bases on USB, as `(serial, "bus:address")` pairs.
fn list_bases() -> Vec<UsbDev> {
    Session::new().available()
}

/// Chooses a base: the only one when `base` is `None`, otherwise the one
/// whose serial or `bus:address` equals `base`.
fn select_base(base: Option<&str>) -> PyResult<UsbDev> {
    let devs = list_bases();
    match base {
        None => match devs.len() {
            0 => Err(err("no base found on USB")),
            1 => Ok(devs[0].clone()),
            n => Err(err(format!(
                "{n} bases found; name one with base=<serial or bus:address>"
            ))),
        },
        Some(want) => devs
            .into_iter()
            .find(|d| d.serial == want || format!("{}:{}", d.bus, d.address) == want)
            .ok_or_else(|| err(format!("no base {want} on USB"))),
    }
}

/// Loader directories searched by name: the caller's, `$TAG_LOADER_DIR`,
/// then `build-host/embedded/loaders`, as `tag-capture` searches them.
fn loader_dirs(extra: Option<Vec<PathBuf>>) -> Vec<PathBuf> {
    let mut dirs = extra.unwrap_or_default();
    if let Some(env) = std::env::var_os("TAG_LOADER_DIR") {
        dirs.push(env.into());
    }
    dirs.push("build-host/embedded/loaders".into());
    dirs
}

/// Resolves a loader given as a path to an image, or as a name
/// (`AT25XE_PresTagv3`, `AT25XE_PresTagv3-RW`) found under the loader
/// directories, and reads it.
fn load_loader(loader: &str, dirs: Option<Vec<PathBuf>>) -> PyResult<TargetImage> {
    let path = Path::new(loader);
    let path = if path.exists() {
        path.to_path_buf()
    } else {
        find_loader(loader, &loader_dirs(dirs)).ok_or_else(|| {
            err(format!(
                "no loader {loader}: not a file, and no {loader}.stldr found"
            ))
        })?
    };
    TargetImage::load(&path).map_err(err)
}

/// A dictionary of the identity record's fields that a script needs.
fn identity_dict<'py>(py: Python<'py>, r: &IdentityRecord) -> PyResult<Bound<'py, PyDict>> {
    let d = PyDict::new(py);
    for (k, v) in &r.strings {
        d.set_item(k, v)?;
    }
    if let Some(x) = r.external_flash {
        let e = PyDict::new(py);
        e.set_item("jedec_id", x.jedec_id)?;
        e.set_item("size", x.flash_size)?;
        e.set_item("program_page", x.program_page)?;
        e.set_item("erase_unit", x.erase_unit)?;
        e.set_item("spare", x.spare)?;
        d.set_item("external_flash", e)?;
    }
    Ok(d)
}

/// Calls a Python progress callback from a detached thread, keeping the
/// first exception it raises for the caller to re-raise afterwards.
struct Progress {
    /// The callback, if one was given.
    callback: Option<Py<PyAny>>,
    /// The first exception the callback raised.
    error: Option<PyErr>,
}

impl Progress {
    /// Wraps an optional callback.
    fn new(callback: Option<Py<PyAny>>) -> Self {
        Progress {
            callback,
            error: None,
        }
    }

    /// Calls the callback with `args`, unless it has already failed.
    fn call<A>(&mut self, args: A)
    where
        A: for<'py> pyo3::call::PyCallArgs<'py>,
    {
        if self.error.is_some() {
            return;
        }
        if let Some(cb) = &self.callback {
            Python::attach(|py| {
                if let Err(e) = cb.call1(py, args) {
                    self.error = Some(e);
                }
            });
        }
    }

    /// The callback's exception, if it raised one.
    fn finish(self) -> PyResult<()> {
        self.error.map_or(Ok(()), Err)
    }
}

/// An SWD recovery session on one base: the tag halted at its reset vector,
/// with no firmware run.
///
/// Made by `tagcore.swd.open()`. Use it in a `with` block; leaving the block
/// closes it with the exit given to `open()` (`"reset"` by default).
#[pyclass(name = "Session", module = "tagcore._native")]
struct PySession {
    /// The Rust session.
    inner: Session,
    /// How [`close`](Self::close) leaves the tag by default.
    exit: SwdExit,
}

impl PySession {
    /// The session's MCU table, or an error if it has closed.
    fn mcu(&self) -> PyResult<&'static tagcore::recovery::McuMap> {
        self.inner.mcu().ok_or_else(|| err("the session is closed"))
    }

    /// Reads `length` bytes at `addr`, any alignment, with the GIL released.
    fn read_bytes(&mut self, py: Python<'_>, addr: u32, length: usize) -> PyResult<Vec<u8>> {
        let start = addr & !3;
        let end = (u64::from(addr) + length as u64 + 3) & !3;
        let s = &mut self.inner;
        let words = py
            .detach(|| s.read_vec(start, (end - u64::from(start)) as usize, 4096))
            .map_err(err)?;
        let off = (addr - start) as usize;
        Ok(words[off..off + length].to_vec())
    }
}

#[pymethods]
impl PySession {
    /// The MCU's name, e.g. `"STM32L432"`.
    #[getter]
    fn mcu_name(&self) -> PyResult<&'static str> {
        Ok(self.mcu()?.name)
    }

    /// The chip's 96-bit unique ID, in the monitor's `info.uuid` form.
    #[getter]
    fn uid(&mut self) -> PyResult<String> {
        read_uid(&mut self.inner).map_err(err)
    }

    /// What the attach observed: the core halted at the reset vector, the
    /// registers as found, and the target voltage.
    #[getter]
    fn attach<'py>(&self, py: Python<'py>) -> PyResult<Bound<'py, PyDict>> {
        let a = self.inner.attach_info();
        let d = PyDict::new(py);
        d.set_item("idcode", a.idcode)?;
        d.set_item("voltage", a.voltage)?;
        d.set_item("dhcsr_before", a.dhcsr_before)?;
        d.set_item("demcr_before", a.demcr_before)?;
        d.set_item("pc", a.pc)?;
        d.set_item("reset_vector", a.reset_vector)?;
        d.set_item("halted", a.halted)?;
        d.set_item("at_reset_vector", a.at_reset_vector)?;
        d.set_item("flash_blank", a.flash_blank)?;
        Ok(d)
    }

    /// Reads `length` bytes of target memory at `addr` (any alignment).
    fn read<'py>(
        &mut self,
        py: Python<'py>,
        addr: u32,
        length: usize,
    ) -> PyResult<Bound<'py, PyBytes>> {
        let data = self.read_bytes(py, addr, length)?;
        Ok(PyBytes::new(py, &data))
    }

    /// Reads one 32-bit word at a word-aligned `addr`.
    fn read_word(&mut self, addr: u32) -> PyResult<u32> {
        self.inner
            .read_word(addr)
            .ok_or_else(|| err(format!("SWD read of 0x{addr:08X} failed")))
    }

    /// Writes `data` at `addr`; both must be word aligned (a multiple of 4).
    fn write(&mut self, py: Python<'_>, addr: u32, data: &[u8]) -> PyResult<()> {
        if !addr.is_multiple_of(4) || !data.len().is_multiple_of(4) {
            return Err(PyValueError::new_err(
                "addr and len(data) must be multiples of 4",
            ));
        }
        let s = &mut self.inner;
        let data = data.to_vec();
        if py.detach(|| s.write(addr, &data)) {
            Ok(())
        } else {
            Err(err(format!("SWD write at 0x{addr:08X} failed")))
        }
    }

    /// The named regions `read_region()` accepts, as `(name, addr, size)`.
    fn regions(&mut self) -> PyResult<Vec<(String, u32, u32)>> {
        let m = self.mcu()?;
        let flash_kb = self
            .inner
            .read_word(m.flashsize_reg)
            .map_or(0, |v| v & 0xFFFF);
        let mut out = vec![
            ("internal_flash".to_string(), m.flash_base, flash_kb * 1024),
            ("backup_regs".to_string(), m.backup_regs, m.backup_count * 4),
        ];
        for r in m.sram.iter().chain(m.register_blocks).chain(m.info_regions) {
            out.push((r.name.to_string(), r.addr, r.size));
        }
        Ok(out)
    }

    /// Reads a named region whole: `internal_flash`, `backup_regs`, an SRAM
    /// (`sram1`, `sram2`) or a register block; see `regions()`.
    fn read_region<'py>(&mut self, py: Python<'py>, name: &str) -> PyResult<Bound<'py, PyBytes>> {
        let (addr, size) = self
            .regions()?
            .into_iter()
            .find(|(n, _, _)| n == name)
            .map(|(_, a, s)| (a, s))
            .ok_or_else(|| PyValueError::new_err(format!("no region {name}; see regions()")))?;
        self.read(py, addr, size as usize)
    }

    /// Halts the core.
    fn halt(&mut self) -> PyResult<()> {
        if self.inner.halt() {
            Ok(())
        } else {
            Err(err("halt failed"))
        }
    }

    /// Lets the core run.
    fn run(&mut self) -> PyResult<()> {
        if self.inner.run() {
            Ok(())
        } else {
            Err(err("run failed"))
        }
    }

    /// Starts a loader session on this tag's external flash.
    ///
    /// `loader` is a path to a loader image, or a loader name found under
    /// `loader_dirs`, `$TAG_LOADER_DIR` and `build-host/embedded/loaders`.
    /// Use the result in a `with` block; leaving it sends `Serve()` EXIT.
    #[pyo3(signature = (loader, loader_dirs=None))]
    fn external_flash(
        slf: Bound<'_, Self>,
        loader: &str,
        loader_dirs: Option<Vec<PathBuf>>,
    ) -> PyResult<PyExternalFlash> {
        let image = load_loader(loader, loader_dirs)?;
        let py = slf.py();
        let xf = {
            let mut me = slf.try_borrow_mut().map_err(busy)?;
            let s = &mut me.inner;
            py.detach(|| ExternalFlash::open(s, &image))
                .map_err(|e| err(format!("loader failed: {e}")))?
        };
        Ok(PyExternalFlash {
            session: slf.unbind(),
            xf: Some(xf),
            path: image.path().to_path_buf(),
        })
    }

    /// Ends the session: `"reset"` (a hardware reset; the tag boots as
    /// after a plain connection) or `"halted"`. Without an argument, the
    /// exit given to `open()`. Closing twice does nothing.
    #[pyo3(signature = (exit=None))]
    fn close(&mut self, exit: Option<&str>) -> PyResult<()> {
        let exit = match exit {
            Some(e) => parse_exit(e)?,
            None => self.exit,
        };
        if self.inner.is_open() {
            self.inner.close(exit);
        }
        Ok(())
    }

    /// Context-manager entry: the session itself.
    fn __enter__(slf: Py<Self>) -> Py<Self> {
        slf
    }

    /// Context-manager exit: closes with the default exit. Never swallows
    /// an exception.
    #[pyo3(signature = (*_args))]
    fn __exit__(&mut self, _args: &Bound<'_, pyo3::types::PyTuple>) -> PyResult<bool> {
        self.close(None)?;
        Ok(false)
    }
}

/// A loader's `Serve()` session on the tag's external flash.
///
/// Made by `Session.external_flash()`. Each call borrows the session, so
/// the session cannot be used for anything else while a command runs.
#[pyclass(name = "ExternalFlash", module = "tagcore._native")]
struct PyExternalFlash {
    /// The session the loader runs in.
    session: Py<PySession>,
    /// The Rust loader session; `None` once closed.
    xf: Option<ExternalFlash>,
    /// The loader image's path.
    path: PathBuf,
}

impl PyExternalFlash {
    /// The open loader session, or an error once closed.
    fn xf(&self) -> PyResult<&ExternalFlash> {
        self.xf
            .as_ref()
            .ok_or_else(|| err("the loader session is closed"))
    }

    /// Runs `f` on the loader and session with the GIL released.
    fn with<T: Send>(
        &mut self,
        py: Python<'_>,
        f: impl FnOnce(&mut ExternalFlash, &mut Session) -> Result<T, String> + Send,
    ) -> PyResult<T> {
        let xf = self
            .xf
            .as_mut()
            .ok_or_else(|| err("the loader session is closed"))?;
        let mut sess = self.session.bind(py).try_borrow_mut().map_err(busy)?;
        let s = &mut sess.inner;
        py.detach(|| f(xf, s)).map_err(err)
    }
}

#[pymethods]
impl PyExternalFlash {
    /// The loader image's path.
    #[getter]
    fn loader(&self) -> String {
        self.path.display().to_string()
    }

    /// The part's JEDEC ID (manufacturer, type, capacity) as the loader read it.
    #[getter]
    fn jedec(&self) -> PyResult<u32> {
        Ok(self.xf()?.jedec())
    }

    /// Status register 1 as the loader found it; the protect bits are evidence.
    #[getter]
    fn sr1(&self) -> PyResult<u32> {
        Ok(self.xf()?.sr1())
    }

    /// Part size in bytes.
    #[getter]
    fn size(&self) -> PyResult<u32> {
        Ok(self.xf()?.size())
    }

    /// Erase unit in bytes.
    #[getter]
    fn sector_size(&self) -> PyResult<u32> {
        Ok(self.xf()?.sector_size())
    }

    /// True for a loader built with erase and program (`-RW`).
    #[getter]
    fn writable(&self) -> PyResult<bool> {
        Ok(self.xf()?.writable())
    }

    /// Bytes per page, data and spare, for a paged part (SPI NAND); 0 for NOR.
    #[getter]
    fn page_bytes(&self) -> PyResult<u32> {
        Ok(self.xf()?.page_bytes())
    }

    /// Reads `length` bytes at `offset`. `progress(done, total)` is called
    /// after each buffer.
    #[pyo3(signature = (offset, length, progress=None))]
    fn read<'py>(
        &mut self,
        py: Python<'py>,
        offset: u32,
        length: usize,
        progress: Option<Py<PyAny>>,
    ) -> PyResult<Bound<'py, PyBytes>> {
        let mut p = Progress::new(progress);
        let mut buf = vec![0u8; length];
        let out = &mut buf;
        let pp = &mut p;
        self.with(py, move |xf, s| {
            let mut cb = |done: u32, total: u32| pp.call((done, total));
            xf.read(s, offset, out, Some(&mut cb))
        })?;
        p.finish()?;
        Ok(PyBytes::new(py, &buf))
    }

    /// Reads one page of a paged part, data and spare: `raw` turns the
    /// part's ECC off. Returns `(data, status, status2)`.
    #[pyo3(signature = (page, raw=false))]
    fn read_page<'py>(
        &mut self,
        py: Python<'py>,
        page: u32,
        raw: bool,
    ) -> PyResult<(Bound<'py, PyBytes>, u8, u8)> {
        let n = self.xf()?.page_bytes() as usize;
        let mut buf = vec![0u8; n];
        let out = &mut buf;
        let (st, st2) = self.with(py, move |xf, s| xf.read_page(s, page, raw, out))?;
        Ok((PyBytes::new(py, &buf), st, st2))
    }

    /// Erases the sector holding `offset`; the loader proves it blank.
    /// Raises on a read-only loader.
    fn erase_sector(&mut self, py: Python<'_>, offset: u32) -> PyResult<()> {
        self.with(py, move |xf, s| xf.erase_sector(s, offset))
    }

    /// Programs `data` at `offset` (onto erased flash); the loader verifies
    /// it by read-back. Raises on a read-only loader.
    fn program(&mut self, py: Python<'_>, offset: u32, data: &[u8]) -> PyResult<()> {
        let data = data.to_vec();
        self.with(py, move |xf, s| xf.program(s, offset, &data))
    }

    /// Sends `Serve()` EXIT. Closing twice does nothing.
    fn close(&mut self, py: Python<'_>) -> PyResult<()> {
        if self.xf.is_none() {
            return Ok(());
        }
        // Borrow before taking the loader, so a busy session leaves it open.
        let mut sess = self.session.bind(py).try_borrow_mut().map_err(busy)?;
        let Some(mut xf) = self.xf.take() else {
            return Ok(());
        };
        let s = &mut sess.inner;
        py.detach(|| xf.close(s))
            .map_err(|e| err(format!("Serve() did not exit cleanly: {e}")))
    }

    /// Context-manager entry: the loader session itself.
    fn __enter__(slf: Py<Self>) -> Py<Self> {
        slf
    }

    /// Context-manager exit: closes. Never swallows an exception.
    #[pyo3(signature = (*_args))]
    fn __exit__(
        &mut self,
        py: Python<'_>,
        _args: &Bound<'_, pyo3::types::PyTuple>,
    ) -> PyResult<bool> {
        self.close(py)?;
        Ok(false)
    }
}

/// The bases on USB, as a list of `{"serial", "bus", "address"}`.
#[pyfunction]
fn bases(py: Python<'_>) -> PyResult<Bound<'_, PyList>> {
    let l = PyList::empty(py);
    for d in list_bases() {
        let e = PyDict::new(py);
        e.set_item("serial", &d.serial)?;
        e.set_item("bus", d.bus)?;
        e.set_item("address", d.address)?;
        l.append(e)?;
    }
    Ok(l)
}

/// Opens a recovery session: attaches under reset with the core halted at
/// its reset vector, so no firmware runs.
///
/// `base` is a serial or `bus:address`; without it, the only base on USB.
/// `exit` is how the session ends by default: `"reset"` or `"halted"`.
#[pyfunction]
#[pyo3(signature = (base=None, exit="reset"))]
fn open(py: Python<'_>, base: Option<&str>, exit: &str) -> PyResult<PySession> {
    let exit = parse_exit(exit)?;
    let dev = select_base(base)?;
    let mut s = Session::new();
    py.detach(|| s.open(&dev))
        .map_err(|e| err(format!("could not halt the tag at its reset vector: {e}")))?;
    Ok(PySession { inner: s, exit })
}

/// Takes a capture, as `tag-capture` does, in its own session; returns a
/// dictionary describing it (`dir`, `manifest`, `mcu`, `uid`, `complete`,
/// `identity`, `external_note`, `regions`, `build`).
#[pyfunction]
#[pyo3(signature = (
    out_dir=None, reason="", sram=false, external=true, loader=None,
    loader_dirs=None, external_full=false, with_build=None, extra=None,
    exit="reset", base=None, progress=None
))]
#[allow(clippy::too_many_arguments)]
fn capture<'py>(
    py: Python<'py>,
    out_dir: Option<PathBuf>,
    reason: &str,
    sram: bool,
    external: bool,
    loader: Option<PathBuf>,
    loader_dirs: Option<Vec<PathBuf>>,
    external_full: bool,
    with_build: Option<PathBuf>,
    extra: Option<Vec<PathBuf>>,
    exit: &str,
    base: Option<&str>,
    progress: Option<Py<PyAny>>,
) -> PyResult<Bound<'py, PyDict>> {
    let exit = parse_exit(exit)?;
    let build = match with_build {
        None => None,
        Some(b) => Some(BuildFiles::resolve(&b, &extra.unwrap_or_default()).map_err(err)?),
    };
    let dev = select_base(base)?;
    let mut p = Progress::new(progress);
    let pp = &mut p;
    let dirs = loader_dirs_for_capture(loader_dirs);
    let result = py.detach(move || {
        let mut line = |l: &str| pp.call((l.to_string(),));
        let mut o = CaptureOptions {
            parent_dir: out_dir.unwrap_or_else(|| PathBuf::from("captures")),
            reason: reason.to_string(),
            include_sram: sram,
            include_external: external,
            loader_path: loader,
            loader_dirs: dirs,
            external_full,
            exit,
            build,
            progress: Some(&mut line),
        };
        capture_tag(&mut o, &dev)
    });
    p.finish()?;
    let r = result.map_err(|(e, _)| err(e))?;
    let d = PyDict::new(py);
    d.set_item("dir", r.dir.display().to_string())?;
    d.set_item("manifest", r.manifest.display().to_string())?;
    d.set_item("mcu", &r.mcu)?;
    d.set_item("uid", &r.uid)?;
    d.set_item("complete", r.complete)?;
    match &r.identity {
        Ok(rec) => d.set_item("identity", identity_dict(py, rec)?)?,
        Err(why) => {
            d.set_item("identity", py.None())?;
            d.set_item("identity_error", why)?;
        }
    }
    d.set_item("external_note", &r.external_note)?;
    let regions = PyList::empty(py);
    for g in &r.regions {
        let e = PyDict::new(py);
        e.set_item("name", &g.name)?;
        e.set_item("file", &g.file)?;
        e.set_item("addr", g.addr)?;
        e.set_item("size", g.size)?;
        e.set_item("ok", g.ok)?;
        regions.append(e)?;
    }
    d.set_item("regions", regions)?;
    match &r.build {
        None => d.set_item("build", py.None())?,
        Some(b) => {
            let e = PyDict::new(py);
            e.set_item("elf", &b.elf)?;
            e.set_item(
                "image_check",
                match b.image_check {
                    ImageCheck::Matches { .. } => "matches",
                    ImageCheck::Differs { .. } => "differs",
                    _ => "not checked",
                },
            )?;
            d.set_item("build", e)?;
        }
    }
    Ok(d)
}

/// The capture's loader search path: as [`loader_dirs`].
fn loader_dirs_for_capture(extra: Option<Vec<PathBuf>>) -> Vec<PathBuf> {
    loader_dirs(extra)
}

/// Rescue step 1, as `tag-xflash rescue-erase` does, in its own session:
/// erases every sector of the tag's NOR external flash that holds data and
/// reads each back blank, through a writable (`-RW`) loader.
///
/// Exactly one of `capture` (a `tag-capture` directory of this tag, matched
/// by chip UID) and `no_capture=True` must be given. `progress(done, total,
/// erased)` is called after every sector. Returns a dictionary of what was
/// done. Raises [`RecoveryError`] on any refusal or failure.
#[pyfunction]
#[pyo3(signature = (loader, capture=None, no_capture=false, loader_dirs=None, base=None, progress=None))]
fn rescue_erase_external<'py>(
    py: Python<'py>,
    loader: &str,
    capture: Option<PathBuf>,
    no_capture: bool,
    loader_dirs: Option<Vec<PathBuf>>,
    base: Option<&str>,
    progress: Option<Py<PyAny>>,
) -> PyResult<Bound<'py, PyDict>> {
    let check_dir = match (&capture, no_capture) {
        (Some(_), true) => {
            return Err(PyValueError::new_err(
                "give capture= or no_capture=True, not both",
            ));
        }
        (None, false) => {
            return Err(PyValueError::new_err(
                "give capture=<tag-capture directory of this tag> or no_capture=True",
            ));
        }
        (c, _) => c.clone(),
    };
    let image = load_loader(loader, loader_dirs)?;
    let dev = select_base(base)?;
    let mut p = Progress::new(progress);
    let pp = &mut p;
    let result = py.detach(move || {
        let mut s = Session::new();
        if let Err(e) = s.open(&dev) {
            return Err(format!("could not halt the tag at its reset vector: {e}"));
        }
        let check = match &check_dir {
            Some(d) => CaptureCheck::Capture(d),
            None => CaptureCheck::NoCapture,
        };
        let mut cb = |r: &RescueProgress| pp.call((r.done, r.total, r.erased));
        let r = rescue_erase(&mut s, &image, check, Some(&mut cb));
        s.close(SwdExit::HardwareReset);
        r
    });
    p.finish()?;
    let r = result.map_err(err)?;
    let d = PyDict::new(py);
    d.set_item("uid", &r.uid)?;
    d.set_item("jedec", r.jedec)?;
    d.set_item("sr1", r.sr1)?;
    d.set_item("size", r.size)?;
    d.set_item("sector_size", r.sector_size)?;
    d.set_item("sectors_scanned", r.sectors_scanned)?;
    d.set_item("sectors_erased", r.sectors_erased)?;
    d.set_item("notes", r.notes.clone())?;
    d.set_item("seconds", r.seconds)?;
    Ok(d)
}

/// The native module: sessions, loader sessions, captures and the rescue
/// erase, and the [`RecoveryError`] they raise.
#[pymodule]
fn _native(m: &Bound<'_, PyModule>) -> PyResult<()> {
    m.add("RecoveryError", m.py().get_type::<RecoveryError>())?;
    m.add_class::<PySession>()?;
    m.add_class::<PyExternalFlash>()?;
    m.add_function(wrap_pyfunction!(bases, m)?)?;
    m.add_function(wrap_pyfunction!(open, m)?)?;
    m.add_function(wrap_pyfunction!(capture, m)?)?;
    m.add_function(wrap_pyfunction!(rescue_erase_external, m)?)?;
    Ok(())
}
