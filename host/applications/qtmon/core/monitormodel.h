/**
 * @file    monitormodel.h
 * @brief   What qtmonitor knows about the attached tag, and how each status
 *          read changes it.
 *
 * @details The tag session reports facts (attached, a status, a command
 *          sent); MonitorModel keeps them and decides what they mean: the
 *          clock offset, whether a self-test or a start has finished, how far
 *          an erase has got, and the tag's debug text as whole lines. The
 *          window draws the model and acts on the events applyStatus()
 *          returns. Qt-free, and it never touches the tag, so its rules can be
 *          checked (test/qtmon_core_check.cc) and ported as they are. See
 *          host/applications/qtmon/design/architecture.md.
 */

#ifndef QTMON_CORE_MONITORMODEL_H
#define QTMON_CORE_MONITORMODEL_H

#include <cstdint>
#include <string>
#include <vector>

#include "commands.h"
#include "tag.pb.h"

namespace qtmon {

/// How often the tag session reads the attached tag's status, ms.
inline constexpr int kStatusPollMs = 400;
/**
 * @brief A self-test result is accepted only from a status read at least
 *        this long after Self-Test was sent, ms.
 *
 * @details The tag runs every test in one pass, usually faster than a poll,
 *          and a status read before it has begun still shows the previous
 *          run's result.
 */
inline constexpr std::int64_t kTestMinimumMs = 1000;
/// Self-Test gives up waiting for a result after this long, ms.
inline constexpr std::int64_t kTestTimeoutMs = 60000;
/**
 * @brief The attached tag counts as not answering when no status has got
 *        through for this long, ms: about seven missed polls.
 *
 * @details A failed status read is skipped silently, so without this a tag
 *          (or base) that stops answering looks like a tag whose state has
 *          not changed. Bursts of base errors (0x81) while a tag is busy can
 *          cause it; see embedded/bases/TODO.md.
 */
inline constexpr std::int64_t kNoAnswerMs = 3000;
/**
 * @brief Start is reported as failed when the tag has not reached RUNNING
 *        or CONFIGURED this long after it was sent, ms.
 *
 * @details An acknowledgement is not a completion, and a lost one is not a
 *          failure: Tag::Start() can report a transient USB or monitor error
 *          after the tag accepted the command. So Start is judged by the
 *          state the tag reaches, not by the call's result.
 */
inline constexpr std::int64_t kStartWaitMs = 7000;

/// Something a status read settled, for the window to report.
struct MonitorEvent {
  /// What happened.
  enum class Kind {
    TagLogLine,      ///< @c text is one whole line of the tag's debug text.
    TestFinished,    ///< The self-test finished with @c test.
    TestTimedOut,    ///< No self-test result within kTestTimeoutMs.
    StartSucceeded,  ///< The tag reached @c state; @c text is a start error it overcame, or empty.
    StartFailed,     ///< The tag is still in @c state after kStartWaitMs; @c text is the start error, or empty.
    EraseFinished,   ///< The tag returned to IDLE after an erase.
  };
  Kind kind;                              ///< See Kind.
  std::string text;                       ///< See Kind.
  TestResult test = TEST_UNSPECIFIED;     ///< For TestFinished.
  TagState state = STATE_UNSPECIFIED;     ///< For StartSucceeded and StartFailed.
};

/**
 * @class   MonitorModel
 * @brief   The attached tag as qtmonitor knows it: identity, latest status,
 *          and the commands still waiting on the tag.
 *
 * @details Fed by the session's reports on the GUI thread: attached(),
 *          detached() and applyStatus(), and by the window as it sends
 *          commands (testSent(), startSent(), eraseSent(), download...()).
 *          Not thread-safe.
 */
class MonitorModel {
public:
  // ---- What the model knows ----

  /// A tag (live or fixture) is attached.
  bool attached() const { return attached_; }
  /// The attached tag's identity; empty when detached.
  const TagInfo &info() const { return info_; }
  /// A status has been read since attaching.
  bool hasStatus() const { return has_status_; }
  /// The latest status; meaningful when hasStatus().
  const Status &status() const { return status_; }
  /// The tag's state at the latest status; STATE_UNSPECIFIED before one.
  TagState state() const { return has_status_ ? status_.state() : STATE_UNSPECIFIED; }
  /// Base-reported supply voltage at the latest status, volts.
  float voltage() const { return voltage_; }
  /// Tag clock minus host clock at the latest status, seconds.
  double clockOffsetS() const { return clock_offset_s_; }
  /// The self-test result the latest status reported.
  TestResult lastTest() const { return has_status_ ? status_.test_status() : TEST_UNSPECIFIED; }
  /// Self-Test was sent and its result has not been seen.
  bool testPending() const { return test_pending_; }
  /// Start was sent and the tag has not yet reached RUNNING or CONFIGURED.
  bool startPending() const { return start_pending_; }
  /// A download is running.
  bool downloading() const { return downloading_; }
  /// An erase was accepted and the tag has not yet returned to IDLE.
  bool erasing() const { return erasing_; }
  /// Sectors erased so far, from the latest status.
  int eraseValue() const { return erase_value_; }
  /// Sectors the erase will cover; never decreases during one erase.
  int eraseMaximum() const { return erase_maximum_; }

