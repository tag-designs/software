/**
 * @file    tagrules.h
 * @brief   Small rules about a tag that qtmonitor's commands and displays
 *          share: which states can be downloaded, how far an erase has to
 *          go, and how far the tag's clock is off.
 *
 * @details Qt-free, so the rules can be checked without a window and ported
 *          as they are. Part of qtmonitor's core; see
 *          host/applications/qtmon/design/architecture.md.
 */

#ifndef QTMON_CORE_TAGRULES_H
#define QTMON_CORE_TAGRULES_H

#include <cstdint>
#include <string>

#include "linkadapt.h"
#include "tag.pb.h"

namespace qtmon {

/// External flash erase sector, bytes, for tags that do not report one.
inline constexpr int kDefaultSectorBytes = 4096;

/**
 * @brief Bytes per IMUTag data-log page, used only to estimate an erase's
 *        length for older IMUTag firmware.
 *
 * @details Firmware that predates Status.erase_sectors_total_plus_one does
 *          not say how many sectors an erase covers; newer firmware does, and
 *          then this is not used.
 */
inline constexpr int kImuDataLogPageBytes = 2048;

/**
 * @brief Whether a tag in @p state can be downloaded.
 *
 * @details EXCEPTION is included: the tag is no longer collecting, and
 *          downloading avoids a reset that could erase the evidence.
 *
 * @param[in] state  The tag's last reported state.
 * @return true for FINISHED, ABORTED and EXCEPTION.
 */
bool isDownloadableState(TagState state);

/**
 * @brief Number of erase sectors needed to cover @p bytes, rounded up.
 *
 * @param[in] bytes        Bytes of flash in use, or the flash size.
 * @param[in] sector_size  Erase sector size in bytes.
 * @return ceil(@p bytes / @p sector_size); 0 when either is zero or
 *         negative, so a tag with no geometry shows no erase progress.
 */
int roundedSectorCount(std::int64_t bytes, int sector_size);

/**
 * @brief How many sectors an erase will report, for its progress bar.
 *
 * @param[in] status               A status read before or during the erase.
 * @param[in] tag_type             The tag's type.
 * @param[in] external_flash_bytes TagInfo.extflashsz.
 * @param[in] sector_size          Erase sector size in bytes.
 * @return The tag's own total when it reports one
 *         (erase_sectors_total_plus_one); otherwise, for an IMUTag, an
 *         estimate from its page count; otherwise the sectors in the whole
 *         external flash.
 */
int eraseSectorMaximum(const Status &status, TagType tag_type,
                       std::int64_t external_flash_bytes,
                       int sector_size = kDefaultSectorBytes);

/**
 * @brief The tag's clock minus the host's, in seconds.
 *
 * @param[in] tag_millis   Status.millis: the tag's clock, ms since the epoch.
 * @param[in] host_millis  The host's clock when the status was read, ms since
 *                         the epoch.
 * @return Positive when the tag is ahead. An unset tag clock (near zero)
 *         gives about -1.45e9 s.
 */
double clockOffsetSeconds(std::int64_t tag_millis, std::int64_t host_millis);

/**
 * @brief The oldest qtmonitor version a tag may require.
 *
 * @details The tag can name it (TagInfo.qtmonitor_min_version), but no
 *          firmware sets it yet, so this fixed floor stands in for it.
 */
inline constexpr float kRequiredMonitorVersion = 2.0f;

/**
 * @brief Whether qtmonitor @p monitor_version may attach to tags.
 * @return false when it is older than kRequiredMonitorVersion; qtmonitor
 *         then says so and detaches.
 */
bool monitorVersionAccepted(float monitor_version);

/**
 * @brief How a tag base is listed when there are several to choose from:
 *        "BBB:AAA    0xVID:0xPID", bus and address zero-padded to three
 *        digits, the IDs in hexadecimal.
 */
std::string describeBase(const UsbDev &base);

} // namespace qtmon

#endif // QTMON_CORE_TAGRULES_H
