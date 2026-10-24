/**
 * @file    firmwareimage.cpp
 * @brief   Identity record, manifest verification and hashing for qtprogram's
 *          firmware images.
 */

#include "firmwareimage.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>

#include <cstring>
#include <vector>

#include "recovery/identityrecord.h"
#include "recovery/targetimage.h"

namespace
{

/// Every tag in this tree boots from the start of internal flash.
constexpr uint32_t kFlashBase = 0x08000000u;

/// Identity record offsets after the vectors: STM32L432, then STM32U375.
constexpr uint32_t kRecordOffsets[] = {0x1A0u, 0x240u};

/**
 * @brief   Checks @p image against the manifest at @p manifestPath, as
 *          flash_release.py's verify() does.
 * @param[in,out] image  Receives manifestVerified, manifestCommit, and an error
 *                       or warning.
 */
void verifyManifest(FirmwareImage &image, const QString &manifestPath)
{
  QFile file(manifestPath);
  if (!file.open(QIODevice::ReadOnly)) {
    image.error = QObject::tr("Cannot read %1.").arg(manifestPath);
    return;
  }
  const QJsonObject manifest = QJsonDocument::fromJson(file.readAll()).object();
  const QString name = QFileInfo(image.elfPath).fileName();
  const QJsonObject recorded =
      manifest.value("artifacts").toObject().value(name).toObject();
  if (recorded.isEmpty()) {
    image.error = QObject::tr("%1 records no hash for %2.")
                      .arg(QFileInfo(manifestPath).fileName(), name);
    return;
  }
  if (recorded.value("sha256").toString() != image.sha256) {
    image.error = QObject::tr("%1 does not match its manifest (recorded %2, actual %3). "
                              "This is not the image that was archived; do not "
                              "program it.")
                      .arg(name, recorded.value("sha256").toString(), image.sha256);
    return;
  }
  if (recorded.value("bytes").toInteger(-1) != image.bytes) {
    image.error = QObject::tr("%1 is %2 bytes; its manifest says %3.")
                      .arg(name)
                      .arg(image.bytes)
                      .arg(recorded.value("bytes").toInteger(-1));
    return;
  }
  image.manifestVerified = true;
  const QJsonObject source = manifest.value("source").toObject();
  image.manifestCommit = source.value("commit").toString();
  if (source.value("dirty").toBool())
    image.warning = QObject::tr("Built from a tree with uncommitted changes, so its "
                                "commit does not describe its sources.");
}

} // namespace

QString FirmwareImage::displayName() const
{
  const QString name = identity.found ? identity.target : QFileInfo(elfPath).fileName();
  return QStringLiteral("%1 (%2)").arg(name, version());
}

QString sha256OfFile(const QString &path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return QString();
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!hash.addData(&file))
    return QString();
  return QString::fromLatin1(hash.result().toHex());
}

ImageIdentity readImageIdentity(const QString &elfPath, QString *error)
{
  using namespace tagcore::recovery;
  ImageIdentity id;
  TargetImage elf;
  std::string why;
  if (!elf.Load(elfPath.toStdString(), &why)) {
    if (error)
      *error = QString::fromStdString(why);
    return id;
  }
  for (const uint32_t offset : kRecordOffsets) {
    uint8_t header[8];
    if (!elf.ReadAt(kFlashBase + offset, header, sizeof(header)) ||
        std::memcmp(header, "TGID", 4) != 0)
      continue;
    const uint32_t size = header[6] | (header[7] << 8);
    std::vector<uint8_t> flash(offset + size);
    if (!elf.ReadAt(kFlashBase, flash.data(), static_cast<uint32_t>(flash.size())))
      continue;
    const IdentityRecord record = ParseIdentityRecord(flash, offset);
    if (!record.found) {
      if (error)
        *error = QString::fromStdString(record.error);
      return id;
    }
    auto text = [&record](const char *name) {
      return QString::fromStdString(record.String(name));
    };
    id.found = true;
    id.target = text("target");
    id.board = text("board");
    id.boardDesc = text("board_desc");
    id.gitHash = text("git_hash");
    id.gitSha = text("git_sha");
    id.sourcePath = text("source_path");
    id.tagType = record.has_numbers ? static_cast<int>(record.tag_type) : 0;
    return id;
  }
  if (error)
    *error = QObject::tr("no tag identity record after the vectors");
  return id;
}

FirmwareImage loadFirmwareImage(const QString &elfPath, const QString &release)
{
  FirmwareImage image;
  image.elfPath = elfPath;
  image.release = release;
  const QFileInfo info(elfPath);
  if (!info.isFile()) {
    image.error = QObject::tr("%1 does not exist.").arg(elfPath);
    return image;
  }
  image.bytes = info.size();
  image.sha256 = sha256OfFile(elfPath);
  if (image.sha256.isEmpty()) {
    image.error = QObject::tr("Cannot read %1.").arg(elfPath);
    return image;
  }
  image.plan = planFlash(elfPath);
  if (image.plan.erase == FlashPlan::Erase::Invalid) {
    image.error = image.plan.error;
    return image;
  }
  image.identity = readImageIdentity(elfPath);

  const QString manifest =
      info.dir().filePath(info.completeBaseName() + "-build-manifest.json");
  if (QFileInfo::exists(manifest)) {
    image.manifestPath = manifest;
    verifyManifest(image, manifest);
  } else if (!release.isEmpty()) {
    image.error = QObject::tr("The release has no %1 beside %2, so the image "
                              "cannot be checked.")
                      .arg(QFileInfo(manifest).fileName(), info.fileName());
  }
  return image;
}
