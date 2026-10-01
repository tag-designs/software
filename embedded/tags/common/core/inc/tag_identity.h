/**
 * @file    tag_identity.h
 * @brief   Tag identity record: what this image is, and where its stored data
 *          lives, at a fixed address right after the interrupt vectors.
 *
 * @details A host that can read the tag's flash over SWD -- including one that
 *          cannot get the firmware to talk -- identifies the MCU from
 *          DBGMCU_IDCODE, then reads this record at the address that MCU's
 *          vector table ends at (0x080001A0 on STM32L432, 0x08000240 on
 *          STM32U375). From it the host chooses the external-flash loader and
 *          the decoder that turns a capture into the tag's SQLite file, with no
 *          .map file, ELF or source checkout.
 *
 *          The record is a sequence of entries {u16 id; u16 length; value},
 *          after a fixed header and ending with TAG_ID_END. Every length is a
 *          multiple of 4 and every value 4-byte aligned. A reader skips ids it
 *          does not know, so entries can be added without breaking older
 *          readers; TAG_IDENTITY_FORMAT_VERSION changes only if an existing
 *          entry changes meaning. All values are little-endian.
 *
 *          The record is built in tag_identity.c from the firmware's own
 *          symbols, sizeof and offsetof, so it cannot drift from the image.
 *          Family-specific facts (decoder, page geometry, scale factors) come
 *          from tag_identity_family.h, which a family or tag overrides by
 *          basename.
 *
 * @see     design/offline-log-reconstruction.md, item 4 of "Decisions and plan"
 * @see     embedded/tags/design/next-release-todo.md, B1
 */

#ifndef TAG_IDENTITY_H
#define TAG_IDENTITY_H

#include <stdint.h>

/** @brief Record magic, "TGID" as little-endian bytes 'T','G','I','D'. */
#define TAG_IDENTITY_MAGIC 0x44494754U

/** @brief Format version; changes only if an existing entry changes meaning. */
#define TAG_IDENTITY_FORMAT_VERSION 1U

/**
 * @name Entry identifiers
 * String entries hold a NUL-terminated string padded with NULs to a multiple
 * of 4 bytes. The other entries hold the struct named beside them.
 * @{
 */
#define TAG_ID_END              0xFFFFU ///< End of the record; length 0.

#define TAG_ID_TARGET           0x0101U ///< Build target name, e.g. "PresTag".
#define TAG_ID_FAMILY           0x0102U ///< Firmware family, e.g. "PresTag".
#define TAG_ID_BOARD            0x0103U ///< Board directory name, e.g. "PresTagv3".
#define TAG_ID_BOARD_DESC       0x0104U ///< BOARD_NAME, as tag-info reports it.
#define TAG_ID_BOARD_REVISION   0x0105U ///< Board hardware revision; "" if unset.
#define TAG_ID_FIRMWARE         0x0106U ///< FIRMWARE_STRING.
#define TAG_ID_GIT_REPO         0x0107U ///< Repository URL.
#define TAG_ID_GIT_HASH         0x0108U ///< Short commit hash (tag-info githash).
#define TAG_ID_GIT_SHA          0x0109U ///< Full commit hash.
#define TAG_ID_GIT_DATE         0x010AU ///< Commit date (tag-info build_time).
#define TAG_ID_SOURCE_PATH      0x010BU ///< Repository-relative target path.
#define TAG_ID_FLASH_PART       0x010CU ///< External flash part, e.g. "AT25XE"; "" if none.
#define TAG_ID_RTC_PART         0x010DU ///< RTC part, e.g. "RV3028".
#define TAG_ID_LOADER           0x010EU ///< External-flash loader name, e.g. "AT25XE_PresTagv3".
#define TAG_ID_DECODER          0x010FU ///< Host decoder for the data log, e.g. "prestag".
#define TAG_ID_MONITOR_VERSION  0x0110U ///< Monitor protocol version string.

#define TAG_ID_NUMBERS          0x0201U ///< TagIdentityNumbers.

