/**
 * @file    programmercli.cpp
 * @brief   STM32_Programmer_CLI lookup, invocation and output parsing.
 */

#include "programmercli.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>

namespace
{

#ifdef Q_OS_WIN
const QString kCli = QStringLiteral("STM32_Programmer_CLI.exe");
#else
const QString kCli = QStringLiteral("STM32_Programmer_CLI");
#endif

/// Whether @p path is an executable file.
bool usable(const QString &path)
{
  const QFileInfo info(path);
  return !path.isEmpty() && info.isFile() && info.isExecutable();
}

/// The connection arguments every target call starts with.
QStringList connectArgs(const QString &serial)
{
  return {"-c", "port=SWD", "sn=" + serial, "mode=UR"};
}

} // namespace

QString stripAnsi(const QString &text)
{
  static const QRegularExpression ansi(QStringLiteral("\x1b\\[[0-9;]*m"));
  QString out = text;
  return out.remove(ansi);
}

QString parseProgrammerVersion(const QString &output)
{
  static const QRegularExpression re(QStringLiteral("STM32CubeProgrammer v(\\d+(?:\\.\\d+)+)"));
  const auto m = re.match(stripAnsi(output));
  return m.hasMatch() ? m.captured(1) : QString();
}

QList<ProbeInfo> parseProbeList(const QString &output)
{
  static const QRegularExpression sn(QStringLiteral("^\\s*ST-LINK SN\\s*:\\s*(\\S+)"));
  static const QRegularExpression fw(QStringLiteral("^\\s*ST-LINK FW\\s*:\\s*(\\S+)"));
  QList<ProbeInfo> probes;
  for (const QString &line : stripAnsi(output).split('\n')) {
    auto m = sn.match(line);
    if (m.hasMatch()) {
      probes.append({m.captured(1), QString()});
      continue;
    }
    m = fw.match(line);
    if (m.hasMatch() && !probes.isEmpty())
      probes.last().firmware = m.captured(1);
  }
  return probes;
}

ConnectInfo parseConnect(const QString &output, int exitCode)
{
  static const QRegularExpression id(QStringLiteral("^\\s*Device ID\\s*:\\s*0x([0-9A-Fa-f]+)"));
  static const QRegularExpression name(QStringLiteral("^\\s*Device name\\s*:\\s*(.+?)\\s*$"));
  static const QRegularExpression volts(QStringLiteral("^\\s*Voltage\\s*:\\s*([0-9.]+)\\s*V"));
  static const QRegularExpression err(QStringLiteral("^\\s*Error:\\s*(.+?)\\s*$"));
  ConnectInfo info;
  for (const QString &line : stripAnsi(output).split('\n')) {
    if (auto m = id.match(line); m.hasMatch())
      info.deviceId = m.captured(1).toUInt(nullptr, 16);
    else if (auto m = name.match(line); m.hasMatch())
      info.deviceName = m.captured(1);
    else if (auto m = volts.match(line); m.hasMatch())
      info.voltage = m.captured(1).toDouble();
    else if (auto m = err.match(line); m.hasMatch() && info.error.isEmpty())
      info.error = m.captured(1);
  }
  info.ok = exitCode == 0 && info.deviceId != 0;
  if (!info.ok && info.error.isEmpty())
    info.error = exitCode == 0 ? QObject::tr("the programmer reported no device")
                               : QObject::tr("STM32_Programmer_CLI exited with %1").arg(exitCode);
  return info;
}

QString findProgrammer(const QString &saved)
{
  if (usable(saved))
    return saved;
  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  if (usable(env.value("STM32CUBEPROGRAMMER")))
    return env.value("STM32CUBEPROGRAMMER");
  if (env.contains("STM32_PRG_PATH") && usable(QDir(env.value("STM32_PRG_PATH")).filePath(kCli)))
    return QDir(env.value("STM32_PRG_PATH")).filePath(kCli);
  const QString onPath = QStandardPaths::findExecutable(kCli);
  if (!onPath.isEmpty())
    return onPath;
  QStringList roots;
#if defined(Q_OS_MACOS)
  const QString bundle =
      "STM32CubeProgrammer/STM32CubeProgrammer.app/Contents/Resources/bin";
  roots << QDir::home().filePath("Software/" + bundle)
        << "/Applications/STMicroelectronics/" + bundle;
#elif defined(Q_OS_WIN)
  roots << "C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin";
#else
  roots << QDir::home().filePath("Software/STM32CubeProgrammer/bin")
        << "/opt/st/stm32cubeprog/bin";
#endif
  for (const QString &root : roots)
    if (usable(QDir(root).filePath(kCli)))
      return QDir(root).filePath(kCli);
  return QString();
}

QString programmerInDirectory(const QString &dir)
{
  const QDir d(dir);
  for (const QString &rel : {QStringLiteral("."), QStringLiteral("bin"),
                             QStringLiteral("Contents/Resources/bin"),
                             QStringLiteral("STM32CubeProgrammer.app/Contents/Resources/bin")}) {
    const QString candidate = QDir(d.filePath(rel)).filePath(kCli);
    if (usable(candidate))
      return QDir::cleanPath(candidate);
  }
  return QString();
}

ProgrammerCli::ProgrammerCli(QObject *parent) : QObject(parent) {}

void ProgrammerCli::run(const QStringList &args, Done done)
{
  if (process_ || path_.isEmpty()) {
    done(-1, process_ ? tr("STM32_Programmer_CLI is already running.")
                      : tr("STM32_Programmer_CLI was not found."));
    return;
  }
  output_.clear();
  partial_.clear();
  process_ = new QProcess(this);
  process_->setProcessChannelMode(QProcess::MergedChannels);
  connect(process_, &QProcess::readyReadStandardOutput, this, &ProgrammerCli::readOutput);
  auto finish = [this, done](int code) {
    readOutput();
    if (!partial_.isEmpty())
      emit outputLine(partial_);
    QProcess *p = process_;
    process_ = nullptr;
    p->deleteLater();
    done(code, output_);
  };
  connect(process_, &QProcess::finished, this,
          [finish](int code, QProcess::ExitStatus status) {
            finish(status == QProcess::NormalExit ? code : -1);
          });
  connect(process_, &QProcess::errorOccurred, this, [this, finish](QProcess::ProcessError e) {
    if (e == QProcess::FailedToStart) {
      output_ += tr("Could not start %1.").arg(path_);
      finish(-1);
    }
  });
  qInfo().noquote() << "Running:" << path_ << args.join(' ');
  process_->start(path_, args);
}

void ProgrammerCli::readOutput()
{
  if (!process_)
    return;
  const QString text = stripAnsi(QString::fromLocal8Bit(process_->readAllStandardOutput()));
  output_ += text;
  partial_ += text;
  const qsizetype end = partial_.lastIndexOf('\n');
  if (end < 0)
    return;
  for (const QString &line : partial_.left(end).split('\n'))
    if (!line.trimmed().isEmpty())
      emit outputLine(line.trimmed());
  partial_ = partial_.mid(end + 1);
}

void ProgrammerCli::connectCheck(const QString &serial, Done done)
{
  run(connectArgs(serial), std::move(done));
}

void ProgrammerCli::program(const QString &serial, const QString &elf,
                            const QStringList &eraseArgs, Done done)
{
  run(connectArgs(serial) + eraseArgs + QStringList{"-d", elf, "-q", "-g", kLoadAddress},
      std::move(done));
}