  /**
   * @brief How long the attached tag has gone without a status getting
   *        through, ms.
   *
   * @param[in] now_ms  Host clock, ms since the epoch.
   * @return 0 when detached, before the first status, or while a download
   *         runs (it pauses the polls on purpose); otherwise the time since
   *         the latest status. kNoAnswerMs or more means the tag is not
   *         answering.
   */
  std::int64_t silenceMs(std::int64_t now_ms) const;

  /**
   * @brief The inputs of the command rule (commands.h), with the document's
   *        facts supplied by the caller.
   * @param[in] document       The Configuration tab holds a document.
   * @param[in] document_type  Its tag type.
   */
  CommandInputs commandInputs(bool document, TagType document_type) const;

  // ---- What the session reports ----

  /// A tag was attached: forgets everything about the previous one.
  void attached(const TagInfo &info);
  /// The tag was detached: forgets it, and every command waiting on it.
  void detached();

  /**
   * @brief One status read: records it, and settles whatever it settles.
   *
   * @param[in] status   The status.
   * @param[in] voltage  Base-reported supply voltage, volts.
   * @param[in] host_ms  Host clock when the status was read, ms since the epoch.
   * @return In order: one TagLogLine per whole line of debug text completed
   *         by this status; TestFinished or TestTimedOut; StartSucceeded or
   *         StartFailed; EraseFinished.
   */
  std::vector<MonitorEvent> applyStatus(const Status &status, float voltage,
                                        std::int64_t host_ms);

  // ---- What the window sends ----

  /// Self-Test was sent at @p now_ms; its result is awaited.
  void testSent(std::int64_t now_ms);
  /// The tag refused Self-Test; nothing is awaited.
  void testRefused() { test_pending_ = false; }

  /**
   * @brief Start was sent at @p now_ms.
   * @param[in] now_ms        Host clock, ms since the epoch.
   * @param[in] call_message  The tag's message if Tag::Start() reported
   *                          failure; empty if it succeeded. Kept for the
   *                          StartSucceeded or StartFailed event.
   */
  void startSent(std::int64_t now_ms, const std::string &call_message);

  /**
   * @brief The tag accepted Erase: progress is tracked until it is IDLE.
   * @param[in] sector_size  External flash erase sector, bytes.
   */
  void eraseSent(int sector_size = 4096);

  /// A download started.
  void downloadStarted() { downloading_ = true; }
  /// The download ended, however it ended.
  void downloadEnded() { downloading_ = false; }

private:
  /// Appends @p text to the debug-line buffer; returns each completed line.
  std::vector<std::string> takeDebugLines(const std::string &text);

  bool attached_ = false;          ///< See attached().
  TagInfo info_;                   ///< See info().
  bool has_status_ = false;        ///< See hasStatus().
  Status status_;                  ///< See status().
  std::int64_t status_ms_ = 0;     ///< Host clock at the latest status.
  float voltage_ = 0.0f;           ///< See voltage().
  double clock_offset_s_ = 0.0;    ///< See clockOffsetS().
  bool test_pending_ = false;      ///< See testPending().
  std::int64_t test_sent_ms_ = 0;  ///< When Self-Test was sent.
  bool start_pending_ = false;     ///< See startPending().
  std::int64_t start_sent_ms_ = 0; ///< When Start was sent.
  std::string start_message_;      ///< Tag::Start()'s error, if it reported one.
  bool downloading_ = false;       ///< See downloading().
  bool erasing_ = false;           ///< See erasing().
  int erase_value_ = 0;            ///< See eraseValue().
  int erase_maximum_ = 0;          ///< See eraseMaximum().
  std::string debug_line_;         ///< Debug text received without its newline yet.
};

} // namespace qtmon

#endif // QTMON_CORE_MONITORMODEL_H
