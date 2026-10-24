/**
 * @file    firmwarecatalog.cpp
 * @brief   GitHub release listing, download, extraction and the image cache.
 */

#include "firmwarecatalog.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QVersionNumber>

#include <algorithm>

namespace
{

/// The repository whose releases are listed.
const char *kReleasesApi =
    "https://api.github.com/repos/tag-designs/software/releases?per_page=100";

/// Written into a release's cache directory once its images are verified.
const char *kVerifiedMarker = ".verified";

/// The last listing, kept so the releases can be shown offline.
const char *kListingFile = "releases.json";

/// Release name for ordering: "fw-v0.7" -> 0.7.
QVersionNumber versionOf(const QString &tag)
{
  return QVersionNumber::fromString(tag.mid(4));
}

/// Serialises releases for kListingFile.
QByteArray toJson(const QList<FirmwareRelease> &releases)
{
  QJsonArray array;
  for (const FirmwareRelease &r : releases)
    array.append(QJsonObject{{"tag", r.tag},
                             {"published", r.published.toString(Qt::ISODate)},
                             {"prerelease", r.prerelease},
                             {"archive", r.archiveUrl.toString()},
                             {"page", r.pageUrl.toString()},
                             {"qualified", QJsonArray::fromStringList(r.qualified)}});
  return QJsonDocument(array).toJson();
}

/// Reads kListingFile.
QList<FirmwareRelease> fromJson(const QByteArray &json)
{
  QList<FirmwareRelease> releases;
  for (const QJsonValue &v : QJsonDocument::fromJson(json).array()) {
    const QJsonObject o = v.toObject();
    FirmwareRelease r;
    r.tag = o.value("tag").toString();
    r.published = QDateTime::fromString(o.value("published").toString(), Qt::ISODate);
    r.prerelease = o.value("prerelease").toBool();
    r.archiveUrl = QUrl(o.value("archive").toString());
    r.pageUrl = QUrl(o.value("page").toString());
    // A listing written before qualification was recorded says nothing about it.
    r.listed = o.contains("qualified");
    for (const QJsonValue &t : o.value("qualified").toArray())
      r.qualified.append(t.toString());
    if (FirmwareCatalog::isFirmwareTag(r.tag))
      releases.append(r);
  }
  return releases;
}

} // namespace

FirmwareCatalog::FirmwareCatalog(const QString &cacheDir, QObject *parent)
    : QObject(parent), cacheDir_(cacheDir), network_(new QNetworkAccessManager(this))
{
  QDir().mkpath(cacheDir_);
  QFile listing(QDir(cacheDir_).filePath(kListingFile));
  setReleases(listing.open(QIODevice::ReadOnly) ? fromJson(listing.readAll())
                                                : QList<FirmwareRelease>());
}

bool FirmwareCatalog::isFirmwareTag(const QString &tag)
{
  static const QRegularExpression re(QStringLiteral("^fw-v\\d+(\\.\\d+)*$"));
  return re.match(tag).hasMatch();
}

bool FirmwareCatalog::acceptableMember(const QString &name)
{
  static const QRegularExpression re(
      QStringLiteral("^firmware/([A-Za-z0-9_-]+)/\\1(\\.elf|-build-manifest\\.json)$"));
  return re.match(name).hasMatch();
}

QString FirmwareCatalog::qualificationTarget(const QString &assetName)
{
  static const QRegularExpression re(
      QStringLiteral("^qualification-([A-Za-z0-9_]+)(-\\d{8})?\\.tar\\.gz$"));
  return re.match(assetName).captured(1);
}

