/**
 * @file    station.h
 * @brief   qtprogram's programming station: the start-up checks, the base
 *          and tag, the firmware choice, the record, and the program session.
 *
 * @details Station holds everything the window shows and does nothing with
 *          widgets; MainWindow renders it and asks the operator what Station
 *          cannot decide. The checks are those of
 *          design/batch-station.md ("Start-up checks"). The
 *          hardware checks (base, SWD, supply, tag, tag state) run in order,
 *          and one that fails stops those after it; the programmer, firmware
 *          and record checks stand alone. Settings persist through QSettings
 *          (organization `tag-designs`, application `qtprogram`).
 */

#ifndef STATION_H
#define STATION_H

#include <QObject>
#include <QTimer>

#include <array>

#include "firmwarecatalog.h"
#include "programmercli.h"
#include "programsession.h"
#include "recordlog.h"
#include "tagclass.h"

/**
 * @class   Station
 * @brief   The state and actions behind qtprogram's window.
 *
 * @details GUI thread only. Holds the one Tag connection; while a program
 *          session runs the session owns it and Station's status poll pauses.
 */
class Station : public QObject
{
  Q_OBJECT

public:
  /// The checks, in the order the station panel lists them.
  enum class Check
  {
    Programmer, ///< STM32_Programmer_CLI found and runs.
    Base,       ///< One base on USB, its serial known.
    Swd,        ///< The CLI connects over SWD and reads a device ID.
    Supply,     ///< Target voltage in range for the MCU.
    Tag,        ///< The tag's firmware answers tagcore.
    TagState,   ///< The tag is IDLE.
    Firmware,   ///< The chosen image is present, verified and fits the tag.
    Record,     ///< The record CSV is open and writable.
  };
  Q_ENUM(Check)
  /// Number of checks.
  static constexpr int kCheckCount = 8;

  /// A check's outcome, drawn as a status light.
  enum class Level
  {
    Pending, ///< Not run yet, or waiting on another check or the operator.
    Ok,      ///< Passed.
    Warn,    ///< Passed with a concern, or needs the operator; does not block Program by itself.
    Fail,    ///< Failed; blocks Program.
  };
  Q_ENUM(Level)

  /// One check's light and line.
  struct CheckStatus
  {
    Level level = Level::Pending; ///< The light.
    QString text;                 ///< One line: the result, or how to fix it.
  };

  /// Builds the station, restoring settings; nothing runs until begin().
  explicit Station(QObject *parent = nullptr);
  /// Detaches from the tag.
  ~Station() override;

  /**
   * @brief Runs the start-up checks.
   * @details Finds the programmer, the base, and, when attaching
   *          automatically is allowed, connects to the tag. Lists releases
   *          and restores the firmware and record.
   */
  void begin();

  /// The status of @p check.
  CheckStatus status(Check check) const { return checks_[static_cast<int>(check)]; }
  /// Whether a check is still running: an attach, the programmer, a listing or a download.
  bool busy() const
  {
    return attaching_ || cli_.busy() || catalog_.fetching() || catalog_.refreshing();
  }

  // Settings

  /// Whether the operator has accepted that the station resets any tag it finds.
  bool autoAttach() const;
  /// Records the operator's answer; true attaches now.
  void setAutoAttach(bool on);

  // Programmer

  /**
   * @brief Uses the STM32_Programmer_CLI in @p dir (the `bin` directory, the
   *        install directory or the application bundle), saving it if it runs.
   * @return false when the tool is not in @p dir.
   */
  bool setProgrammerDirectory(const QString &dir);

  // Base and tag

  /// The bases found by the last enumeration.
  const std::vector<UsbDev> &bases() const { return bases_; }
  /// Selects base @p index of bases(), saves its serial, and continues the checks.
  void chooseBase(int index);
  /// The selected base's ST-LINK serial, or empty.
  QString baseSerial() const { return serial_; }

  /// Connects to the tag: SWD check, supply check, tagcore attach and state.
  void attach();
  /// Detaches and clears the tag, for the next board.
  void nextTag();
  /// Asks a running tag to stop.
  void stopTag();
  /// Has a FINISHED or ABORTED tag's firmware erase its log.
  void eraseTag();
  /// Whether the tag's firmware answers.
  bool tagAnswers() const { return tag_.IsAttached() && haveInfo_; }
  /// The tag's identity, valid when tagAnswers().
  const TagInfo &tagInfo() const { return info_; }
  /// The tag's last polled status, valid when tagAnswers().
  const Status &tagStatus() const { return tagStatus_; }
  /// The build target the tag runs, from its source path, or empty.
  QString tagTarget() const;
  /// The DBGMCU device ID from the last SWD check, or 0.
  quint32 deviceId() const { return connect_.deviceId; }
  /// The target voltage from the last SWD check, or < 0.
  double voltage() const { return connect_.voltage; }

  // Firmware

  /// The release listing and image cache.
  FirmwareCatalog &catalog() { return catalog_; }
  /// Chooses release @p tag, downloading it if needed.
  void chooseRelease(const QString &tag);
  /// Chooses an ELF file instead of a release.
  void chooseFile(const QString &path);
  /// The chosen release, or empty when a file is chosen.
  QString chosenRelease() const { return release_; }
  /// The chosen file, or empty when a release is chosen.
  QString chosenFile() const { return file_; }
  /// ELF files chosen recently, newest first.
  QStringList recentFiles() const;
  /// The build targets the chosen release offers, or the chosen file's.
  QStringList targets() const;
  /// The chosen build target.
  QString target() const { return target_; }
  /// Whether target() was set from the attached tag rather than by hand.
  bool targetDetected() const { return targetDetected_; }
  /// Chooses the build target by hand.
  void chooseTarget(const QString &target);
  /// The image that Program would write; check usable().
  const FirmwareImage &image() const { return image_; }

