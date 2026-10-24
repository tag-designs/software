/**
 * @file    programmercli.h
 * @brief   Running STMicroelectronics' STM32_Programmer_CLI from qtprogram,
 *          and reading what it prints.
 *
 * @details qtprogram drives the command-line tool rather than the
 *          STM32CubeProgrammer API, which its licence keeps us from linking.
 *          Every call names the ST-LINK by serial (`sn=`), so a second base on
 *          the same host is never programmed by mistake. The parsers are free
 *          functions so they can be checked against captured output with no
 *          programmer installed.
 */

#ifndef PROGRAMMERCLI_H
#define PROGRAMMERCLI_H

#include <QList>
#include <QObject>
#include <QProcess>
#include <QStringList>

#include <functional>

/// Load and start address: every tag in this tree boots from the start of internal flash.
inline constexpr const char *kLoadAddress = "0x08000000";

/// DBGMCU device ID of the STM32L432 (and L43x/L44x).
inline constexpr quint32 kDeviceIdL432 = 0x435;
/// DBGMCU device ID of the STM32U375 (and U3 series).
inline constexpr quint32 kDeviceIdU375 = 0x454;

/**
 * @struct  ProbeInfo
 * @brief   One ST-LINK listed by `STM32_Programmer_CLI -l st-link`.
 */
struct ProbeInfo
{
  QString serial;    ///< `ST-LINK SN`, the value `sn=` takes.
  QString firmware;  ///< `ST-LINK FW`, e.g. "V2J37S7".
};

/**
 * @struct  ConnectInfo
 * @brief   What an SWD connect (`-c port=SWD sn=... mode=UR`) reported.
 */
struct ConnectInfo
{
  bool ok = false;        ///< The tool exited 0 and reported a device ID.
  QString error;          ///< The tool's first `Error:` line, or a summary, when !ok.
  quint32 deviceId = 0;   ///< DBGMCU device ID, e.g. 0x435; 0 if not reported.
  QString deviceName;     ///< e.g. "STM32L43xxx/STM32L44xxx".
  double voltage = -1.0;  ///< Target voltage the ST-LINK measured, volts; < 0 if not reported.
};

/**
 * @brief   Removes the ANSI colour escapes STM32_Programmer_CLI writes.
 * @param[in] text  Raw output.
 * @return  @p text without `ESC [ ... m` sequences.
 */
QString stripAnsi(const QString &text);

/**
 * @brief   Reads the version from `STM32_Programmer_CLI --version` output.
 * @param[in] output  The tool's output, escapes already stripped or not.
 * @return  e.g. "2.22.0", or an empty string when none is printed.
 */
QString parseProgrammerVersion(const QString &output);

/**
 * @brief   Reads the probe list from `STM32_Programmer_CLI -l st-link` output.
 * @param[in] output  The tool's output.
 * @return  One entry per `ST-LINK SN` line, in the order listed.
 */
QList<ProbeInfo> parseProbeList(const QString &output);

/**
 * @brief   Reads an SWD connect's result.
 * @param[in] output    The tool's output.
 * @param[in] exitCode  Its exit code; non-zero is a failure whatever was printed.
 * @return  The device ID, name and target voltage, or why the connect failed.
 */
ConnectInfo parseConnect(const QString &output, int exitCode);

/**
 * @brief   Finds STM32_Programmer_CLI.
 *
 * @details Tries @p saved, then STM32CUBEPROGRAMMER (the tool's path),
 *          STM32_PRG_PATH (its directory), the PATH, and the usual install
 *          directories, as embedded/tools/flash_release.py does.
 *
 * @param[in] saved  A path chosen earlier, or empty.
 * @return  The tool's path, or an empty string when none was found.
 */
QString findProgrammer(const QString &saved);

/**
 * @brief   The tool's path inside a directory the user chose.
 * @details Accepts the `bin` directory, the STM32CubeProgrammer install
 *          directory, or (on macOS) the application bundle.
 * @param[in] dir  The chosen directory.
 * @return  The tool's path, or an empty string when it is not there.
 */
QString programmerInDirectory(const QString &dir);

/**
 * @class   ProgrammerCli
 * @brief   Runs STM32_Programmer_CLI asynchronously, one call at a time.
 *
 * @details Each call starts the tool and returns at once; the callback runs on
 *          the GUI thread when the tool exits. The tool's output is also
 *          emitted line by line for the log pane. A call while another is
 *          running fails at once with exit code -1. The ST-LINK must not be
 *          held by tagcore during a call: detach the Tag first.
 */
class ProgrammerCli : public QObject
{
  Q_OBJECT

public:
  /// Receives the exit code (-1 if the tool could not run or crashed) and its output, escapes stripped.
  using Done = std::function<void(int exitCode, const QString &output)>;

  /// Constructs an idle runner with no tool path.
  explicit ProgrammerCli(QObject *parent = nullptr);

  /// Sets the tool's path.
  void setPath(const QString &path) { path_ = path; }
  /// The tool's path; empty until set.
  QString path() const { return path_; }
  /// Whether a call is running.
  bool busy() const { return process_ != nullptr; }

  /**
   * @brief Runs the tool with @p args.
   * @param[in] args  Arguments.
   * @param[in] done  Called once, when the tool exits or fails to start.
   */
  void run(const QStringList &args, Done done);

  /// `--version`.
  void version(Done done) { run({"--version"}, std::move(done)); }
  /// `-l st-link`: lists probes without connecting to a target.
  void listProbes(Done done) { run({"-l", "st-link"}, std::move(done)); }

  /**
   * @brief Connects over SWD under reset and disconnects; reads the device ID
   *        and the target voltage.
   * @param[in] serial  ST-LINK serial.
   * @param[in] done    Completion; parse with parseConnect().
   * @warning Resets the tag.
   */
  void connectCheck(const QString &serial, Done done);

  /**
   * @brief Erases as @p eraseArgs say, writes @p elf and starts it, in one
   *        invocation.
   * @details One invocation, because two leave a reset with the flash empty,
   *          which latches FLASH_SR.PEMPTY (release procedure).
   * @param[in] serial     ST-LINK serial.
   * @param[in] elf        Image to write.
   * @param[in] eraseArgs  FlashPlan::eraseArgs().
   * @param[in] done       Completion; exit code 0 is success.
   */
  void program(const QString &serial, const QString &elf, const QStringList &eraseArgs,
               Done done);

  /**
   * @brief Reads @p size bytes of memory from @p address into @p file
   *        (`-u`), connecting under reset as connectCheck() does.
   * @param[in] serial   ST-LINK serial.
   * @param[in] address  First byte.
   * @param[in] size     Bytes to read.
   * @param[in] file     Destination; a `.bin` path.
   * @param[in] done     Completion; exit code 0 is success.
   * @warning Resets the tag, as every SWD connection here does.
   */
  void readMemory(const QString &serial, quint32 address, quint32 size, const QString &file,
                  Done done);

signals:
  /// One line of the running tool's output, escapes stripped.
  void outputLine(const QString &line);

private:
  /// Emits complete lines of the process's output and keeps them for the callback.
  void readOutput();

  QString path_;                 ///< The tool.
  QProcess *process_ = nullptr;  ///< The running call, or null.
  QString output_;               ///< Output so far of the running call.
  QString partial_;              ///< An unfinished last line, held until it ends.
};

#endif // PROGRAMMERCLI_H