QList<FirmwareRelease> FirmwareCatalog::parseReleases(const QByteArray &json)
{
  QList<FirmwareRelease> releases;
  for (const QJsonValue &v : QJsonDocument::fromJson(json).array()) {
    const QJsonObject o = v.toObject();
    const QString tag = o.value("tag_name").toString();
    if (o.value("draft").toBool() || !isFirmwareTag(tag))
      continue;
    FirmwareRelease r;
    r.tag = tag;
    r.published = QDateTime::fromString(o.value("published_at").toString(), Qt::ISODate);
    r.prerelease = o.value("prerelease").toBool();
    r.pageUrl = QUrl(o.value("html_url").toString());
    const QString archive = QStringLiteral("tag-firmware-%1.tar.gz").arg(tag);
    r.listed = true;
    for (const QJsonValue &a : o.value("assets").toArray()) {
      const QString name = a.toObject().value("name").toString();
      if (name == archive)
        r.archiveUrl = QUrl(a.toObject().value("browser_download_url").toString());
      const QString qualified = qualificationTarget(name);
      if (!qualified.isEmpty() && !r.qualified.contains(qualified))
        r.qualified.append(qualified);
    }
    if (r.archiveUrl.isValid() && !r.archiveUrl.isEmpty())
      releases.append(r);
  }
  std::sort(releases.begin(), releases.end(), [](const FirmwareRelease &a,
                                                 const FirmwareRelease &b) {
    return versionOf(a.tag) > versionOf(b.tag);
  });
  return releases;
}

void FirmwareCatalog::setReleases(QList<FirmwareRelease> listed)
{
  // A cached release stays offered even when the listing lacks it (offline,
  // or withdrawn later): its images were verified when they were extracted.
  for (const QString &tag : QDir(cacheDir_).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    if (!isCached(tag))
      continue;
    const bool known = std::any_of(listed.begin(), listed.end(),
                                   [&tag](const FirmwareRelease &r) { return r.tag == tag; });
    if (!known) {
      FirmwareRelease r;
      r.tag = tag;
      listed.append(r);
    }
  }
  for (FirmwareRelease &r : listed)
    r.cached = isCached(r.tag);
  std::sort(listed.begin(), listed.end(), [](const FirmwareRelease &a,
                                             const FirmwareRelease &b) {
    return versionOf(a.tag) > versionOf(b.tag);
  });
  releases_ = listed;
  emit releasesChanged();
}

FirmwareRelease FirmwareCatalog::release(const QString &tag) const
{
  for (const FirmwareRelease &r : releases_)
    if (r.tag == tag)
      return r;
  return FirmwareRelease();
}

bool FirmwareCatalog::isCached(const QString &tag) const
{
  return isFirmwareTag(tag) &&
         QFile::exists(QDir(QDir(cacheDir_).filePath(tag)).filePath(kVerifiedMarker));
}

void FirmwareCatalog::refresh()
{
  QNetworkRequest request{QUrl(QString::fromLatin1(kReleasesApi))};
  request.setRawHeader("Accept", "application/vnd.github+json");
  request.setRawHeader("User-Agent", "qtprogram");
  refreshing_ = true;
  QNetworkReply *reply = network_->get(request);
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    reply->deleteLater();
    refreshing_ = false;
    if (reply->error() != QNetworkReply::NoError) {
      const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      refreshError_ = status == 403 || status == 429
                          ? tr("GitHub refused the listing (rate limit); try again later.")
                          : reply->errorString();
      emit refreshFailed(refreshError_);
      emit releasesChanged();
      return;
    }
    refreshError_.clear();
    const QList<FirmwareRelease> listed = parseReleases(reply->readAll());
    QFile listing(QDir(cacheDir_).filePath(kListingFile));
    if (listing.open(QIODevice::WriteOnly))
      listing.write(toJson(listed));
    setReleases(listed);
  });
}

void FirmwareCatalog::fetch(const QString &tag)
{
  if (isCached(tag)) {
    emit fetched(tag);
    return;
  }
  const FirmwareRelease r = release(tag);
  if (fetching() || r.archiveUrl.isEmpty()) {
    emit fetchFailed(tag, fetching() ? tr("Another release is downloading.")
                                     : tr("%1 has no firmware archive.").arg(tag));
    return;
  }
  fetching_ = tag;
  const QString staging = QDir(cacheDir_).filePath(tag + ".partial");
  QDir(staging).removeRecursively();
  QDir().mkpath(staging);
  const QString archive = QDir(staging).filePath("archive.tar.gz");

  QNetworkRequest request(r.archiveUrl);
  request.setRawHeader("User-Agent", "qtprogram");
  QNetworkReply *reply = network_->get(request);
  qInfo().noquote() << "Downloading" << r.archiveUrl.toString();
  connect(reply, &QNetworkReply::finished, this, [this, reply, archive, staging]() {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
      failFetch(staging, reply->errorString());
      return;
    }
    QFile file(archive);
    if (!file.open(QIODevice::WriteOnly) || file.write(reply->readAll()) < 0) {
      failFetch(staging, tr("Cannot write %1.").arg(archive));
      return;
    }
    file.close();
    extract(archive, staging);
  });
}

