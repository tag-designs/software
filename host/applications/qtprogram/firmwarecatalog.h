/**
 * @file    firmwarecatalog.h
 * @brief   The published firmware releases qtprogram offers, and its local
 *          cache of their images.
 *
 * @details Lists the repository's published `fw-v*` releases through the
 *          GitHub REST API (the repository is public, so no token is needed,
 *          and drafts are not listed without one), downloads a release's
 *          `tag-firmware-<tag>.tar.gz` once, and keeps each target's ELF and
 *          build manifest under the cache directory. Extraction uses the
 *          system `tar` (bsdtar on macOS and Windows 10 and later, GNU tar on
 *          Linux) and names every member it extracts, after checking each name
 *          against the release layout, so nothing outside
 *          `firmware/<Target>/` is written. Each image is then verified
 *          against its manifest before the release counts as cached. Offline,
 *          the cached releases are still listed.
 *
 * @see     docs/release/release-procedure.md ("Release tags and what CI builds")
 */

#ifndef FIRMWARECATALOG_H
#define FIRMWARECATALOG_H

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QUrl>

#include <functional>

#include "firmwareimage.h"

class QNetworkAccessManager;

/**
 * @struct  FirmwareRelease
 * @brief   One published firmware release.
 */
struct FirmwareRelease
{
  QString tag;            ///< e.g. "fw-v0.7".
  QDateTime published;    ///< When it was published.
  bool prerelease = false; ///< Marked as a prerelease on GitHub.
  QUrl archiveUrl;        ///< The `tag-firmware-<tag>.tar.gz` asset.
  QUrl pageUrl;           ///< The release page, whose notes say what was qualified.
  bool cached = false;    ///< Its images are in the cache and verified.
};

/**
 * @class   FirmwareCatalog
 * @brief   Lists, downloads and caches firmware releases.
 *
 * @details All network and extraction work is asynchronous and reports
 *          through signals on the GUI thread. One fetch runs at a time.
 */
class FirmwareCatalog : public QObject
{
  Q_OBJECT

public:
  /**
   * @brief Uses @p cacheDir for downloaded releases and the last listing.
   * @param[in] cacheDir  Created if missing.
   * @param[in] parent    Qt parent.
   */
  explicit FirmwareCatalog(const QString &cacheDir, QObject *parent = nullptr);

  /// Asks GitHub for the releases; emits releasesChanged() or refreshFailed().
  void refresh();
  /// Whether a listing is in progress.
  bool refreshing() const { return refreshing_; }
  /// Why the last listing failed, or empty if it succeeded or none has run.
  QString refreshError() const { return refreshError_; }

  /// The releases known from the last listing and the cache, newest first.
  QList<FirmwareRelease> releases() const { return releases_; }
  /// The release named @p tag, or a default-constructed one.
  FirmwareRelease release(const QString &tag) const;

  /// Whether @p tag's images are cached and were verified when extracted.
  bool isCached(const QString &tag) const;

  /**
   * @brief Downloads and extracts @p tag's archive into the cache.
   * @details Emits fetched() or fetchFailed(). A cached release emits
   *          fetched() at once.
   */
  void fetch(const QString &tag);
  /// Whether a fetch is running.
  bool fetching() const { return !fetching_.isEmpty(); }

  /// The cached images of @p tag, one per target, sorted by target; each is loaded and verified again.
  QList<FirmwareImage> images(const QString &tag) const;
  /// The build targets cached for @p tag, sorted; empty when it is not cached.
  QStringList targets(const QString &tag) const;
  /// Where @p target's ELF of release @p tag is cached (whether or not it exists).
  QString imagePath(const QString &tag, const QString &target) const;

  /// Whether @p tag names a firmware release (`fw-v` then a digit).
  static bool isFirmwareTag(const QString &tag);
  /// Whether an archive member is one qtprogram extracts: `firmware/<T>/<T>.elf` or its manifest.
  static bool acceptableMember(const QString &name);
  /// Parses the API's release list: published `fw-v*` releases with an archive asset, newest first.
  static QList<FirmwareRelease> parseReleases(const QByteArray &json);

signals:
  /// The list changed, from the network or the cache.
  void releasesChanged();
  /// The listing failed; the cached releases are still listed.
  void refreshFailed(const QString &why);
  /// @p tag is cached and verified.
  void fetched(const QString &tag);
  /// @p tag could not be fetched; nothing of it was kept.
  void fetchFailed(const QString &tag, const QString &why);

private:
  /// Merges the cached releases into @p listed and stores the result.
  void setReleases(QList<FirmwareRelease> listed);
  /// Runs `tar` with @p args; @p done gets its exit code and output.
  void runTar(const QStringList &args, std::function<void(int, const QString &)> done);
  /// Extracts, verifies and installs the downloaded archive of fetching_.
  void extract(const QString &archive, const QString &staging);
  /// Ends the fetch of fetching_ with @p why, removing @p staging.
  void failFetch(const QString &staging, const QString &why);

  QString cacheDir_;                       ///< Root of the cache.
  QNetworkAccessManager *network_;         ///< For the API and downloads.
  QList<FirmwareRelease> releases_;        ///< Newest first.
  QString fetching_;                       ///< Release being fetched, or empty.
  bool refreshing_ = false;                ///< A listing is in progress.
  QString refreshError_;                   ///< Why the last listing failed.
};

#endif // FIRMWARECATALOG_H
