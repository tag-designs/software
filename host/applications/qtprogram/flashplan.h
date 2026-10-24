/**
 * @file    flashplan.h
 * @brief   Decides how qtprogram erases internal flash when it programs an
 *          image, from the image's own linker symbols.
 *
 * @details The rule is the release procedure's
 *          (docs/release/release-procedure.md, "When a mass erase is needed")
 *          and embedded/tools/flash_release.py's:
 *
 *          - STM32U375 images (4 KB pages) pin every NOLOAD region, so a plain
 *            program keeps calibration, stored configuration and the NAND map.
 *          - STM32L432 images (2 KB pages) that reserve calibration pages
 *            (CompassTag) erase every page below the calibration table, which
 *            keeps calibration and clears the persistent region that moves
 *            with code size.
 *          - Other STM32L432 images mass erase.
 *
 *          The new image decides, because its layout governs from now on. A
 *          raw `.bin` carries no symbols, so only ELF images can be planned.
 */

#ifndef FLASHPLAN_H
#define FLASHPLAN_H

#include <QString>
#include <QStringList>

/**
 * @struct  FlashPlan
 * @brief   How one image is to be programmed, and what that keeps.
 */
struct FlashPlan
{
  /// Kind of internal-flash erase that accompanies the program.
  enum class Erase
  {
    Invalid,          ///< The image cannot be planned; see @ref error.
    None,             ///< Plain program: only the image's own pages are written.
    BelowCalibration, ///< Pages 0..@ref lastErasedPage, keeping calibration.
    All,              ///< Mass erase.
  };

  Erase erase = Erase::Invalid; ///< The erase that goes with the program.
  QString error;           ///< Why the image cannot be programmed, when Invalid.
  /// Invalid because the image records no flash layout (no
  /// `__tag_flash_page_size__`): an image built before the symbol existed.
  bool noLayout = false;
  int pageSize = 0;        ///< Flash page size the image was linked for, in bytes.
  int lastErasedPage = -1; ///< Highest page erased, for BelowCalibration.

  /// STM32_Programmer_CLI erase arguments, placed before `-d` in the same call.
  QStringList eraseArgs() const;
  /// One line naming the erase, e.g. "Erase pages 0-126, keeping calibration".
  QString summary() const;
  /// Whether the tag's calibration constants (if it has any) survive.
  bool keepsCalibration() const { return erase == Erase::None || erase == Erase::BelowCalibration; }
  /// Whether the tag's stored configuration survives the program itself.
  bool keepsConfiguration() const { return erase == Erase::None; }
};

/**
 * @brief   Plans the program of the ELF image at @p path.
 *
 * @param[in] path  Firmware ELF. A file that is not a 32-bit little-endian ELF,
 *                  or one without `__tag_flash_page_size__` (built before the
 *                  layout was published), yields an Invalid plan.
 * @return  The plan; check FlashPlan::erase for Invalid.
 */
FlashPlan planFlash(const QString &path);

#endif // FLASHPLAN_H
