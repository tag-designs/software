/**
 * @file    tagrules.cc
 * @brief   The rules declared in tagrules.h.
 */

#include "tagrules.h"

#include <cstdio>

namespace qtmon {

bool isDownloadableState(TagState state)
{
  return state == FINISHED || state == ABORTED || state == EXCEPTION;
}

int roundedSectorCount(std::int64_t bytes, int sector_size)
{
  if (bytes <= 0 || sector_size <= 0)
    return 0;
  return static_cast<int>((bytes + sector_size - 1) / sector_size);
}

int eraseSectorMaximum(const Status &status, TagType tag_type,
                       std::int64_t external_flash_bytes, int sector_size)
{
  if (status.erase_sectors_total_plus_one() > 0)
    return status.erase_sectors_total_plus_one() - 1;
  if (tag_type == IMUTAG)
    return roundedSectorCount(
        static_cast<std::int64_t>(status.external_data_count()) * kImuDataLogPageBytes,
        sector_size);
  return roundedSectorCount(external_flash_bytes, sector_size);
}

double clockOffsetSeconds(std::int64_t tag_millis, std::int64_t host_millis)
{
  // As a double throughout, as qtmonitor always computed it.
  const double host = static_cast<double>(host_millis);
  return (static_cast<double>(tag_millis) - host) / 1000.0;
}

bool monitorVersionAccepted(float monitor_version)
{
  return monitor_version >= kRequiredMonitorVersion;
}

std::string describeBase(const UsbDev &base)
{
  char text[48];
  std::snprintf(text, sizeof text, "%03u:%03u    0x%x:0x%x", unsigned(base.bus),
                unsigned(base.address), unsigned(base.vid), unsigned(base.pid));
  return text;
}

} // namespace qtmon