void FirmwareCatalog::runTar(const QStringList &args,
                             std::function<void(int, const QString &)> done)
{
  auto *tar = new QProcess(this);
  tar->setProcessChannelMode(QProcess::MergedChannels);
  connect(tar, &QProcess::finished, this,
          [tar, done](int code, QProcess::ExitStatus status) {
            tar->deleteLater();
            done(status == QProcess::NormalExit ? code : -1,
                 QString::fromLocal8Bit(tar->readAll()));
          });
  connect(tar, &QProcess::errorOccurred, this, [tar, done](QProcess::ProcessError e) {
    if (e == QProcess::FailedToStart) {
      tar->deleteLater();
      done(-1, tr("Could not run tar."));
    }
  });
  tar->start(QStringLiteral("tar"), args);
}

void FirmwareCatalog::extract(const QString &archive, const QString &staging)
{
  runTar({"-tzf", archive}, [this, archive, staging](int code, const QString &listing) {
    if (code != 0) {
      failFetch(staging, tr("The archive could not be read: %1").arg(listing.trimmed()));
      return;
    }
    QStringList members;
    for (QString name : listing.split('\n', Qt::SkipEmptyParts)) {
      name = name.trimmed();
      if (name.startsWith("./"))
        name = name.mid(2);
      if (acceptableMember(name))
        members.append(name);
    }
    if (members.isEmpty()) {
      failFetch(staging, tr("The archive holds no firmware images."));
      return;
    }
    runTar(QStringList{"-xzf", archive, "-C", staging} + members,
           [this, archive, staging](int code, const QString &output) {
             if (code != 0) {
               failFetch(staging, tr("Extraction failed: %1").arg(output.trimmed()));
               return;
             }
             QFile::remove(archive);
             const QString tag = fetching_;
             // Verify every image against its manifest before the release
             // counts as cached; one bad image refuses the whole release.
             const QDir firmware(QDir(staging).filePath("firmware"));
             for (const QString &target : firmware.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
               const FirmwareImage image = loadFirmwareImage(
                   QDir(firmware.filePath(target)).filePath(target + ".elf"), tag);
               if (!image.usable()) {
                 failFetch(staging, image.error);
                 return;
               }
             }
             const QString final = QDir(cacheDir_).filePath(tag);
             QDir(final).removeRecursively();
             if (!QDir().rename(staging, final)) {
               failFetch(staging, tr("Cannot install the release in %1.").arg(final));
               return;
             }
             QFile marker(QDir(final).filePath(kVerifiedMarker));
             if (!marker.open(QIODevice::WriteOnly)) {
               failFetch(final, tr("Cannot install the release in %1.").arg(final));
               return;
             }
             marker.close();
             fetching_.clear();
             setReleases(releases_);
             emit fetched(tag);
           });
  });
}

void FirmwareCatalog::failFetch(const QString &staging, const QString &why)
{
  QDir(staging).removeRecursively();
  const QString tag = fetching_;
  fetching_.clear();
  qWarning().noquote() << "Fetching" << tag << "failed:" << why;
  emit fetchFailed(tag, why);
}

QStringList FirmwareCatalog::targets(const QString &tag) const
{
  QStringList out;
  if (!isCached(tag))
    return out;
  const QDir firmware(QDir(QDir(cacheDir_).filePath(tag)).filePath("firmware"));
  for (const QString &target : firmware.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
    if (QFileInfo::exists(imagePath(tag, target)))
      out.append(target);
  return out;
}

QString FirmwareCatalog::imagePath(const QString &tag, const QString &target) const
{
  return QDir(cacheDir_).filePath(QStringLiteral("%1/firmware/%2/%2.elf").arg(tag, target));
}

QList<FirmwareImage> FirmwareCatalog::images(const QString &tag) const
{
  QList<FirmwareImage> out;
  for (const QString &target : targets(tag))
    out.append(loadFirmwareImage(imagePath(tag, target), tag));
  return out;
}
