//! The host/firmware monitor ABI, mirrored from `include/monitor.h`.
//!
//! These values cross the SWD boundary, so they must match the header the
//! firmware is built with. `docs/shared/monitor-interface.md` describes both
//! transports; this module only restates the constants and the layout of the
//! STM32U3 shared block.
//!
//! A change on either side of the boundary must be made in `monitor.h`
//! and here together. The U3 attach checks [`MONITOR_SHARED_ABI_VERSION`]
//! and [`DEBUGVERSION`] and refuses a mismatch; the L4 transport has no
//! such check, so an L4 mismatch shows up as failed or nonsensical calls.

/// Monitor debug protocol version an STM32U3 tag must publish.
pub const DEBUGVERSION: u32 = 0x20;

/// Address of the STM32U3 shared monitor block, at the bottom of SRAM.
pub const MONITOR_SHARED_ADDR: u32 = 0x2000_0000;
/// Size in bytes reserved for the shared block; the struct uses the first
/// [`MonitorShared::SIZE`] of them.
pub const MONITOR_SHARED_SIZE: u32 = 0x40;

/// `request` value the host writes before releasing reset to ask for a session.
pub const MONITOR_REQUEST_MAGIC: u32 = 0xC0FF_EE00;
/// `request` value the firmware writes once it has published its metadata.
pub const MONITOR_CONNECTED_MAGIC: u32 = 0xDEAD_BEEF;
/// `path_magic` value identifying the U3 transport.
pub const MONITOR_PATH_U3_MAGIC: u32 = 0x5533_0003;
/// Shared-block layout version the host understands.
pub const MONITOR_SHARED_ABI_VERSION: u32 = 1;
/// `flags` bit set once the firmware's monitor watchdog and IRQ are armed.
pub const MONITOR_SHARED_FLAG_SESSION_READY: u32 = 1 << 0;
/// Seconds between the firmware's checks of `host_activity`.
pub const MONITOR_HEARTBEAT_PERIOD_S: u32 = 5;

/// External IRQ the host pends to kick the U3 monitor.
pub const MONITOR_SHARED_KICK_IRQN: u32 = 39;
/// NVIC ISPR word holding the kick IRQ.
pub const MONITOR_SHARED_KICK_ISPR_ADDR: u32 = 0xE000_E204;
/// NVIC ICPR word holding the kick IRQ.
pub const MONITOR_SHARED_KICK_ICPR_ADDR: u32 = 0xE000_E284;
/// Bit of the kick IRQ within its ISPR/ICPR word.
pub const MONITOR_SHARED_KICK_MASK: u32 = 1 << 7;

/// Values of the U3 shared block's `status` word (`enum MONITOR_STATUS`).
pub mod status {
    /// No command in progress.
    pub const IDLE: u32 = 0;
    /// The firmware has taken the command and is running it.
    pub const PENDING: u32 = 1;
    /// The command finished; `result` holds its value.
    pub const DONE: u32 = 2;
    /// The firmware refused the command because another was running.
    pub const BUSY: u32 = 3;
    /// The firmware did not recognise the command.
    pub const BAD_COMMAND: u32 = 4;
    /// The firmware has no monitor session.
    pub const NOT_ATTACHED: u32 = 5;
}

/// Monitor operations (`enum DBGOP`), the low byte of a command word.
pub mod op {
    /// Returns a piece of monitor metadata selected by a [`super::info`] operand.
    pub const TAG_MONITORINFO: u8 = 0;
    /// Starts the session; returns 1 on success, 0 on failure.
    pub const MONITORSTART: u8 = 1;
    /// Ends the session; the tag may enter low power immediately.
    pub const MONITORSTOP: u8 = 2;
    /// Runs the protobuf request in the call buffer; returns the reply length.
    pub const PROTOBUF: u8 = 3;
}

/// Operands for [`op::TAG_MONITORINFO`] (`enum DBGINFO`).
pub mod info {
    /// Monitor version.
    pub const MONITORVERSION: i32 = 0;
    /// Address of the RPC call buffer.
    pub const MONITORBUF: i32 = 1;
    /// Size of the RPC call buffer in bytes.
    pub const MONITORBUFSIZE: i32 = 2;
    /// Address of the firmware's git SHA string.
    pub const TAGSHASTR: i32 = 3;
}

/// Short name of a monitor operation, for log messages.
pub fn operation_name(operation: u8) -> &'static str {
    match operation {
        op::TAG_MONITORINFO => "TAG_MONITORINFO",
        op::MONITORSTART => "MONITORSTART",
        op::MONITORSTOP => "MONITORSTOP",
        op::PROTOBUF => "PROTOBUF",
        _ => "UNKNOWN",
    }
}

