//! Download an image into a halted tag's SRAM and call its functions, with
//! the calling convention STM32CubeProgrammer uses for external loaders.
//!
//! Ported from the deleted C++ `tagcore/recovery/sramcall.cc`. The convention, as traced from
//! CubeProgrammer (`embedded/loaders/design/loader-runtime.md`, "The
//! contract"):
//!
//! - the loadable SRAM segments are written at their link addresses;
//!   segments outside SRAM, such as a loader's `StorageInfo` descriptor at
//!   address 0, are not downloaded;
//! - a BKPT return trap sits at the start of SRAM, and LR points at it with
//!   the Thumb bit set, so a function returns into a halt;
//! - MSP is set about 1 KB past the image;
//! - arguments go in R0-R3, every other register is zeroed, and the result
//!   comes back in R0.
//!
//! The transfer buffer follows the stack and runs to the end of the first
//! SRAM region (at most 32 KB), so a caller can pass
//! [`SramCall::buffer_address`] as a pointer argument and read it back over
//! SWD. The core runs with DHCSR.C_MASKINTS set ([`Session::run`]), so the
//! tag's interrupts are not taken even if the image enables them.
//!
//! Downloading overwrites the start of SRAM1: capture SRAM first if it
//! matters.

use std::time::Duration;

use super::image::TargetImage;
use super::session::{REG_LR, REG_MSP, REG_PC, REG_R0, REG_SP, REG_XPSR, Session};

/// Stack reserved past the image; CubeProgrammer gives about 1 KB.
const STACK_BYTES: u32 = 1024;
/// Largest transfer buffer: the session's largest single read.
const MAX_BUFFER: u32 = 32768;
/// xPSR with only the Thumb bit set.
const XPSR_THUMB: u32 = 1 << 24;
/// `BKPT #0` in both halfwords of the trap word.
const TRAP_WORD: u32 = 0xBE00_BE00;

/// Rounds `v` up to a multiple of `a`, a power of two.
fn align_up(v: u32, a: u32) -> u32 {
    (v + a - 1) & !(a - 1)
}

/// Where a downloaded image's call frame and buffer are.
///
/// Holds no reference to the session: each call takes it, so one session
/// can be used for SWD reads between calls.
#[derive(Debug, Clone)]
pub struct SramCall {
    /// The downloaded image.
    image: TargetImage,
    /// Address of the BKPT trap word, the first word of SRAM1.
    trap: u32,
    /// Initial MSP for each call.
    stack_top: u32,
    /// First byte of the transfer buffer.
    buffer: u32,
    /// Transfer buffer length in bytes, a multiple of 512.
    buffer_size: u32,
    /// Symbol started by [`start`](Self::start), for messages.
    running: String,
}

impl SramCall {
    /// Writes the image's SRAM segments and the return trap.
    ///
    /// Each segment is padded with zeros to its memory size, written, and
    /// read back. The session must be open with the core halted.
    ///
    /// # Errors
    ///
    /// The session is not open; a segment overlaps the trap word, runs past
    /// SRAM1 or is not word aligned; a segment did not read back identical;
    /// the image has nothing for SRAM; the trap could not be written; or
    /// there is no room left for a 512-byte buffer.
    pub fn download(s: &mut Session, image: &TargetImage) -> Result<SramCall, String> {
        let Some(sram) = s.mcu().and_then(|m| m.sram.first()).filter(|_| s.is_open()) else {
            return Err("session not open".into());
        };
        let (base, end) = (sram.addr, sram.addr + sram.size);

        let mut image_end = base + 4; // the trap word
        for seg in image.segments() {
            if seg.addr < base || seg.addr >= end {
                continue; // not for SRAM, e.g. a loader's descriptor at 0
            }
            if seg.addr < base + 4 || u64::from(seg.addr) + u64::from(seg.memsz) > u64::from(end) {
                return Err(format!(
                    "segment at 0x{:08X} overlaps the trap or runs past 0x{end:08X}",
                    seg.addr
                ));
            }
            if !seg.addr.is_multiple_of(4) {
                return Err(format!("segment at 0x{:08X} is not word aligned", seg.addr));
            }
            let mut bytes = vec![0u8; align_up(seg.memsz, 4) as usize];
            bytes[..seg.data.len()].copy_from_slice(&seg.data);
            let mut back = vec![0u8; bytes.len()];
            if !s.write(seg.addr, &bytes) || !s.read(seg.addr, &mut back, 4096, None) || back != bytes {
                return Err(format!("segment at 0x{:08X} did not write back", seg.addr));
            }
            image_end = image_end.max(seg.addr + seg.memsz);
        }
        if image_end == base + 4 {
            return Err(format!("{} has nothing to load into SRAM", image.path().display()));
        }

        if !s.write_word(base, TRAP_WORD) {
            return Err("could not write the return trap".into());
        }

        let stack_top = align_up(image_end + STACK_BYTES, 8);
        let buffer = align_up(stack_top + 64, 512);
        let buffer_size = if buffer < end {
            MAX_BUFFER.min((end - buffer) & !511)
        } else {
            0
        };
        if buffer_size < 512 {
            return Err("no room for a transfer buffer".into());
        }
        Ok(SramCall {
            image: image.clone(),
            trap: base,
            stack_top,
            buffer,
            buffer_size,
            running: String::new(),
        })
    }