#define TAG_ID_REGION_IMAGE          0x0301U ///< TagIdentityRegion: the loaded image.
#define TAG_ID_REGION_PERSISTENT     0x0302U ///< TagIdentityRegion: erasable persistent region.
#define TAG_ID_REGION_STATE_LOG      0x0303U ///< TagIdentityRegion: sEpoch[] state markers.
#define TAG_ID_REGION_STORED_CONFIG  0x0304U ///< TagIdentityRegion: sconfig.
#define TAG_ID_REGION_DATA_HEADERS   0x0305U ///< TagIdentityRegion: vddHeader[] internal headers.
#define TAG_ID_REGION_CALIBRATION    0x0306U ///< TagIdentityRegion: calibration slots.
#define TAG_ID_REGION_NAND_MAP       0x0307U ///< TagIdentityRegion: NAND logical-block map.
#define TAG_ID_REGION_SCRATCHPAD     0x0308U ///< TagIdentityRegion: retained SRAM scratchpad.
#define TAG_ID_DEFAULT_CONFIG        0x0309U ///< TagIdentityBlob: nanopb default Config.

#define TAG_ID_BACKUP_STATE     0x0401U ///< TagIdentityBackupState.
#define TAG_ID_EXTERNAL_FLASH   0x0402U ///< TagIdentityExternalFlash.
#define TAG_ID_DATA_FORMAT      0x0403U ///< TagIdentityDataFormat.
#define TAG_ID_SCALES           0x0404U ///< float[TAG_IDENTITY_SCALE_SLOTS], decoder-defined order.
#define TAG_ID_BUILD            0x0501U ///< TagIdentityBuild.
#define TAG_ID_SESSION_FACTS    0x0601U ///< TagIdentitySessionFacts.
/** @} */

/** @brief Number of float slots in the TAG_ID_SCALES entry. */
#define TAG_IDENTITY_SCALE_SLOTS 8U

/**
 * @name Header-to-data mapping
 * Values of TagIdentityDataFormat::mapping: how internal header i finds its
 * external data.
 * @{
 */
#define TAG_IDENTITY_MAP_NONE        0U ///< No external data; records are internal only.
#define TAG_IDENTITY_MAP_STRIDE      1U ///< Header i owns external bytes [i*page_bytes, (i+1)*page_bytes).
#define TAG_IDENTITY_MAP_BLOCK_FIELD 2U ///< The header carries its external block index.
#define TAG_IDENTITY_MAP_CHECKPOINT  3U ///< Headers are checkpoints with logical/physical page cursors.
/** @} */

/**
 * @name Build flags
 * Bits of TagIdentityBuild::flags: compile-time options that change what an
 * image stores or retains, and so how a capture of it must be read.
 * @{
 */
#define TAG_IDENTITY_FLAG_SCRATCHPAD          (1U << 0) ///< TAG_SCRATCHPAD.
#define TAG_IDENTITY_FLAG_SCRATCHPAD_RING     (1U << 1) ///< TAG_SCRATCHPAD_RING.
#define TAG_IDENTITY_FLAG_RETAINED_RUN_DIAG   (1U << 2) ///< TAG_RETAINED_RUN_DIAGNOSTICS.
#define TAG_IDENTITY_FLAG_STORED_CONFIG_PAGE  (1U << 3) ///< sconfig has its own flash page.
/** @} */

/**
 * @struct  TagIdentityRegion
 * @brief   A stored-data region.
 *
 * @details `size` 0 means the region runs to the end of the persistent region
 *          (the data headers do: they are bounded by __persistent_end__, not by
 *          their declared array length). `record_count` 0 means "read until the
 *          first erased record". Absent regions are omitted from the record.
 */
typedef struct {
  uint32_t address;        ///< First byte.
  uint32_t size;           ///< Bytes; 0 = to the end of the persistent region.
  uint32_t record_size;    ///< Bytes per record; 0 if not record-structured.
  uint32_t record_count;   ///< Records; 0 = until the first erased record.
  uint32_t layout_version; ///< Version of the record layout in this region.
} TagIdentityRegion;

/**
 * @struct  TagIdentityBlob
 * @brief   A const blob whose length is a variable in flash, not a constant.
 */
typedef struct {
  uint32_t address;        ///< First byte.
  uint32_t length_address; ///< Address of the uint32_t holding its length.
} TagIdentityBlob;

/**
 * @struct  TagIdentityNumbers
 * @brief   Numeric facts the tag-info call reports, and validation limits.
 */
