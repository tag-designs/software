/**
 * @file    downloadjob.h
 * @brief   Downloads a stopped tag's data log into a log file: the loop
 *          qtmonitor's File ▸ Download Data… runs.
 *
 * @details Plain C++ with no thread or Qt of its own: run() blocks until the
 *          download ends, so the caller decides where it runs. In qtmonitor
 *          that is the tag session's worker thread, which owns the Tag. The
 *          sequence follows the command-line tag-dwnld (host/commandline/
 *          dwnld.cc): the writer decides how far each page advances the
 *          download and where it ends. See
 *          host/applications/qtmon/design/architecture.md.
 */

#ifndef QTMON_CORE_DOWNLOADJOB_H
#define QTMON_CORE_DOWNLOADJOB_H

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "tag.pb.h"
#include "tagclass.h"
#include "taglogwriter.h"

namespace qtmon {

/// A file type a tag's download can be saved as, for the save dialog.
struct DownloadFormat {
  TagLogStorageFormat format;  ///< The writer's format.
  std::string filter;          ///< The save dialog's filter, e.g. "SQLite database (*.db3)".
  std::string extension;       ///< With its dot, e.g. ".db3".
};

/**
 * @brief The formats a download from a tag of @p tag_type can be saved as,
 *        the default first.
 * @return Empty when the tag type has no log format, so it cannot be
 *         downloaded.
 */
std::vector<DownloadFormat> downloadFormats(TagType tag_type);

/// The file name the save dialog suggests: "untitled" with the default format's extension.
std::string defaultDownloadName(TagType tag_type);

/**
 * @brief The format the user chose in the save dialog.
 * @param[in] formats  downloadFormats(); not empty.
 * @param[in] filter   The dialog's selected filter.
 * @return The format with that filter, or the default (the first).
 */
const DownloadFormat &formatForFilter(const std::vector<DownloadFormat> &formats,
                                      const std::string &filter);

/**
 * @brief The file a download is written to, from the path the user chose.
 *
 * @details A name with no extension gets the chosen format's. A name with
 *          another download format's extension has it replaced by the chosen
 *          one's (compared without regard to case). Any other extension is
 *          kept as the user typed it.
 *
 * @param[in] chosen    The path from the save dialog.
 * @param[in] selected  The chosen format.
 * @param[in] formats   downloadFormats().
 */
std::string downloadPath(const std::string &chosen, const DownloadFormat &selected,
                         const std::vector<DownloadFormat> &formats);

/// How serious a message from the download is.
enum class LogLevel { Debug, Info, Warning, Error };

/// How a download ended.
struct DownloadResult {
  bool ok = false;         ///< The log was written (possibly empty, or cut short by cancel).
  bool cancelled = false;  ///< The caller cancelled; what was downloaded is kept.
  int records = 0;         ///< Log records written (the next download index).
  std::string error;       ///< Why it failed; empty when ok.
};

/**
 * @class   DownloadJob
 * @brief   One download of the attached tag's log into one file.
 *
 * @details run() reads the status (for the record count) and the
 *          configuration, creates the file with createTagLogWriter(), writes
 *          the header from the tag, then requests pages from index 0. Each
 *          page's records advance the index; a NODATA page below the record
 *          count advances it by one; the download ends when the writer
 *          consumes nothing or the count is reached. Cancelling stops between
 *          pages and keeps what was written.
 *
 * @warning run() makes blocking monitor calls on @p tag for as long as the
 *          download takes. Nothing else may use the Tag meanwhile.
 */
class DownloadJob {
public:
  /// Receives progress: records written so far, and the record count.
  using Progress = std::function<void(int value, int maximum)>;
  /// Receives the download's log messages.
  using Log = std::function<void(LogLevel level, const std::string &text)>;

  /**
   * @param[in] tag     The attached tag; must stay attached through run().
   * @param[in] format  Output format (SQLite).
   * @param[in] path    Output file; replaced if it exists.
   */
  DownloadJob(Tag &tag, TagLogStorageFormat format, std::string path);

  /**
   * @brief Downloads the log.
   *
   * @param[in] cancel    Checked between pages; set it from any thread.
   * @param[in] progress  Called with (0, count) before the first page and
   *                      after each one.
   * @param[in] log       Called with the download's messages.
   * @return How it ended. A tag with no records gives ok and 0 records,
   *         and creates no file.
   */
  DownloadResult run(const std::atomic_bool &cancel, const Progress &progress,
                     const Log &log);

private:
  Tag &tag_;                    ///< The attached tag.
  TagLogStorageFormat format_;  ///< Output format.
  std::string path_;            ///< Output file.
};

} // namespace qtmon

#endif // QTMON_CORE_DOWNLOADJOB_H