/// Packs an operation and its operand into the command word the firmware
/// decodes: operand in bits 31..8, operation in bits 7..0. A negative
/// operand keeps its two's-complement bits, as the C++ shift does.
pub fn command_word(operation: u8, operand: i32) -> u32 {
    ((operand as u32) << 8) | u32::from(operation)
}

/// The STM32U3 shared monitor block (`monitor_shared_t`): thirteen
/// little-endian words at [`MONITOR_SHARED_ADDR`].
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct MonitorShared {
    /// Session state: 0, [`MONITOR_REQUEST_MAGIC`] or [`MONITOR_CONNECTED_MAGIC`].
    pub request: u32,
    /// Layout version; must equal [`MONITOR_SHARED_ABI_VERSION`].
    pub abi_version: u32,
    /// Monitor protocol version; must equal [`DEBUGVERSION`].
    pub debug_version: u32,
    /// Session flags, such as [`MONITOR_SHARED_FLAG_SESSION_READY`].
    pub flags: u32,
    /// Address of the RPC call buffer.
    pub buf_addr: u32,
    /// Size of the RPC call buffer in bytes.
    pub buf_size: u32,
    /// Address of the firmware's git SHA string.
    pub sha_addr: u32,
    /// Pending command word, see [`command_word`]; 0 when idle.
    pub command: u32,
    /// Value returned by the last command.
    pub result: u32,
    /// Command progress, one of the [`status`] values.
    pub status: u32,
    /// Session heartbeat: set to 1 by the kick IRQ on every call and by the
    /// host while attaching or keeping an idle session alive; cleared by the
    /// firmware at each heartbeat check, which ends the session if it finds
    /// the word still clear.
    pub host_activity: u32,
    /// Firmware monitor watchdog tick count.
    pub watchdog_ticks: u32,
    /// Transport identifier, [`MONITOR_PATH_U3_MAGIC`] on U3.
    pub path_magic: u32,
}

impl MonitorShared {
    /// Size in bytes of the structure as laid out in tag memory.
    pub const SIZE: usize = 13 * 4;

    /// Byte offset of `request`.
    pub const REQUEST: u32 = 0;
    /// Byte offset of `command`.
    pub const COMMAND: u32 = 7 * 4;
    /// Byte offset of `result`.
    pub const RESULT: u32 = 8 * 4;
    /// Byte offset of `status`.
    pub const STATUS: u32 = 9 * 4;
    /// Byte offset of `host_activity`.
    pub const HOST_ACTIVITY: u32 = 10 * 4;

    /// Decodes the block from the bytes read at [`MONITOR_SHARED_ADDR`].
    pub fn from_bytes(bytes: &[u8; Self::SIZE]) -> Self {
        let w = |i: usize| u32::from_le_bytes(bytes[i * 4..i * 4 + 4].try_into().unwrap());
        Self {
            request: w(0),
            abi_version: w(1),
            debug_version: w(2),
            flags: w(3),
            buf_addr: w(4),
            buf_size: w(5),
            sha_addr: w(6),
            command: w(7),
            result: w(8),
            status: w(9),
            host_activity: w(10),
            watchdog_ticks: w(11),
            path_magic: w(12),
        }
    }

    /// True if any field is non-zero, i.e. the firmware has written the block.
    pub fn any_nonzero(&self) -> bool {
        *self != Self::default()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn command_word_packs_operand_above_operation() {
        assert_eq!(command_word(op::PROTOBUF, 0x1234), 0x0012_3403);
        assert_eq!(command_word(op::TAG_MONITORINFO, info::TAGSHASTR), 0x300);
        assert_eq!(command_word(op::MONITORSTART, -1), 0xFFFF_FF01);
    }

    #[test]
    fn shared_block_decodes_little_endian_words_in_order() {
        let mut bytes = [0u8; MonitorShared::SIZE];
        for (i, chunk) in bytes.chunks_mut(4).enumerate() {
            chunk.copy_from_slice(&(0x1000 + i as u32).to_le_bytes());
        }
        let shared = MonitorShared::from_bytes(&bytes);
        assert_eq!(shared.request, 0x1000);
        assert_eq!(shared.command, 0x1000 + MonitorShared::COMMAND / 4);
        assert_eq!(shared.result, 0x1000 + MonitorShared::RESULT / 4);
        assert_eq!(shared.status, 0x1000 + MonitorShared::STATUS / 4);
        assert_eq!(
            shared.host_activity,
            0x1000 + MonitorShared::HOST_ACTIVITY / 4
        );
        assert_eq!(shared.path_magic, 0x100c);
        assert!(shared.any_nonzero());
        assert!(!MonitorShared::default().any_nonzero());
    }

    #[test]
    fn shared_block_fits_reserved_hole() {
        assert!(MonitorShared::SIZE as u32 <= MONITOR_SHARED_SIZE);
    }
}
