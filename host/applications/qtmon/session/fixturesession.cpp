/**
 * @file    fixturesession.cpp
 * @brief   FixtureSession, declared in fixturesession.h.
 */

#include "fixturesession.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <google/protobuf/util/json_util.h>

namespace {

/**
 * @brief Resolves a path given relative to the source tree.
 *
 * @details Screenshot commands usually run from the build tree, but fixtures
 *          live in the repository. An existing or absolute path is returned
 *          as it is; otherwise it is tried relative to TAG_DESIGNS_SOURCE_DIR.
 *
 * @return The path found, or @p path for the caller's diagnostics.
 */
QString resolveSourceRelativePath(const QString &path)
{
  const QFileInfo requested(path);
  if (requested.exists() || requested.isAbsolute())
    return requested.filePath();
  const QFileInfo sourceRelative(QDir(QStringLiteral(TAG_DESIGNS_SOURCE_DIR)).filePath(path));
  if (sourceRelative.exists())
    return sourceRelative.filePath();
  return requested.filePath();
}

/**
 * @brief Parses one fixture JSON object as a protobuf message.
 * @param[in]  object   The object, protobuf JSON.
 * @param[out] message  The message to fill.
 * @param[in]  context  What it is, for the warning on failure.
 * @return true when it parsed.
 */
template <typename Message>
bool parseJsonMessage(const QJsonObject &object, Message &message, const QString &context)
{
  const std::string text = QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString();
  const auto status = google::protobuf::util::JsonStringToMessage(text, &message);
  if (!status.ok()) {
    qWarning().noquote() << "Could not parse" << context
                         << QString::fromStdString(status.ToString());
    return false;
  }
  return true;
}

} // namespace

bool FixtureSession::load(const QString &path)
{
  const QString resolvedPath = resolveSourceRelativePath(path);
  QFile file(resolvedPath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qWarning() << "Unable to open qtmonitor fake fixture" << path << "resolved as"
               << resolvedPath;
    return false;
  }

  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    qWarning() << "Unable to parse qtmonitor fake fixture" << resolvedPath
               << parseError.errorString();
    return false;
  }

  const QJsonObject root = document.object();
  id_ = root.value("id").toString("fixture");
  voltage_ = static_cast<float>(root.value("voltage").toDouble(0.0));
  if (!parseJsonMessage(root.value("info").toObject(), info_, "fixture info"))
    return false;

  bool configLoaded = false;
  const QJsonObject configObject = root.value("config").toObject();
  if (configObject.contains("value")) {
    configLoaded =
        parseJsonMessage(configObject.value("value").toObject(), config_, "fixture config");
  } else if (configObject.contains("$ref")) {
    QFile configFile(resolveSourceRelativePath(configObject.value("$ref").toString()));
    if (!configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
      qWarning() << "Unable to open fixture config ref" << configFile.fileName();
      return false;
    }
    QJsonParseError configError;
    const QJsonDocument configDocument =
        QJsonDocument::fromJson(configFile.readAll(), &configError);
    if (configError.error != QJsonParseError::NoError || !configDocument.isObject()) {
      qWarning() << "Unable to parse fixture config ref" << configError.errorString();
      return false;
    }
    configLoaded = parseJsonMessage(configDocument.object(), config_, "fixture config ref");
  }
  if (!configLoaded) {
    qWarning() << "qtmonitor fake fixture has no usable config";
    return false;
  }

  statuses_.clear();
  const QJsonObject statuses = root.value("statuses").toObject();
  for (auto it = statuses.begin(); it != statuses.end(); ++it) {
    Status status;
    if (parseJsonMessage(it.value().toObject(), status,
                         QString("fixture status %1").arg(it.key())))
      statuses_.insert(it.key().toLower(), status);
  }
  if (statuses_.isEmpty()) {
    qWarning() << "qtmonitor fake fixture has no statuses";
    return false;
  }

  qInfo() << "Loaded qtmonitor fake fixture" << resolvedPath;
  return true;
}

void FixtureSession::showState(const QString &name)
{
  const QString key = name.toLower();
  Status status;
  if (statuses_.contains(key))
    status = statuses_.value(key);
  else if (statuses_.contains("idle"))
    status = statuses_.value("idle");
  else if (statuses_.contains("current"))
    status = statuses_.value("current");
  else
    status = statuses_.first();

  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  status.set_millis(now);
  if (key == "idle") {
    status.set_state(IDLE);
    status.set_internal_data_count(0);
    status.set_external_data_count(0);
  } else if (key == "running") {
    status.set_state(RUNNING);
    if (status.internal_data_count() == 0)
      status.set_internal_data_count(42);
    if (status.external_data_count() == 0)
      status.set_external_data_count(7);
  } else if (key == "finished") {
    status.set_state(FINISHED);
    if (status.internal_data_count() == 0)
      status.set_internal_data_count(128);
    if (status.external_data_count() == 0)
      status.set_external_data_count(512);
  }
  if (status.test_status() == TEST_UNSPECIFIED)
    status.set_test_status(ALL_PASSED);
  emit statusRead(status, voltage_, now);
}

void FixtureSession::findBases()
{
  emit basesFound({UsbDev()});
}

void FixtureSession::attach(const UsbDev &)
{
  emit attached(info_, config_);
}

void FixtureSession::detach()
{
  ignore("Detach");
}

void FixtureSession::ignore(const char *command) const
{
  qInfo() << "Ignoring" << command << "in qtmonitor fixture mode";
}

void FixtureSession::readConfig()
{
  ignore("Read");
}

void FixtureSession::start(const Config &)
{
  ignore("Start");
}

void FixtureSession::stop()
{
  ignore("Stop");
}

void FixtureSession::syncClock()
{
  ignore("Synchronize Clock");
}

void FixtureSession::selfTest()
{
  ignore("Self-Test");
}

void FixtureSession::erase()
{
  ignore("Erase");
}

void FixtureSession::download(const QString &, TagLogStorageFormat)
{
  ignore("Download");
  emit downloadFinished(true, QString());
}
