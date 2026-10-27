//! Field-rescue helpers for old BitTag V6 firmware, used only by
//! `tag-dwnld --rescue-exception`.
//!
//! Port of `legacy_bittag_rescue.cc`. The addresses come from the legacy
//! BitTag firmware map and are not a general tagcore contract; they are kept
//! here, out of the monitor proper, for that reason.
//!
//! Old BitTag V6 firmware that hit an exception latches the EXCEPTION
//! state in its backup registers and then refuses internal-data log RPCs,
//! so its data cannot be downloaded the normal way. These helpers work around that over SWD, while the monitor
//! session is open: rewrite the state word in the RTC backup registers, or
//! read the sample records straight out of internal flash and wrap them in
//! the same [`Ack`] a log RPC would return. They write at most one backup
//! register (and PWR_CR1 transiently) and never touch flash.
//!
//! **STM32L4 only.** Nothing checks the target family; on any other part
//! these addresses mean something else.

use log::error;

use super::TagMonitor;
use crate::proto::{Ack, BitTagData, BitTagLog, TagState, ack};

/// STM32L4 PWR_CR1, which holds the backup-domain write-protection bit.
const STM32L4_PWR_CR1: u32 = 0x4000_7000;
/// STM32L4 RTC_BKP0R, the first of the RTC backup registers that survive
/// standby; the firmware's `BackupState` mirror starts here.
const STM32L4_RTC_BKP0R: u32 = 0x4000_2850;
/// PWR_CR1.DBP: disable backup-domain write protection while set.
const PWR_CR1_DBP: u32 = 1 << 8;

/// BackupState.state is the fourth RTC backup register. Rescue writes only
/// this word; log cursors and flash contents are left untouched.
const TAG_BACKUP_STATE_ADDR: u32 = STM32L4_RTC_BKP0R + 3 * 4;

// Legacy BitTag V6 persistent layout.
/// Flash address of the first legacy sample record (`t_DataHeader`).
const BITTAG_VDD_HEADER_ADDR: u32 = 0x0800_7a68;
/// End (exclusive) of the legacy persistent region: the top of the
/// STM32L432's 256 KiB flash.
const BITTAG_PERSISTENT_END: u32 = 0x0804_0000;
/// Size of one legacy sample record in bytes.
const BITTAG_DATA_HEADER_SIZE: u32 = 16;
/// Most records returned per synthesised log Ack (the C++
/// `kBitTagLogRecordsPerAck`); callers page by advancing `index` by the
/// number of records each Ack carries.
const BITTAG_LOG_RECORDS_PER_ACK: u32 = 30;

/// Decodes one legacy 16-byte `t_DataHeader`: epoch (i32), temperature in
/// tenths of a degree (i16), voltage in hundredths of a volt (u16), and the
/// 64-bit activity word. Returns `None` for a blank (erased) record.
///
/// The activity word is not interpreted here; the log writers decode it
/// later using the recovered `Config.bittag_log`.
pub(crate) fn decode_bittag_record(header: &[u8; 16]) -> Option<BitTagData> {
    let epoch = i32::from_le_bytes(header[0..4].try_into().unwrap());
    if epoch as u32 == 0xffff_ffff {
        return None;
    }
    let temp10 = i16::from_le_bytes(header[4..6].try_into().unwrap());
    let vdd100 = u16::from_le_bytes(header[6..8].try_into().unwrap());
    Some(BitTagData {
        epoch,
        temperature: f32::from(temp10) * 0.1,
        voltage: f32::from(vdd100) * 0.01,
        rawdata: u64::from_le_bytes(header[8..16].try_into().unwrap()),
    })
}