typedef struct {
  uint32_t tag_type;              ///< TagType enum value.
  float qtmonitor_min_version;    ///< Minimum qtmonitor version.
  float accel_constant;           ///< ACCEL_CONSTANT, or 0.
  float mag_constant;             ///< MAG_CONSTANT, or 0.
  uint32_t tag_state_max;         ///< _TagState_MAX: largest valid state.
  uint32_t state_event_max;       ///< _State_Event_MAX: largest valid reason.
} TagIdentityNumbers;

/**
 * @struct  TagIdentityBackupState
 * @brief   Where pState lives and which word holds what.
 *
 * @details BackupState is defined per family, so a host must not assume the
 *          common layout. Word indices count 32-bit backup registers from
 *          `base`.
 */
typedef struct {
  uint32_t base;                ///< RTC_BKP0R or TAMP_BKP0R.
  uint32_t word_count;          ///< sizeof(BackupState) / 4.
  uint32_t valid_magic;         ///< BACKUP_STATE_VALID_MAGIC.
  uint32_t word_valid;          ///< Index of BackupState.valid.
  uint32_t word_state;          ///< Index of BackupState.state.
  uint32_t word_pages;          ///< Index of BackupState.pages.
  uint32_t word_external_blocks;///< Index of BackupState.external_blocks.
  uint32_t word_reset_cause;    ///< Index of BackupState.resetCause.
} TagIdentityBackupState;

/**
 * @struct  TagIdentityExternalFlash
 * @brief   External flash geometry; all zero when the tag has none.
 */
typedef struct {
  uint32_t jedec_id;        ///< Manufacturer << 16 | device, as the driver checks it.
  uint32_t size;            ///< Bytes of data area.
  uint32_t program_page;    ///< Program-page size, bytes.
  uint32_t erase_unit;      ///< Smallest erase unit, bytes.
  uint32_t spare;           ///< Spare bytes per page (NAND), else 0.
} TagIdentityExternalFlash;

/**
 * @struct  TagIdentityDataFormat
 * @brief   Geometry of the tag's data log, for its decoder.
 */
typedef struct {
  uint32_t layout_version;     ///< Version of the data layout the decoder reads.
  uint32_t mapping;            ///< TAG_IDENTITY_MAP_*.
  uint32_t header_size;        ///< Bytes per internal header.
  uint32_t page_bytes;         ///< Bytes of external data per header or page.
  uint32_t samples_per_page;   ///< Sample records per page.
  uint32_t sample_bytes;       ///< Bytes per sample record.
  uint32_t sample_period_s;    ///< Nominal sample period, s; 0 = from the stored config.
  uint32_t subsecond_hz;       ///< Sub-second tick rate in headers; 0 if none.
} TagIdentityDataFormat;

/**
 * @struct  TagIdentityBuild
 * @brief   Build identity beyond the commit.
 */
typedef struct {
  uint32_t options_digest; ///< CRC32 (POSIX cksum) of the build's UDEFS.
  uint32_t flags;          ///< TAG_IDENTITY_FLAG_*.
} TagIdentityBuild;

/**
 * @struct  TagIdentitySessionFacts
 * @brief   Where the session facts sit inside the stored configuration.
 *
 * @details Present only for families whose t_storedconfig carries a
 *          t_sessionFacts (session_facts.h). Read them from
 *          stored_config.start + offset.
 */
typedef struct {
  uint32_t offset;   ///< Byte offset of `session` within sconfig.
  uint32_t size;     ///< sizeof(t_sessionFacts).
  uint32_t version;  ///< TAG_SESSION_FACTS_VERSION.
} TagIdentitySessionFacts;

/**
 * @struct  TagIdentityStrings
 * @brief   Pointers to the record's strings, for infoAck().
 *
 * @details The tag-info call reports these, so live and offline readers get
 *          the same values from the same bytes.
 */
typedef struct {
  const char *board_desc;      ///< BOARD_NAME.
  const char *firmware;        ///< FIRMWARE_STRING.
  const char *git_repo;        ///< Repository URL.
  const char *git_hash;        ///< Short commit hash.
  const char *git_date;        ///< Commit date.
  const char *source_path;     ///< Repository-relative target path.
  const char *monitor_version; ///< Monitor protocol version.
} TagIdentityStrings;

/** @brief The record's strings, for the tag-info call. */
extern const TagIdentityStrings tagIdentityStrings;

#endif /* TAG_IDENTITY_H */
