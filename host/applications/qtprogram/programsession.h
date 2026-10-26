/**
 * @file    programsession.h
 * @brief   The program sequence for one tag: stop, erase, program, test.
 *
 * @details qtprogram's sequence, moved out of the window and made
 *          non-blocking. It no longer starts the tag after the tests (a start
 *          belongs to a deployment; see programsession.cpp), so the tag is
 *          left IDLE: STM32_Programmer_CLI runs asynchronously, and
 *          every wait is a one-second tick, so the window repaints and the
 *          tag's state is polled throughout. After the self-tests the session
 *          reads TagInfo again and compares the tag's git hash with the
 *          image's, so a tag not running the image just written is caught
 *          ("verify what you built", docs/bench/verifying-firmware.md).
 *
 * @see     README.md ("Program sequence"), design/batch-station.md
 */

#ifndef PROGRAMSESSION_H
#define PROGRAMSESSION_H

#include <QObject>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>

#include "firmwareimage.h"
#include "tagclass.h"

class ProgrammerCli;

/**
 * @struct  SessionResult
 * @brief   How a session ended, for the record and the operator.
 */
struct SessionResult
{
  bool programmed = false;  ///< The programmer was run (so a row is due), whatever followed.
  QString result;           ///< OK, TEST_FAILED, PROGRAM_FAILED: (reason), NO_ANSWER, MISMATCH, CANCELLED, or FAILED: (reason) before programming.
  QString testStatus;       ///< TestResult name, or empty if the tests did not run.
  QString uuid;             ///< The tag's UUID after programming, or empty.
  QString gitHash;          ///< The tag's git hash after programming, or empty.
  QString message;          ///< One sentence for the operator.

  /// What a retest's read-back of internal flash found.
  enum class Flash
  {
    NotChecked, ///< No image to compare with, or not a retest.
    Matches,    ///< Every loaded byte of the image is in the tag's flash.
    Differs,    ///< The tag's flash is not the image.
    Unread,     ///< The flash could not be read; see flashNote.
  };
  Flash flash = Flash::NotChecked; ///< Retest only.
  QString flashNote;               ///< Why the flash differs or was not read.
};

/**
 * @class   ProgramSession
 * @brief   Runs the program sequence for one tag.
 *
 * @details One session at a time; start() while running is ignored. Uses the
 *          caller's Tag, which the caller must not touch until finished(). The
 *          Tag is detached while the programmer runs, because both use the
 *          ST-LINK, and is left attached at the end when the tag answers.
 *          GUI thread only.
 */
class ProgramSession : public QObject
{
  Q_OBJECT

public:
  /// Steps reported through stepChanged().
  enum class Step
  {
    Stopping,     ///< Stopping a running tag.
    Erasing,      ///< The tag's own firmware is erasing its log.
    Programming,  ///< STM32_Programmer_CLI is erasing internal flash and writing the image.
    Reading,      ///< Retest: STM32_Programmer_CLI is reading internal flash back to compare.
    Connecting,   ///< Waiting for the new image to answer.
    PowerCycle,   ///< The new image does not answer; waiting for a power cycle and retryConnect().
    Testing,      ///< The self-tests are running.
    Done,         ///< Finished; see finished().
  };
  Q_ENUM(Step)

  /**
   * @brief Uses @p tag and @p cli for its sessions.
   * @param[in] tag     The station's tag connection.
   * @param[in] cli     The station's programmer.
   * @param[in] parent  Qt parent.
   */
  ProgramSession(Tag &tag, ProgrammerCli &cli, QObject *parent = nullptr);

  /// Whether a session is running.
  bool running() const { return running_; }
  /// The current step.
  Step step() const { return step_; }

  /**
   * @brief Programs @p image into the tag on the base @p serial.
   *
   * @param[in] image   A usable image.
   * @param[in] serial  The base's ST-LINK serial.
   * @param[in] attach  Attaches the Tag to the base (finding it again by
   *                    serial, since a replugged base has a new address);
   *                    returns whether the tag answered.
   * @pre   The tag, if it answers, is attached; a board that does not answer
   *        (blank, or foreign firmware) is programmed without the stop and
   *        erase.
   */
  void start(const FirmwareImage &image, const QString &serial, std::function<bool()> attach);

  /**
   * @brief Runs the self-tests again on the attached tag, without programming.
   * @details With a usable @p image and an IDLE tag, first reads back the
   *          internal flash the image's loaded bytes occupy and compares it
   *          (SessionResult::flash), so the record names an image only when
   *          the tag's flash is that image. A tag whose flash differs ends as
   *          MISMATCH once its tests have run.
   * @param[in] image   The image the tag is expected to run, or an empty one
   *                    to skip the comparison.
   * @param[in] serial  The base's ST-LINK serial.
   * @param[in] attach  As for start(), used if the tag is not attached.
   * @warning Reading the flash connects over SWD under reset, so the tag is
   *          reset and attached again before its tests.
   */
  void retest(const FirmwareImage &image, const QString &serial, std::function<bool()> attach);

  /// After a power cycle: tries to reach the new image again.
  void retryConnect();

  /**
   * @brief Stops the session.
   * @details Before the programmer runs this ends the session as CANCELLED,
   *          with nothing programmed. While it runs it is refused: an
   *          interrupted write is worse than any wait. At PowerCycle it ends
   *          the session as NO_ANSWER.
   * @return  Whether the session was stopped.
   */
  bool cancel();

signals:
  /// The session moved to @p step; @p text says what is happening.
  void stepChanged(ProgramSession::Step step, const QString &text);
  /// The tag's erase has done @p done of @p total sectors (@p total 0 when unknown).
  void eraseProgress(int done, int total);
  /// The session ended.
  void finished(const SessionResult &result);

private:
  /// One step of the sequence; rescheduled every second until Done.
  void tick();
  /// Enters @p step and reports it.
  void setStep(Step step, const QString &text);
  /// Runs the programmer; continues at Connecting or ends the session.
  void runProgrammer();
  /// Retest: reads the image's flash span back and compares; continues at Connecting.
  void readFlash();
  /// Reads TagInfo, sets the clock and starts the self-tests.
  void beginTests();
  /// Ends the session with @p result.
  void finish(const QString &result, const QString &message);
  /// Polls the tag's status; false when it does not answer.
  bool poll(Status &status);

  Tag &tag_;                       ///< The station's connection.
  ProgrammerCli &cli_;             ///< The station's programmer.
  QTimer timer_;                   ///< The one-second tick.
  std::function<bool()> attach_;   ///< Reattaches to the base.
  FirmwareImage image_;            ///< Image being programmed.
  QString serial_;                 ///< Base serial.
  Step step_ = Step::Done;         ///< Current step.
  bool running_ = false;           ///< A session is running.
  bool retestOnly_ = false;        ///< Testing only; no programming.
  bool mismatch_ = false;          ///< The tag's git hash is not the image's.
  int waitTicks_ = 0;              ///< Ticks spent in the current wait.
  SessionResult result_;           ///< Built up as the session runs.
  QTemporaryDir flashDir_;         ///< Where a retest's flash read-back is written.
};

#endif // PROGRAMSESSION_H
