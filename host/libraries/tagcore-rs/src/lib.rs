//! Host-side interface to a tag's firmware monitor, over an ST-LINK.
//!
//! This is a Rust port of the C++ `tagcore` tag interface in
//! `host/libraries/tagcore`: [`Tag`] corresponds to `Tag` (`tagclass.*`),
//! [`TagMonitor`] (the [`monitor`] folder) to `TagMonitor` (`tagmonitor.cc`,
//! `legacy_bittag_rescue.cc`) and [`LinkAdapt`] (the [`link`] folder) to
//! `LinkAdapt` (`linkadapt.*`). The layers are the same:
//!
//! - [`LinkAdapt`] finds and opens an ST-LINK with libusb and issues its USB
//!   commands: debug-register and memory access, target voltage, nRST.
//! - [`TagMonitor`] runs the monitor protocol over that link. On STM32L4 tags
//!   it uses the DebugMonitor exception and the DCRDR mailbox; on STM32U3 tags
//!   the shared block at [`monitor::abi::MONITOR_SHARED_ADDR`]. Either way an
//!   RPC is a serialised [`proto::Req`] written into the tag's buffer and a
//!   [`proto::Ack`] read back.
//! - [`Tag`] is the thread-safe facade the tools use: one method per request,
//!   plus the outcome of the last RPC.
//!
//! The wire behaviour, timeouts and attach sequences follow the C++ code; see
//! `docs/shared/monitor-interface.md` for the protocol itself.
//!
//! # Attaching resets the tag
//!
//! Every attach goes through reset. Attaching to a running tag ends the run
//! it was meant to observe, so never attach just to see what a tag is doing;
//! see `docs/bench/verifying-firmware.md`.
//!
//! # Other modules
//!
//! - [`proto`]: the protobuf types, generated from `proto/` by `build.rs`,
//!   with C++-compatible JSON in and out.
//! - [`textformat`]: protobuf text format laid out as the C++ prints it.
//! - [`configshape`]: which Config fields each tag type supports, from the
//!   embedded `default-config.json` files.
//!
//! # Errors and logging
//!
//! Requests to the firmware return `Result<_, `[`TagError`]`>`. The layers
//! below return `bool`/`Option` as the C++ does and log the reason through
//! the [`log`] facade, so install a logger (`RUST_LOG=debug` with
//! `env_logger` shows the attach detail).
//!
//! The architecture, and where the port deliberately differs from the C++,
//! are described in `host/libraries/tagcore-rs/design/architecture.md`.

pub mod configshape;
pub mod link;
pub mod monitor;
pub mod proto;
pub mod tag;
pub mod textformat;

pub use link::{LinkAdapt, LinkAdaptStats, UsbDev};
pub use monitor::{MemAccess, TagMonitor, TagMonitorStats, TargetFamily};
pub use tag::{Tag, TagError};
