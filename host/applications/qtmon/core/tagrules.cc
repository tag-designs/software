/**
 * @file    tagrules.cc
 * @brief   The rules declared in tagrules.h.
 */

#include "tagrules.h"

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

} // namespace qtmon
