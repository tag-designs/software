---
type: readme
status: current
summary: The tagcore Python package - SWD recovery sessions, loader sessions on external flash, captures and the rescue erase from Python, through a PyO3 binding of tagcore-rs's recovery module; building it with maturin and its checks.
---

# tagcore (Python)

The SWD capture and recovery library, from Python. It is a PyO3 binding of
tagcore-rs's `recovery` module, so it does exactly what `tag-capture`,
`tag-xflash` and `tag-sramcall` do: attach with the core halted at its reset
vector so that no firmware runs, read memory, drive the external-flash
loaders through `Serve()`, capture, and run rescue step 1. How that works is
in [SWD Capture and Recovery Library](../../libraries/tagcore-rs/design/swd-recovery.md).

```python
import tagcore

with tagcore.swd.open() as s:                 # the only base; or base="<serial>"
    print(s.mcu_name, s.uid, hex(s.attach["pc"]))
    bkp = s.read_region("backup_regs")        # pState, as the tag left it
    with s.external_flash("AT25XE_PresTagv3") as xf:
        print(hex(xf.jedec), xf.size, xf.writable)
        data = xf.read(0, xf.size, progress=lambda done, total: None)
# leaving the session resets the tag, as tag-capture does (exit="reset")

cap = tagcore.recovery.capture("captures", reason="returned from site 3",
                               sram=True, with_build="build/", progress=print)
print(cap["dir"], cap["complete"], cap["identity"]["target"])
```

## The API

`tagcore.swd`:

| Name | What it does |
| --- | --- |
| `bases()` | The bases on USB: `[{"serial", "bus", "address"}]` |
| `open(base=None, exit="reset")` | A `Session`: attaches halted at the reset vector. `base` is a serial or `bus:address`; without it, the only base. `exit` is `"reset"` or `"halted"` |
| `Session.mcu_name`, `.uid`, `.attach` | The part, its 96-bit UID (the monitor's `info.uuid` form), and what the attach found (`pc`, `reset_vector`, `halted`, `voltage`, `dhcsr_before`, `demcr_before`, ...) |
| `Session.read(addr, length)`, `.read_word(addr)` | Target memory, any alignment, as `bytes` |
| `Session.write(addr, data)` | Word-aligned writes |
| `Session.regions()`, `.read_region(name)` | Named regions: `internal_flash`, `backup_regs`, `sram1`, `sram2`, the register blocks and information regions |
| `Session.halt()`, `.run()`, `.close(exit=None)` | Core control and the declared exit |
| `Session.external_flash(loader, loader_dirs=None)` | An `ExternalFlash`: the loader, by path or by name under `loader_dirs`, `$TAG_LOADER_DIR` or `build-host/embedded/loaders`, running `Serve()` |
| `ExternalFlash.jedec`, `.sr1`, `.size`, `.sector_size`, `.writable`, `.page_bytes`, `.loader` | The part as the loader found it |
| `ExternalFlash.read(offset, length, progress=None)` | Data, with `progress(done, total)` |
| `ExternalFlash.read_page(page, raw=False)` | One NAND page, data and spare: `(bytes, status, status2)` |
| `ExternalFlash.erase_sector(offset)`, `.program(offset, data)` | With a `-RW` loader; a read-only one raises |

`tagcore.recovery`, each in its own session:

| Name | What it does |
| --- | --- |
| `capture(out_dir="captures", reason="", sram=False, external=True, loader=None, loader_dirs=None, external_full=False, with_build=None, extra=None, exit="reset", base=None, progress=None)` | A capture, as `tag-capture` takes it; returns `dir`, `manifest`, `mcu`, `uid`, `complete`, `identity`, `external_note`, `regions`, `build` |
| `rescue_erase_external(loader, capture=None, no_capture=False, loader_dirs=None, base=None, progress=None)` | Rescue step 1, as `tag-xflash rescue-erase` runs it; `progress(done, total, erased)` |

Every failure raises `tagcore.RecoveryError` (bad arguments raise
`ValueError`). Sessions and loader sessions are context managers, and every
long operation releases the GIL, so other Python threads run while the base
works. A session owns the base: a monitor session (`tag-info`, Tag Monitor)
cannot use it meanwhile. Downloading a loader overwrites the start of SRAM1,
so read SRAM first if it matters.

## Building

maturin builds the package into a virtual environment. The crate is a member
of the host Cargo workspace (`host/Cargo.toml`) but not of the CMake build.

```sh
python3 -m venv ~/.venvs/tagcore
~/.venvs/tagcore/bin/pip install maturin pytest
cd host/python/tagcore-py
VIRTUAL_ENV=~/.venvs/tagcore ~/.venvs/tagcore/bin/maturin develop --release
```

The module is built for the stable ABI (`abi3`, Python 3.9 and later), so one
build serves every newer interpreter. `maturin build --release` makes a
wheel. The crate links tagcore-rs statically, libusb included (rusb's
`vendored` feature), so the wheel needs no other library.

## Checks

- `tests/`: offline checks that need no tag (the API is there, arguments are
  refused before anything attaches); `~/.venvs/tagcore/bin/pytest tests`.
- [`embedded/tools/xflash_write_check.py`](../../../embedded/tools/xflash_write_check.py):
  the loaders README's write sequence through the binding (pattern,
  overwrite, refuse, restore), on a NOR tag. It passed on the bench PresTag
  `20333050364150040063005F` on 2026-11-06 in 165 s, leaving the part and
  the backup registers as found.
- On the same tag the same day: `capture()` wrote the same internal flash a
  session reads directly, and a Python thread kept running during a long
  read (the GIL is released).

Rust documentation: `cargo doc -p tagcore-py --no-deps --document-private-items`.