  // Record

  /// The record.
  const RecordLog &record() const { return record_; }
  /// The path a new record is created at by default.
  static QString defaultRecordPath();
  /// Opens the record at @p path and saves it; false with @p error otherwise.
  bool openRecord(const QString &path, QString *error);
  /// Creates a record at @p path and saves it; false with @p error otherwise.
  bool createRecord(const QString &path, QString *error);
  /// Programmed and failed rows written in this session.
  int sessionOk() const { return sessionOk_; }
  /// Rows written in this session that were not OK.
  int sessionFailed() const { return sessionFailed_; }

  // Programming

  /// Whether Program is possible now; @p why says why not.
  bool canProgram(QString *why) const;
  /// The session.
  ProgramSession &session() { return session_; }
  /**
   * @brief Programs the tag and records the attempt under @p label.
   * @pre   canProgram().
   */
  void program(const QString &label);
  /// Runs the self-tests again on the tag and records them under @p label.
  void retest(const QString &label);
  /// The label of the last program or retest.
  QString lastLabel() const { return label_; }

  /// qtprogram's version, as recorded.
  static QString hostVersion();

signals:
  /// @p check changed.
  void checkChanged(Station::Check check);
  /// The tag's identity or status changed.
  void tagChanged();
  /// The firmware choice, target or image changed.
  void firmwareChanged();
  /// Several bases are on USB and none is the saved one; call chooseBase().
  void baseChoiceNeeded();
  /// No record is open; ask to create one or choose one.
  void recordNeeded();
  /// A program or retest ended; @p record is the row written (if @p written).
  void programFinished(const ProgramRecord &record, bool written, const QString &message);

private:
  /// Sets @p check and emits checkChanged().
  void set(Check check, Level level, const QString &text);
  /// Finds the programmer and checks its version.
  void checkProgrammer();
  /// Enumerates bases and selects one, or polls until one appears.
  void checkBase();
  /// Continues after a base is selected: attach, or wait for Attach.
  void baseReady();
  /// Re-enumerates and attaches the Tag to the base with serial_; for the session.
  bool reattach();
  /**
   * @brief Attaches tagcore after the SWD check and reads the tag's identity,
   *        retrying while the USB device is still held.
   * @param[in] tries  Attempts left, kAttachRetryMs apart.
   */
  void attachTag(int tries);
  /// Sets the Supply check from connect_.
  void checkSupply();
  /// Sets the TagState check from tagStatus_.
  void checkTagState();
  /// Sets the Firmware check from the choice, the image and the tag.
  void checkFirmware();
  /// Sets the Record check.
  void checkRecord();
  /// Loads image_ from the choice and target.
  void loadImage();
  /// Picks the target from the attached tag, when the choice offers it.
  void detectTarget();
  /// Polls the attached tag's status.
  void poll();
  /// Clears what is known about the tag.
  void forgetTag();
  /// Writes the row for a finished session.
  void writeRecord(const SessionResult &result);

  std::array<CheckStatus, kCheckCount> checks_; ///< Every check's status.
  /// The tag connection. Mutable because tagcore's IsAttached() is not const.
  mutable Tag tag_;
  ProgrammerCli cli_;           ///< STM32_Programmer_CLI.
  FirmwareCatalog catalog_;     ///< Releases and their cache.
  RecordLog record_;            ///< The record CSV.
  ProgramSession session_;      ///< The program sequence.
  QTimer basePoll_;             ///< Looks for a base every second while there is none.
  QTimer statusPoll_;           ///< Polls the attached tag, every 400 ms.
  std::vector<UsbDev> bases_;   ///< The last enumeration.
  UsbDev base_;                 ///< The selected base.
  bool haveBase_ = false;       ///< base_ is valid.
  QString serial_;              ///< The base's ST-LINK serial.
  ConnectInfo connect_;         ///< The last SWD check.
  TagInfo info_;                ///< The tag's identity.
  bool haveInfo_ = false;       ///< info_ is from the attached tag.
  bool attaching_ = false;      ///< attachTag() is retrying.
  Status tagStatus_;            ///< The tag's last status.
  int missedPolls_ = 0;         ///< Consecutive polls the tag did not answer.
  QString release_;             ///< Chosen release, or empty.
  QString file_;                ///< Chosen file, or empty.
  QString target_;              ///< Chosen build target.
  bool targetDetected_ = false; ///< target_ came from the tag.
  QString fetchError_;          ///< Why the chosen release could not be fetched.
  FirmwareImage image_;         ///< The image Program would write.
  FirmwareImage sessionImage_;  ///< The image of the running session.
  double sessionVoltage_ = -1;  ///< Voltage when the session started.
  QString label_;               ///< Label of the running or last session.
  bool sessionRetest_ = false;  ///< The running session is a retest.
  int sessionOk_ = 0;           ///< OK rows written this session.
  int sessionFailed_ = 0;       ///< Other rows written this session.
};

#endif // STATION_H