impl TagMonitor {
    /// Overwrites the tag's persisted state word (BackupState.state).
    ///
    /// For old firmware that cannot serve log RPCs while its state is
    /// EXCEPTION. Enables backup-domain writes (PWR_CR1.DBP) if needed,
    /// writes the one word, restores DBP, and verifies by reading the word
    /// back. STM32L4 addresses only. Returns false if not attached or any
    /// write or the verification fails.
    ///
    /// Old firmware serves internal-data log RPCs only in ABORTED or
    /// FINISHED, and reads its state from this register, so `tag-dwnld
    /// --rescue-exception` writes ABORTED here and downloads in the same
    /// session; no status RPC is needed (or trusted) afterwards, since the
    /// read-back is the verification. If PWR_CR1 cannot be read, DBP is
    /// assumed already enabled and left alone. Port of
    /// `TagMonitor::ForceBackupState`.
    pub fn force_backup_state(&mut self, state: TagState) -> bool {
        if !self.is_attached() {
            error!("Monitor not attached");
            return false;
        }

        let original_pwr_cr1 = self.read_word(STM32L4_PWR_CR1);
        let enabled_dbp = matches!(original_pwr_cr1, Some(v) if v & PWR_CR1_DBP == 0);
        if enabled_dbp && !self.write_word(STM32L4_PWR_CR1, original_pwr_cr1.unwrap() | PWR_CR1_DBP)
        {
            error!("could not enable backup-domain writes");
            return false;
        }

        let backup_state = state as i32 as u32;
        let write_ok = self.write_word(TAG_BACKUP_STATE_ADDR, backup_state);

        if enabled_dbp {
            let _ = self.write_word(STM32L4_PWR_CR1, original_pwr_cr1.unwrap());
        }

        if !write_ok {
            error!("could not write backup state");
            return false;
        }
        match self.read_word(TAG_BACKUP_STATE_ADDR) {
            None => {
                error!("could not verify backup state");
                false
            }
            Some(v) if v != backup_state => {
                error!("backup state verify failed: wrote {backup_state} read {v}");
                false
            }
            Some(_) => true,
        }
    }

    /// Counts the legacy BitTag's sample records in internal flash.
    ///
    /// One 16-byte header is stored per sample and a blank record has epoch
    /// 0xffffffff, so this recovers the sample count when the persisted page
    /// count was lost in an exception. Returns `None` if not attached or a
    /// read fails.
    ///
    /// Reads one word per record until a blank record or the end of the
    /// region, so it costs up to several thousand SWD reads on a full log.
    /// Port of `TagMonitor::CountBitTagLogHeaders`.
    pub fn count_bittag_log_headers(&mut self) -> Option<usize> {
        if !self.is_attached() {
            error!("Monitor not attached");
            return None;
        }
        let mut count = 0;
        let mut address = BITTAG_VDD_HEADER_ADDR;
        while address + BITTAG_DATA_HEADER_SIZE <= BITTAG_PERSISTENT_END {
            let first = self.read_word(address)?;
            if first == 0xffff_ffff {
                break;
            }
            count += 1;
            address += BITTAG_DATA_HEADER_SIZE;
        }
        Some(count)
    }

    /// Builds a normal BitTagLog [`Ack`] from up to 30 legacy flash records
    /// starting at record `index`, stopping at the first blank record or the
    /// end of the region.
    ///
    /// A negative `index` yields an OK Ack with an empty log. Returns `None`
    /// if not attached or a read fails.
    ///
    /// An empty log means there are no more records, as with the firmware's
    /// own log RPC. Port of `TagMonitor::ReadBitTagLogFromFlash`.
    pub fn read_bittag_log_from_flash(&mut self, index: i32) -> Option<Ack> {
        if !self.is_attached() {
            error!("Monitor not attached");
            return None;
        }
        let mut log = BitTagLog::default();
        if index >= 0 {
            for n in 0..BITTAG_LOG_RECORDS_PER_ACK {
                let record = index as u32 + n;
                let address = BITTAG_VDD_HEADER_ADDR + record * BITTAG_DATA_HEADER_SIZE;
                if address + BITTAG_DATA_HEADER_SIZE > BITTAG_PERSISTENT_END {
                    break;
                }
                let mut header = [0u8; 16];
                if !self.link.read_mem32(address, &mut header) {
                    return None;
                }
                match decode_bittag_record(&header) {
                    Some(entry) => log.data.push(entry),
                    None => break,
                }
            }
        }
        Some(Ack {
            err: ack::Err::Ok as i32,
            payload: Some(ack::Payload::BittagDataLog(log)),
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn legacy_record_decodes_scaled_fields() {
        let mut header = [0u8; 16];
        header[0..4].copy_from_slice(&1_700_000_000i32.to_le_bytes());
        header[4..6].copy_from_slice(&(-123i16).to_le_bytes());
        header[6..8].copy_from_slice(&300u16.to_le_bytes());
        header[8..16].copy_from_slice(&0x0102_0304_0506_0708u64.to_le_bytes());
        let entry = decode_bittag_record(&header).unwrap();
        assert_eq!(entry.epoch, 1_700_000_000);
        assert!((entry.temperature + 12.3).abs() < 1e-5);
        assert!((entry.voltage - 3.0).abs() < 1e-5);
        assert_eq!(entry.rawdata, 0x0102_0304_0506_0708);
    }

    #[test]
    fn blank_record_ends_the_log() {
        assert!(decode_bittag_record(&[0xff; 16]).is_none());
    }

    #[test]
    fn backup_state_is_fourth_backup_register() {
        assert_eq!(TAG_BACKUP_STATE_ADDR, 0x4000_285C);
    }
}
