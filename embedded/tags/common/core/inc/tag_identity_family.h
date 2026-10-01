/**
 * @file    tag_identity_family.h
 * @brief   Family-specific facts for the tag identity record: defaults.
 *
 * @details tag_identity.c includes this header by basename, so a family's
 *          inc/tag_identity_family.h, or a tag's own, replaces this one,
 *          following the tree's usual override rule. This default defines
 *          nothing. A family that has not written its own still builds; its
 *          record says the decoder is unknown and gives no data geometry, and a
 *          host falls back to identifying the image another way.
 *
 *          The default for every macro, and everything derived from the build
 *          rather than the family, lives in tag_identity.c, after this header
 *          is included, so an override cannot lose it. That covers the
 *          external flash part, JEDEC ID and geometry (from the flash module's
 *          TAG_FLASH_* macros), the RTC part, and the loader name, which
 *          follows the loader naming convention PART_Board
 *          (embedded/loaders/README.md).
 *
 *          Macros a family header may define (tag_identity.c supplies each
 *          default):
 *          - TAG_IDENTITY_FAMILY: family name.
 *          - TAG_IDENTITY_DECODER: host decoder name (sqlitelog decoders).
 *          - TAG_IDENTITY_DATA_LAYOUT_VERSION: bump when the data layout changes.
 *          - TAG_IDENTITY_MAPPING: a TAG_IDENTITY_MAP_* value.
 *          - TAG_IDENTITY_DATA_HEADER_SIZE, TAG_IDENTITY_PAGE_BYTES,
 *            TAG_IDENTITY_SAMPLES_PER_PAGE, TAG_IDENTITY_SAMPLE_BYTES,
 *            TAG_IDENTITY_SAMPLE_PERIOD_S, TAG_IDENTITY_SUBSECOND_HZ.
 *          - TAG_IDENTITY_SCALES: up to TAG_IDENTITY_SCALE_SLOTS floats, in the
 *            order the family's decoder documents.
 *          - TAG_IDENTITY_CALIBRATION_SLOT_SIZE, _SLOT_COUNT: 0 if none.
 *          - TAG_IDENTITY_BOARD_REVISION: "" if unset.
 */

#ifndef TAG_IDENTITY_FAMILY_H
#define TAG_IDENTITY_FAMILY_H

/* The default family: nothing to declare. */

#endif /* TAG_IDENTITY_FAMILY_H */