    /// Calls the function named `symbol` with up to four arguments in R0-R3
    /// and waits up to `timeout` for it to return; the result is R0.
    ///
    /// Leaves the core halted.
    ///
    /// # Errors
    ///
    /// As [`start`](Self::start) and [`wait_return`](Self::wait_return):
    /// no such symbol, a timeout (the core is halted where it is), an SWD
    /// fault, or a halt anywhere but the trap, such as a fault handler or a
    /// breakpoint in the image.
    pub fn call(
        &mut self,
        s: &mut Session,
        symbol: &str,
        args: &[u32],
        timeout: Duration,
    ) -> Result<u32, String> {
        self.start(s, symbol, args)?;
        self.wait_return(s, timeout)
    }

    /// Sets up the same frame as [`call`](Self::call), lets the core run,
    /// and returns at once.
    ///
    /// For a function that does not return until told to, such as a
    /// loader's `Serve()`; finish with [`wait_return`](Self::wait_return).
    ///
    /// # Errors
    ///
    /// No symbol of that name; more than four arguments; a core register
    /// could not be set; or the core could not be started.
    pub fn start(&mut self, s: &mut Session, symbol: &str, args: &[u32]) -> Result<(), String> {
        let entry = self
            .image
            .symbol(symbol)
            .ok_or_else(|| format!("no symbol {symbol} in {}", self.image.path().display()))?;
        if args.len() > 4 {
            return Err("at most four arguments".into());
        }
        let mut r = [0u32; 13];
        r[..args.len()].copy_from_slice(args);
        for (reg, value) in r.iter().enumerate() {
            if !s.write_core_register(REG_R0 + reg as u32, *value) {
                return Err(format!("could not set R{reg}"));
            }
        }
        if !s.write_core_register(REG_MSP, self.stack_top)
            || !s.write_core_register(REG_SP, self.stack_top)
            || !s.write_core_register(REG_LR, self.trap | 1)
            || !s.write_core_register(REG_PC, entry & !1)
            || !s.write_core_register(REG_XPSR, XPSR_THUMB)
        {
            return Err("could not set up the call frame".into());
        }
        if !s.run() {
            return Err("could not start the core".into());
        }
        self.running = symbol.to_string();
        Ok(())
    }

    /// Waits up to `timeout` for a function started by
    /// [`start`](Self::start) to return; the result is R0.
    ///
    /// # Errors
    ///
    /// A timeout (the core is then halted, and the message gives its PC);
    /// the result could not be read; or the core halted somewhere other than
    /// the trap.
    pub fn wait_return(&mut self, s: &mut Session, timeout: Duration) -> Result<u32, String> {
        if !s.wait_halt(timeout).0 {
            s.halt();
            let pc = s.read_core_register(REG_PC).unwrap_or(0);
            return Err(format!(
                "{} did not return within {} ms (halted at pc 0x{pc:08X})",
                self.running,
                timeout.as_millis()
            ));
        }
        let (Some(pc), Some(result)) = (s.read_core_register(REG_PC), s.read_core_register(REG_R0))
        else {
            return Err("could not read the result".into());
        };
        if pc & !1 != self.trap {
            return Err(format!("{} halted at 0x{pc:08X}, not the return trap", self.running));
        }
        Ok(result)
    }

    /// The downloaded image.
    pub fn image(&self) -> &TargetImage {
        &self.image
    }

    /// First byte of the transfer buffer.
    pub fn buffer_address(&self) -> u32 {
        self.buffer
    }

    /// Transfer buffer length in bytes, a multiple of 512.
    pub fn buffer_size(&self) -> u32 {
        self.buffer_size
    }

    /// Initial MSP for each call.
    pub fn stack_top(&self) -> u32 {
        self.stack_top
    }
}
