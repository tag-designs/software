/**
 * @file    firmwareimage.h
 * @brief   One firmware ELF as qtprogram programs it: what it is for, whether
 *          its bytes are the archived ones, and how it is to be flashed.
 *
 * @details An image describes itself through the tag identity record compiled
 *          after its interrupt vectors (embedded/tags/common/core/inc/
 *          tag_identity.h): build target, board, TagType and git hash. That
 *          record is what tag-info reports from a running tag, so the same
 *          fields identify the attached tag and the image meant for it, with
 *          no table to maintain. A `<Target>-build-manifest.json` beside the
 *          image, as a release carries, adds the SHA-256 the image must match,
 *          as embedded/tools/flash_release.py checks it.
 *
 * @see     design/batch-station.md, docs/release/release-procedure.md
 */

#ifndef FIRMWAREIMAGE_H
#define FIRMWAREIMAGE_H

#include <QString>

#include "flashplan.h"

/**
 * @struct  ImageIdentity
 * @brief   The fields of an image's tag identity record that say which tag it
 *          is for.
 */
struct ImageIdentity
{
  bool found = false;   ///< The image carries a well-formed identity record.
  QString target;       ///< Build target, e.g. "BitTag"; names a release directory.
  QString board;        ///< Board directory, e.g. "BitTagv6".
  QString boardDesc;    ///< BOARD_NAME, as TagInfo.board_desc reports it.
  QString gitHash;      ///< Short commit, as TagInfo.githash reports it.
  QString gitSha;       ///< Full commit.
  QString sourcePath;   ///< e.g. "/embedded/tags/BitTag", as TagInfo.source_path.
  int tagType = 0;      ///< TagType value (proto/tagdata.proto).
};

/**
 * @struct  FirmwareImage
 * @brief   An ELF chosen for programming, checked as far as it can be.
 *
 * @details Built by loadFirmwareImage(). An image with a non-empty @ref error
 *          is never programmed; @ref warning is shown but does not block.
 */
struct FirmwareImage
{
  QString elfPath;        ///< The image.
  QString release;        ///< Release tag, e.g. "fw-v0.7"; empty for a file chosen by hand.
  QString manifestPath;   ///< `<stem>-build-manifest.json` beside the image, or empty.
  QString sha256;         ///< SHA-256 of the ELF, computed when loaded; lowercase hex.
  qint64 bytes = 0;       ///< Size of the ELF.
  bool manifestVerified = false; ///< The hash and size match the manifest's record.
  QString manifestCommit; ///< The manifest's source commit, or empty.
  QString error;          ///< Why the image must not be programmed; empty when usable.
  QString warning;        ///< A concern to show, e.g. a dirty source tree.
  ImageIdentity identity; ///< What the image says it is for.
  FlashPlan plan;         ///< The internal-flash erase that goes with it.

  /// Whether the image may be programmed: one was loaded, and nothing refused it.
  bool usable() const { return !elfPath.isEmpty() && error.isEmpty(); }
  /// The CSV's firmware_version: the release tag, or "custom".
  QString version() const { return release.isEmpty() ? QStringLiteral("custom") : release; }
  /// "BitTag (fw-v0.7)" or "BitTag.elf (custom)", for lists and messages.
  QString displayName() const;
};

/**
 * @brief   Loads, hashes and checks one firmware ELF.
 *
 * @details Reads the identity record, plans the erase with planFlash(), and
 *          hashes the file. When a manifest is beside the image its recorded
 *          SHA-256 and size must match, or the image is refused; a manifest
 *          that records a dirty tree gives a warning. For a @p release image
 *          the manifest is required. An image with no identity record is
 *          usable (images before the record existed lack one) but cannot be
 *          matched to a tag automatically.
 *
 * @param[in] elfPath  The image.
 * @param[in] release  Release tag it came from, or empty for a file chosen by hand.
 * @return  The image; check FirmwareImage::usable().
 */
FirmwareImage loadFirmwareImage(const QString &elfPath, const QString &release = QString());

/**
 * @brief   Reads the tag identity record from an ELF's loaded flash image.
 *
 * @details Looks after the vectors at both offsets the targets use (0x1A0 on
 *          STM32L432, 0x240 on STM32U375), relative to 0x08000000, through
 *          tagcore's ELF reader and record parser.
 *
 * @param[in]  elfPath  Firmware ELF.
 * @param[out] error    Why no record was read; may be nullptr.
 * @return  The identity; ImageIdentity::found is false without a record.
 */
ImageIdentity readImageIdentity(const QString &elfPath, QString *error = nullptr);

/**
 * @brief   SHA-256 of a file.
 * @param[in] path  File to hash.
 * @return  Lowercase hex digest, or an empty string when the file cannot be read.
 */
QString sha256OfFile(const QString &path);

#endif // FIRMWAREIMAGE_H
