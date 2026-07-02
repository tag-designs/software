#include "tagmonitor.h"

#include <cstdint>
#include <cstring>

extern "C"
{
#include "log.h"
}

namespace
{

// This file intentionally quarantines the field-recovery assumptions for old
// BitTag V6 firmware. These addresses are not a generic tagcore contract; they
// come from the legacy BitTag firmware map and are used only by
// tag-dwnld --rescue-exception.

constexpr uint32_t kStm32l4PwrCr1 = 0x40007000U;
constexpr uint32_t kStm32l4RtcBkp0r = 0x40002850U;
constexpr uint32_t kPwrCr1Dbp = 1U << 8;

// BackupState.state is the fourth 32-bit RTC backup register. Rescue download
// writes this word only; log cursors and flash contents are left untouched.
constexpr uint32_t kTagBackupStateAddr =
    kStm32l4RtcBkp0r + 3U * sizeof(uint32_t);

// Legacy BitTag V6 persistent layout.
constexpr uint32_t kBitTagVddHeaderAddr = 0x08007a68U;
constexpr uint32_t kBitTagPersistentEnd = 0x08040000U;
constexpr uint32_t kBitTagDataHeaderSize = 16U;
constexpr uint32_t kBitTagLogRecordsPerAck = 30U;

} // namespace

bool TagMonitor::ForceBackupState(TagState state)
{
  /*
   * Field-rescue helper for old firmware that cannot serve log RPCs while
   * pState->state is EXCEPTION. We enable backup-domain writes if needed,
   * write only BackupState.state, restore DBP, then verify by reading the same
   * word back over ST-LINK memory access.
   */
  if (!IsAttached())
  {
    log_error("Monitor not attached");
    return false;
  }

  uint32_t original_pwr_cr1 = 0;
  const bool have_pwr_cr1 =
      ReadMem32(kStm32l4PwrCr1, (uint8_t *)&original_pwr_cr1,
                sizeof(original_pwr_cr1));
  if (have_pwr_cr1 && ((original_pwr_cr1 & kPwrCr1Dbp) == 0U))
  {
    uint32_t writable_pwr_cr1 = original_pwr_cr1 | kPwrCr1Dbp;
    if (!WriteMem32(kStm32l4PwrCr1, (uint8_t *)&writable_pwr_cr1,
                    sizeof(writable_pwr_cr1)))
    {
      log_error("could not enable backup-domain writes");
      return false;
    }
  }

  uint32_t backup_state = static_cast<uint32_t>(state);
  const bool write_ok =
      WriteMem32(kTagBackupStateAddr, (uint8_t *)&backup_state,
                 sizeof(backup_state));

  if (have_pwr_cr1 && ((original_pwr_cr1 & kPwrCr1Dbp) == 0U))
  {
    (void)WriteMem32(kStm32l4PwrCr1, (uint8_t *)&original_pwr_cr1,
                     sizeof(original_pwr_cr1));
  }

  if (!write_ok)
  {
    log_error("could not write backup state");
    return false;
  }

  uint32_t verify_state = 0;
  if (!ReadMem32(kTagBackupStateAddr, (uint8_t *)&verify_state,
                 sizeof(verify_state)))
  {
    log_error("could not verify backup state");
    return false;
  }
  if (verify_state != backup_state)
  {
    log_error("backup state verify failed: wrote %u read %u",
              backup_state, verify_state);
    return false;
  }

  return true;
}

bool TagMonitor::CountBitTagLogHeaders(int &count)
{
  /*
   * Legacy BitTag stores one 16-byte t_DataHeader per sample in internal flash.
   * A blank record has epoch == 0xffffffff. Counting these records recovers the
   * number of samples when pState->pages was lost during an exception.
   */
  count = 0;
  if (!IsAttached())
  {
    log_error("Monitor not attached");
    return false;
  }

  for (uint32_t address = kBitTagVddHeaderAddr;
       (address + kBitTagDataHeaderSize) <= kBitTagPersistentEnd;
       address += kBitTagDataHeaderSize)
  {
    uint32_t header[kBitTagDataHeaderSize / sizeof(uint32_t)] = {0};
    if (!ReadMem32(address, (uint8_t *)header, sizeof(header)))
    {
      return false;
    }
    if (header[0] == 0xffffffffU)
    {
      return true;
    }
    count++;
  }

  return true;
}

bool TagMonitor::ReadBitTagLogFromFlash(Ack &ack, int index)
{
  /*
   * Synthesize a normal BitTagLog Ack from legacy BitTag flash records. This
   * does not interpret the 64-bit activity field; text/SQLite writers decode it
   * later using the recovered Config.bittag_log value.
   */
  ack.Clear();
  if (!IsAttached())
  {
    log_error("Monitor not attached");
    return false;
  }
  if (index < 0)
  {
    ack.set_err(Ack::OK);
    ack.mutable_bittag_data_log();
    return true;
  }

  ack.set_err(Ack::OK);
  BitTagLog *log = ack.mutable_bittag_data_log();

  for (uint32_t count = 0; count < kBitTagLogRecordsPerAck; count++)
  {
    const uint32_t record_index = static_cast<uint32_t>(index) + count;
    const uint32_t address =
        kBitTagVddHeaderAddr + record_index * kBitTagDataHeaderSize;
    if ((address + kBitTagDataHeaderSize) > kBitTagPersistentEnd)
    {
      return true;
    }

    uint8_t header[kBitTagDataHeaderSize] = {0};
    if (!ReadMem32(address, header, sizeof(header)))
    {
      return false;
    }

    int32_t epoch = 0;
    int16_t temp10 = 0;
    uint16_t vdd100 = 0;
    uint64_t activity = 0;
    memcpy(&epoch, &header[0], sizeof(epoch));
    memcpy(&temp10, &header[4], sizeof(temp10));
    memcpy(&vdd100, &header[6], sizeof(vdd100));
    memcpy(&activity, &header[8], sizeof(activity));

    if (static_cast<uint32_t>(epoch) == 0xffffffffU)
    {
      return true;
    }

    BitTagData *entry = log->add_data();
    entry->set_epoch(epoch);
    entry->set_temperature(temp10 * 0.1f);
    entry->set_voltage(vdd100 * 0.01f);
    entry->set_rawdata(activity);
  }

  return true;
}
