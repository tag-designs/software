/**
 * @file    tag_identity.c
 * @brief   The tag identity record, placed right after the interrupt vectors.
 *
 * @details One const object, `tagIdentity`, in section `.tag_identity`, which
 *          the tag linker scripts place directly after `.vectors` and assert
 *          stays there. Its format is described in tag_identity.h. Every value
 *          comes from the firmware's own symbols, macros, sizeof and offsetof,
 *          so the record cannot disagree with the image it describes.
 *
 *          Region bounds are stored as start and end addresses rather than
 *          sizes: each address is a link-time constant a static initializer
 *          can hold, while the difference of two symbols is not.
 *
 *          Build-supplied macros (common make.mk): TAG_TARGET_NAME,
 *          TAG_BOARD_ID, TAG_BUILD_OPTIONS_DIGEST. Family facts come from
 *          tag_identity_family.h; this file supplies a default for each.
 *
 * @see     tag_identity.h, design/offline-log-reconstruction.md
 */

#include "hal.h"

/* Same order as persistent.c: the protobuf types first, then the family's. */
#include <tag.pb.h>
#include "config.h"
#include "custom.h"
#include "flash_internal.h"
#include "persistent.h"
#include "scratchpad.h"
#include "datalog.h"
#include "version.h"

#include "tag_identity.h"
#include "tag_identity_family.h"

#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Defaults for anything a family header does not define.                     */
/* -------------------------------------------------------------------------- */

#ifndef TAG_TARGET_NAME
/** @brief Build target name; set by make.mk from PROJECT. */
#define TAG_TARGET_NAME "unknown"
#endif
#ifndef TAG_BOARD_ID
/** @brief Board directory name; set by make.mk from BOARDINC. */
#define TAG_BOARD_ID "unknown"
#endif
#ifndef TAG_BUILD_OPTIONS_DIGEST
/** @brief CRC32 of the build's -D options; set by make.mk. */
#define TAG_BUILD_OPTIONS_DIGEST 0U
#endif

/*
 * Families whose persistent.h does not define the backup-state magic use 1U,
 * from the identical fallbacks in main.c, pwr.c and state_machine.c. The
 * record reports the value this image actually checks.
 */
#ifndef BACKUP_STATE_VALID_MAGIC
/** @brief Backup-state valid magic: the main.c fallback, used where the family does not define one. */
#define BACKUP_STATE_VALID_MAGIC 1U
#endif

#ifndef BOARD_NAME
/** @brief Board description reported by tag-info; "unknown" if custom.h omits it. */
#define BOARD_NAME "unknown"
#endif

#ifndef TAG_IDENTITY_FAMILY
/** @brief Firmware family name. */
#define TAG_IDENTITY_FAMILY "unknown"
#endif
#ifndef TAG_IDENTITY_DECODER
/** @brief Host sqlitelog decoder that reads this family's data log. */
#define TAG_IDENTITY_DECODER "unknown"
#endif
#ifndef TAG_IDENTITY_DATA_LAYOUT_VERSION
/** @brief Version of the data layout; bump when it changes. */
#define TAG_IDENTITY_DATA_LAYOUT_VERSION 0U
#endif
#ifndef TAG_IDENTITY_MAPPING
/** @brief How a header finds its external data (TAG_IDENTITY_MAP_*). */
#define TAG_IDENTITY_MAPPING TAG_IDENTITY_MAP_NONE
#endif
#ifndef TAG_IDENTITY_DATA_HEADER_SIZE
/** @brief Bytes per internal header. */
#define TAG_IDENTITY_DATA_HEADER_SIZE 0U
#endif
#ifndef TAG_IDENTITY_PAGE_BYTES
/** @brief Bytes of external data per header or page. */
#define TAG_IDENTITY_PAGE_BYTES 0U
#endif
#ifndef TAG_IDENTITY_SAMPLES_PER_PAGE
/** @brief Sample records per page. */
#define TAG_IDENTITY_SAMPLES_PER_PAGE 0U
#endif
#ifndef TAG_IDENTITY_SAMPLE_BYTES
/** @brief Bytes per sample record. */
#define TAG_IDENTITY_SAMPLE_BYTES 0U
#endif
#ifndef TAG_IDENTITY_SAMPLE_PERIOD_S
/** @brief Nominal sample period in seconds; 0 when it comes from the stored config. */
#define TAG_IDENTITY_SAMPLE_PERIOD_S 0U
#endif
#ifndef TAG_IDENTITY_SUBSECOND_HZ
/** @brief Sub-second tick rate of header timestamps; 0 if none. */
#define TAG_IDENTITY_SUBSECOND_HZ 0U
#endif
#ifndef TAG_IDENTITY_SCALES
/** @brief Scale factors, in the order this file documents. */
#define TAG_IDENTITY_SCALES 0.0f
#endif
#ifndef TAG_IDENTITY_CALIBRATION_SLOT_SIZE
/** @brief Bytes per calibration slot; sensors.c asserts it. */
#define TAG_IDENTITY_CALIBRATION_SLOT_SIZE 0U
#endif
#ifndef TAG_IDENTITY_CALIBRATION_SLOT_COUNT
/** @brief Calibration slots in the table; sensors.c asserts it. */
#define TAG_IDENTITY_CALIBRATION_SLOT_COUNT 0U
#endif
#ifndef TAG_IDENTITY_BOARD_REVISION
/** @brief Board hardware revision; "" if unset. */
#define TAG_IDENTITY_BOARD_REVISION ""
#endif

/* -------------------------------------------------------------------------- */
/* Facts derived from the build, which no family header can drop.             */
/* -------------------------------------------------------------------------- */

#if defined(TAG_FLASH_AT25XE) && TAG_FLASH_AT25XE
#include "at25xe_commands.h"
/** @brief External flash part name; "" if none. */
#define TAG_IDENTITY_FLASH_PART "AT25XE"
/** @brief External flash JEDEC ID, manufacturer << 16 | device. */
#define TAG_IDENTITY_FLASH_JEDEC                                                \
  (((uint32_t)AT25XE_JEDEC_MANUFACTURER << 16) | (uint32_t)AT25XE_JEDEC_DEVICE1)
/** @brief External flash data size in bytes. */
#define TAG_IDENTITY_FLASH_SIZE AT25XE_SIZE
/** @brief External flash program-page size in bytes. */
#define TAG_IDENTITY_FLASH_PROGRAM_PAGE 256U
/** @brief External flash smallest erase unit in bytes. */
#define TAG_IDENTITY_FLASH_ERASE_UNIT AT25XE_SECTOR_SIZE
/** @brief External flash spare bytes per page (NAND), else 0. */
#define TAG_IDENTITY_FLASH_SPARE 0U
#elif (defined(TAG_FLASH_GD5F2GM7RE) && TAG_FLASH_GD5F2GM7RE) ||              \
    (defined(TAG_FLASH_GD5F1GQ5RE) && TAG_FLASH_GD5F1GQ5RE)
/* Geometry defaults for parts whose module sets none (GD5F1GQ5RE). */
#include "storage_gd5f.h"
/* The ID defaults are private to gd5f.c; these mirror them. */
#ifndef GD5F_ID_MANUFACTURER
#define GD5F_ID_MANUFACTURER 0xC8U
#endif
#ifndef GD5F_ID_DEVICE
#define GD5F_ID_DEVICE 0x41U
#endif
#if defined(TAG_FLASH_GD5F2GM7RE) && TAG_FLASH_GD5F2GM7RE
/** @brief External flash part name; "" if none. */
#define TAG_IDENTITY_FLASH_PART "GD5F2GM7RE"
#else
/** @brief External flash part name; "" if none. */
#define TAG_IDENTITY_FLASH_PART "GD5F1GQ5RE"
#endif
/** @brief External flash JEDEC ID, manufacturer << 16 | device. */
#define TAG_IDENTITY_FLASH_JEDEC                                                \
  (((uint32_t)GD5F_ID_MANUFACTURER << 16) | (uint32_t)GD5F_ID_DEVICE)
/** @brief External flash data size in bytes. */
#define TAG_IDENTITY_FLASH_SIZE                                                 \
  (GD5F_PHYSICAL_BLOCK_COUNT * GD5F_PAGES_PER_BLOCK * GD5F_PAGE_SIZE)
/** @brief External flash program-page size in bytes. */
#define TAG_IDENTITY_FLASH_PROGRAM_PAGE GD5F_PAGE_SIZE
/** @brief External flash smallest erase unit in bytes. */
#define TAG_IDENTITY_FLASH_ERASE_UNIT (GD5F_PAGES_PER_BLOCK * GD5F_PAGE_SIZE)
/** @brief External flash spare bytes per page (NAND), else 0. */
#define TAG_IDENTITY_FLASH_SPARE GD5F_SPARE_SIZE
/** @brief This image keeps a NAND logical-block map. */
#define TAG_IDENTITY_HAS_NAND_MAP 1
#elif defined(TAG_FLASH_MX25R) && TAG_FLASH_MX25R
/** @brief External flash part name; "" if none. */
#define TAG_IDENTITY_FLASH_PART "MX25R"
#elif defined(TAG_FLASH_MX25L) && TAG_FLASH_MX25L
/** @brief External flash part name; "" if none. */
#define TAG_IDENTITY_FLASH_PART "MX25L"
#elif defined(TAG_FLASH_MX25U12843) && TAG_FLASH_MX25U12843
/** @brief External flash part name; "" if none. */
#define TAG_IDENTITY_FLASH_PART "MX25U12843"
#else
/** @brief External flash part name; "" if none. */
#define TAG_IDENTITY_FLASH_PART ""
#endif

/* Parts without a geometry description here report their name only. */
#ifndef TAG_IDENTITY_FLASH_JEDEC
/** @brief External flash JEDEC ID, manufacturer << 16 | device. */
#define TAG_IDENTITY_FLASH_JEDEC 0U
/** @brief External flash data size in bytes. */
#define TAG_IDENTITY_FLASH_SIZE 0U
/** @brief External flash program-page size in bytes. */
#define TAG_IDENTITY_FLASH_PROGRAM_PAGE 0U
/** @brief External flash smallest erase unit in bytes. */
#define TAG_IDENTITY_FLASH_ERASE_UNIT 0U
/** @brief External flash spare bytes per page (NAND), else 0. */
#define TAG_IDENTITY_FLASH_SPARE 0U
#endif

/* The loader that reads this board's external flash, by naming convention. */
#ifndef TAG_IDENTITY_LOADER
#if defined(TAG_HAS_EXTERNAL_FLASH) && TAG_HAS_EXTERNAL_FLASH
/** @brief External-flash loader name, by the PART_Board naming convention. */
#define TAG_IDENTITY_LOADER TAG_IDENTITY_FLASH_PART "_" TAG_BOARD_ID
#else
/** @brief External-flash loader name, by the PART_Board naming convention. */
#define TAG_IDENTITY_LOADER ""
#endif
#endif

#if defined(TAG_RTC_RV3028) && TAG_RTC_RV3028
/** @brief RTC part. */
#define TAG_IDENTITY_RTC_PART "RV3028"
#else
/** @brief RTC part. */
#define TAG_IDENTITY_RTC_PART "STM32"
#endif

/* Values the tag-info call reports, under the same conditions infoAck() uses. */
#ifdef QTMONITOR_VERSION
/** @brief Minimum qtmonitor version, as infoAck() reports it. */
#define TAG_IDENTITY_QTMONITOR_VERSION ((float)(QTMONITOR_VERSION))
#else
/** @brief Minimum qtmonitor version, as infoAck() reports it. */
#define TAG_IDENTITY_QTMONITOR_VERSION 1.5f
#endif
#if defined(SENSOR_CONSTANTS) && SENSOR_CONSTANTS
/** @brief ACCEL_CONSTANT, or 0 without SENSOR_CONSTANTS. */
#define TAG_IDENTITY_ACCEL_CONSTANT ((float)(ACCEL_CONSTANT))
/** @brief MAG_CONSTANT, or 0 without SENSOR_CONSTANTS. */
#define TAG_IDENTITY_MAG_CONSTANT ((float)(MAG_CONSTANT))
#else
/** @brief ACCEL_CONSTANT, or 0 without SENSOR_CONSTANTS. */
#define TAG_IDENTITY_ACCEL_CONSTANT 0.0f
/** @brief MAG_CONSTANT, or 0 without SENSOR_CONSTANTS. */
#define TAG_IDENTITY_MAG_CONSTANT 0.0f
#endif

/** @brief Monitor protocol version, as the tag-info call has reported it. */
#define TAG_IDENTITY_MONITOR_VERSION "1.0"

/** @brief Stringify a macro's expansion. */
#define TAG_ID_XSTR(s) TAG_ID_STR_(s)
/** @brief Stringify a token sequence (helper for TAG_ID_XSTR). */
#define TAG_ID_STR_(s) #s

#if defined(TAMP_BKP0R) && !defined(RTC_BKP0R)
/** @brief Address of the first backup register: TAMP_BKP0R or RTC_BKP0R. */
#define TAG_IDENTITY_BACKUP_BASE (TAMP_BASE + offsetof(TAMP_TypeDef, BKP0R))
#else
/** @brief Address of the first backup register: TAMP_BKP0R or RTC_BKP0R. */
#define TAG_IDENTITY_BACKUP_BASE (RTC_BASE + offsetof(RTC_TypeDef, BKP0R))
#endif

/* -------------------------------------------------------------------------- */
/* Linker-script symbols.                                                     */
/* -------------------------------------------------------------------------- */

/*
 * Bound under private names with asm labels, so these declarations cannot
 * conflict with a family header's own declaration of the same symbol.
 */
extern const uint8_t tag_id_flash_base[] __asm__("__flash0_base__"); ///< Linker symbol __flash0_base__.
extern const uint8_t tag_id_image_end[] __asm__("__tag_image_end__"); ///< Linker symbol __tag_image_end__.
extern const uint8_t tag_id_persistent_start[] __asm__("__persistent_start__"); ///< Linker symbol __persistent_start__.
extern const uint8_t tag_id_persistent_end[] __asm__("__persistent_end__"); ///< Linker symbol __persistent_end__.
extern const uint8_t tag_id_calibration_start[] __asm__("__calibration_start__"); ///< Linker symbol __calibration_start__.
extern const uint8_t tag_id_calibration_end[] __asm__("__calibration_end__"); ///< Linker symbol __calibration_end__.
extern const uint8_t tag_id_nand_map_start[] __asm__("__nand_map_start__"); ///< Linker symbol __nand_map_start__.
extern const uint8_t tag_id_nand_map_end[] __asm__("__nand_map_end__"); ///< Linker symbol __nand_map_end__.
extern const uint8_t tag_id_default_config[] __asm__("tag_default_config"); ///< Linker symbol tag_default_config.
extern const uint8_t tag_id_default_config_len[] __asm__("tag_default_config_len"); ///< Linker symbol tag_default_config_len.

/* -------------------------------------------------------------------------- */
/* The record.                                                                */
/* -------------------------------------------------------------------------- */

/** @brief Round a byte count up to a multiple of 4. */
#define TAG_ID_PAD(n) (((n) + 3U) & ~3U)

/** @brief A string entry sized for @p literal, NUL-padded to 4 bytes. */
#define TAG_ID_STRING(field, literal)                                           \
  struct {                                                                      \
    uint16_t id;                                                                \
    uint16_t length;                                                            \
    char value[TAG_ID_PAD(sizeof(literal))];                                    \
  } field

/** @brief An entry holding one value of @p type. */
#define TAG_ID_VALUE(field, type)                                               \
  struct {                                                                      \
    uint16_t id;                                                                \
    uint16_t length;                                                            \
    type value;                                                                 \
  } field

/**
 * @struct  TagIdentityRecord
 * @brief   The record's layout for this image.
 *
 * @details The string fields are sized by their literals, so this type is
 *          private to this file; readers walk the entries by id and length.
 */
typedef struct {
  uint32_t magic;           ///< TAG_IDENTITY_MAGIC.
  uint16_t format_version;  ///< TAG_IDENTITY_FORMAT_VERSION.
  uint16_t size;            ///< Bytes in the whole record, header included.

  TAG_ID_STRING(target, TAG_TARGET_NAME); ///< Entry TAG_ID_TARGET.
  TAG_ID_STRING(family, TAG_IDENTITY_FAMILY); ///< Entry TAG_ID_FAMILY.
  TAG_ID_STRING(board, TAG_BOARD_ID); ///< Entry TAG_ID_BOARD.
  TAG_ID_STRING(board_desc, BOARD_NAME); ///< Entry TAG_ID_BOARD_DESC.
  TAG_ID_STRING(board_revision, TAG_IDENTITY_BOARD_REVISION); ///< Entry TAG_ID_BOARD_REVISION.
  TAG_ID_STRING(firmware, FIRMWARE_STRING); ///< Entry TAG_ID_FIRMWARE.
  TAG_ID_STRING(git_repo, GIT_REPO); ///< Entry TAG_ID_GIT_REPO.
  TAG_ID_STRING(git_hash, VERSION_HASH); ///< Entry TAG_ID_GIT_HASH.
  TAG_ID_STRING(git_sha, GIT_SHA); ///< Entry TAG_ID_GIT_SHA.
  TAG_ID_STRING(git_date, GIT_DATE); ///< Entry TAG_ID_GIT_DATE.
  TAG_ID_STRING(source_path, TAG_ID_XSTR(SOURCEDIR)); ///< Entry TAG_ID_SOURCE_PATH.
  TAG_ID_STRING(flash_part, TAG_IDENTITY_FLASH_PART); ///< Entry TAG_ID_FLASH_PART.
  TAG_ID_STRING(rtc_part, TAG_IDENTITY_RTC_PART); ///< Entry TAG_ID_RTC_PART.
  TAG_ID_STRING(loader, TAG_IDENTITY_LOADER); ///< Entry TAG_ID_LOADER.
  TAG_ID_STRING(decoder, TAG_IDENTITY_DECODER); ///< Entry TAG_ID_DECODER.
  TAG_ID_STRING(monitor_version, TAG_IDENTITY_MONITOR_VERSION); ///< Entry TAG_ID_MONITOR_VERSION.

  TAG_ID_VALUE(numbers, TagIdentityNumbers); ///< Entry TAG_ID_NUMBERS.
  TAG_ID_VALUE(image, TagIdentityRegion); ///< Entry TAG_ID_REGION_IMAGE.
  TAG_ID_VALUE(persistent, TagIdentityRegion); ///< Entry TAG_ID_REGION_PERSISTENT.
  TAG_ID_VALUE(state_log, TagIdentityRegion); ///< Entry TAG_ID_REGION_STATE_LOG.
  TAG_ID_VALUE(stored_config, TagIdentityRegion); ///< Entry TAG_ID_REGION_STORED_CONFIG.
  TAG_ID_VALUE(data_headers, TagIdentityRegion); ///< Entry TAG_ID_REGION_DATA_HEADERS.
#if TAG_IDENTITY_CALIBRATION_SLOT_COUNT > 0
  TAG_ID_VALUE(calibration, TagIdentityRegion); ///< Entry TAG_ID_REGION_CALIBRATION.
#endif
#if defined(TAG_IDENTITY_HAS_NAND_MAP) && TAG_IDENTITY_HAS_NAND_MAP
  TAG_ID_VALUE(nand_map, TagIdentityRegion); ///< Entry TAG_ID_REGION_NAND_MAP.
#endif
#if defined(TAG_SCRATCHPAD) && TAG_SCRATCHPAD
  TAG_ID_VALUE(scratchpad, TagIdentityRegion); ///< Entry TAG_ID_REGION_SCRATCHPAD.
#endif
  TAG_ID_VALUE(default_config, TagIdentityBlob); ///< Entry TAG_ID_DEFAULT_CONFIG.
  TAG_ID_VALUE(backup_state, TagIdentityBackupState); ///< Entry TAG_ID_BACKUP_STATE.
  TAG_ID_VALUE(external_flash, TagIdentityExternalFlash); ///< Entry TAG_ID_EXTERNAL_FLASH.
  TAG_ID_VALUE(data_format, TagIdentityDataFormat); ///< Entry TAG_ID_DATA_FORMAT.
  struct {
    uint16_t id;
    uint16_t length;
    float value[TAG_IDENTITY_SCALE_SLOTS];
  } scales; ///< Entry TAG_ID_SCALES.
  TAG_ID_VALUE(build, TagIdentityBuild); ///< Entry TAG_ID_BUILD.

  struct {
    uint16_t id;
    uint16_t length;
  } end; ///< Entry TAG_ID_END.
} TagIdentityRecord;

/** @brief Length field for entry @p field: the size of its value. */
#define TAG_ID_LEN(field) ((uint16_t)sizeof(((TagIdentityRecord *)0)->field.value))

/** @brief Word index of BackupState member @p m. */
#define TAG_ID_WORD(m) ((uint32_t)(offsetof(BackupState, m) / sizeof(uint32_t)))

/** @brief Address of a link-time object, as a 32-bit value. */
#define TAG_ID_ADDR(x) ((uint32_t)(uintptr_t)(x))

/* Build flags, from the options this image was compiled with. */
#if defined(TAG_SCRATCHPAD) && TAG_SCRATCHPAD
/** @brief TAG_IDENTITY_FLAG_SCRATCHPAD when built with TAG_SCRATCHPAD. */
#define TAG_ID_F_SCRATCHPAD TAG_IDENTITY_FLAG_SCRATCHPAD
#else
/** @brief TAG_IDENTITY_FLAG_SCRATCHPAD when built with TAG_SCRATCHPAD. */
#define TAG_ID_F_SCRATCHPAD 0U
#endif
#if defined(TAG_SCRATCHPAD_RING) && TAG_SCRATCHPAD_RING
/** @brief TAG_IDENTITY_FLAG_SCRATCHPAD_RING when built with TAG_SCRATCHPAD_RING. */
#define TAG_ID_F_RING TAG_IDENTITY_FLAG_SCRATCHPAD_RING
#else
/** @brief TAG_IDENTITY_FLAG_SCRATCHPAD_RING when built with TAG_SCRATCHPAD_RING. */
#define TAG_ID_F_RING 0U
#endif
#if defined(TAG_RETAINED_RUN_DIAGNOSTICS) && TAG_RETAINED_RUN_DIAGNOSTICS
/** @brief TAG_IDENTITY_FLAG_RETAINED_RUN_DIAG when built with TAG_RETAINED_RUN_DIAGNOSTICS. */
#define TAG_ID_F_RETAINED TAG_IDENTITY_FLAG_RETAINED_RUN_DIAG
#else
/** @brief TAG_IDENTITY_FLAG_RETAINED_RUN_DIAG when built with TAG_RETAINED_RUN_DIAGNOSTICS. */
#define TAG_ID_F_RETAINED 0U
#endif
#if TAG_STORED_CONFIG_OWN_PAGE
/** @brief TAG_IDENTITY_FLAG_STORED_CONFIG_PAGE when sconfig has its own page. */
#define TAG_ID_F_CONFIG_PAGE TAG_IDENTITY_FLAG_STORED_CONFIG_PAGE
#else
/** @brief TAG_IDENTITY_FLAG_STORED_CONFIG_PAGE when sconfig has its own page. */
#define TAG_ID_F_CONFIG_PAGE 0U
#endif

/**
 * @brief   The tag identity record.
 *
 * @details `used` keeps it through --gc-sections and LTO although no code
 *          refers to its body; the linker script's KEEP keeps the section.
 */
__attribute__((used, section(".tag_identity"), aligned(4)))
const TagIdentityRecord tagIdentity = {
    .magic = TAG_IDENTITY_MAGIC,
    .format_version = TAG_IDENTITY_FORMAT_VERSION,
    .size = (uint16_t)sizeof(TagIdentityRecord),

    .target = {TAG_ID_TARGET, TAG_ID_LEN(target), TAG_TARGET_NAME},
    .family = {TAG_ID_FAMILY, TAG_ID_LEN(family), TAG_IDENTITY_FAMILY},
    .board = {TAG_ID_BOARD, TAG_ID_LEN(board), TAG_BOARD_ID},
    .board_desc = {TAG_ID_BOARD_DESC, TAG_ID_LEN(board_desc), BOARD_NAME},
    .board_revision = {TAG_ID_BOARD_REVISION, TAG_ID_LEN(board_revision),
                       TAG_IDENTITY_BOARD_REVISION},
    .firmware = {TAG_ID_FIRMWARE, TAG_ID_LEN(firmware), FIRMWARE_STRING},
    .git_repo = {TAG_ID_GIT_REPO, TAG_ID_LEN(git_repo), GIT_REPO},
    .git_hash = {TAG_ID_GIT_HASH, TAG_ID_LEN(git_hash), VERSION_HASH},
    .git_sha = {TAG_ID_GIT_SHA, TAG_ID_LEN(git_sha), GIT_SHA},
    .git_date = {TAG_ID_GIT_DATE, TAG_ID_LEN(git_date), GIT_DATE},
    .source_path = {TAG_ID_SOURCE_PATH, TAG_ID_LEN(source_path),
                    TAG_ID_XSTR(SOURCEDIR)},
    .flash_part = {TAG_ID_FLASH_PART, TAG_ID_LEN(flash_part),
                   TAG_IDENTITY_FLASH_PART},
    .rtc_part = {TAG_ID_RTC_PART, TAG_ID_LEN(rtc_part), TAG_IDENTITY_RTC_PART},
    .loader = {TAG_ID_LOADER, TAG_ID_LEN(loader), TAG_IDENTITY_LOADER},
    .decoder = {TAG_ID_DECODER, TAG_ID_LEN(decoder), TAG_IDENTITY_DECODER},
    .monitor_version = {TAG_ID_MONITOR_VERSION, TAG_ID_LEN(monitor_version),
                        TAG_IDENTITY_MONITOR_VERSION},

    .numbers = {TAG_ID_NUMBERS, TAG_ID_LEN(numbers),
                {
                    .tag_type = (uint32_t)TAG_TYPE,
                    .qtmonitor_min_version = TAG_IDENTITY_QTMONITOR_VERSION,
                    .accel_constant = TAG_IDENTITY_ACCEL_CONSTANT,
                    .mag_constant = TAG_IDENTITY_MAG_CONSTANT,
                    .tag_state_max = (uint32_t)_TagState_MAX,
                    .state_event_max = (uint32_t)_State_Event_MAX,
                }},

    .image = {TAG_ID_REGION_IMAGE, TAG_ID_LEN(image),
              {TAG_ID_ADDR(tag_id_flash_base), TAG_ID_ADDR(tag_id_image_end), 0U,
               0U, 0U}},
    .persistent = {TAG_ID_REGION_PERSISTENT, TAG_ID_LEN(persistent),
                   {TAG_ID_ADDR(tag_id_persistent_start),
                    TAG_ID_ADDR(tag_id_persistent_end), 0U, 0U, 0U}},
    .state_log = {TAG_ID_REGION_STATE_LOG, TAG_ID_LEN(state_log),
                  {TAG_ID_ADDR(&sEpoch[0]), TAG_ID_ADDR(&sEpoch[sEPOCH_SIZE]),
                   (uint32_t)sizeof(t_StateMarker), (uint32_t)sEPOCH_SIZE,
                   1U}},
    .stored_config = {TAG_ID_REGION_STORED_CONFIG, TAG_ID_LEN(stored_config),
                      {TAG_ID_ADDR(&sconfig), TAG_ID_ADDR(&sconfig + 1),
                       (uint32_t)sizeof(t_storedconfig), 1U, 1U}},
    .data_headers = {TAG_ID_REGION_DATA_HEADERS, TAG_ID_LEN(data_headers),
                     {TAG_ID_ADDR(&vddHeader[0]), 0U,
                      (uint32_t)TAG_IDENTITY_DATA_HEADER_SIZE, 0U,
                      (uint32_t)TAG_IDENTITY_DATA_LAYOUT_VERSION}},
#if TAG_IDENTITY_CALIBRATION_SLOT_COUNT > 0
    .calibration = {TAG_ID_REGION_CALIBRATION, TAG_ID_LEN(calibration),
                    {TAG_ID_ADDR(tag_id_calibration_start),
                     TAG_ID_ADDR(tag_id_calibration_end),
                     (uint32_t)TAG_IDENTITY_CALIBRATION_SLOT_SIZE,
                     (uint32_t)TAG_IDENTITY_CALIBRATION_SLOT_COUNT, 1U}},
#endif
#if defined(TAG_IDENTITY_HAS_NAND_MAP) && TAG_IDENTITY_HAS_NAND_MAP
    .nand_map = {TAG_ID_REGION_NAND_MAP, TAG_ID_LEN(nand_map),
                 {TAG_ID_ADDR(tag_id_nand_map_start),
                  TAG_ID_ADDR(tag_id_nand_map_end), (uint32_t)sizeof(uint16_t),
                  (uint32_t)GD5F_PHYSICAL_BLOCK_COUNT, 1U}},
#endif
#if defined(TAG_SCRATCHPAD) && TAG_SCRATCHPAD
    .scratchpad = {TAG_ID_REGION_SCRATCHPAD, TAG_ID_LEN(scratchpad),
                   {TAG_SCRATCH_BASE, TAG_SCRATCH_BASE + TAG_SCRATCH_SIZE, 0U,
                    0U, 1U}},
#endif
    .default_config = {TAG_ID_DEFAULT_CONFIG, TAG_ID_LEN(default_config),
                       {TAG_ID_ADDR(tag_id_default_config),
                        TAG_ID_ADDR(tag_id_default_config_len)}},
    .backup_state = {TAG_ID_BACKUP_STATE, TAG_ID_LEN(backup_state),
                     {
                         .base = (uint32_t)TAG_IDENTITY_BACKUP_BASE,
                         .word_count =
                             (uint32_t)(sizeof(BackupState) / sizeof(uint32_t)),
                         .valid_magic = BACKUP_STATE_VALID_MAGIC,
                         .word_valid = TAG_ID_WORD(valid),
                         .word_state = TAG_ID_WORD(state),
                         .word_pages = TAG_ID_WORD(pages),
                         .word_external_blocks = TAG_ID_WORD(external_blocks),
                         .word_reset_cause = TAG_ID_WORD(resetCause),
                     }},
    .external_flash = {TAG_ID_EXTERNAL_FLASH, TAG_ID_LEN(external_flash),
                       {
                           .jedec_id = (uint32_t)TAG_IDENTITY_FLASH_JEDEC,
                           .size = (uint32_t)TAG_IDENTITY_FLASH_SIZE,
                           .program_page =
                               (uint32_t)TAG_IDENTITY_FLASH_PROGRAM_PAGE,
                           .erase_unit = (uint32_t)TAG_IDENTITY_FLASH_ERASE_UNIT,
                           .spare = (uint32_t)TAG_IDENTITY_FLASH_SPARE,
                       }},
    .data_format = {TAG_ID_DATA_FORMAT, TAG_ID_LEN(data_format),
                    {
                        .layout_version =
                            (uint32_t)TAG_IDENTITY_DATA_LAYOUT_VERSION,
                        .mapping = (uint32_t)TAG_IDENTITY_MAPPING,
                        .header_size = (uint32_t)TAG_IDENTITY_DATA_HEADER_SIZE,
                        .page_bytes = (uint32_t)TAG_IDENTITY_PAGE_BYTES,
                        .samples_per_page =
                            (uint32_t)TAG_IDENTITY_SAMPLES_PER_PAGE,
                        .sample_bytes = (uint32_t)TAG_IDENTITY_SAMPLE_BYTES,
                        .sample_period_s = (uint32_t)TAG_IDENTITY_SAMPLE_PERIOD_S,
                        .subsecond_hz = (uint32_t)TAG_IDENTITY_SUBSECOND_HZ,
                    }},
    .scales = {TAG_ID_SCALES, TAG_ID_LEN(scales), {TAG_IDENTITY_SCALES}},
    .build = {TAG_ID_BUILD, TAG_ID_LEN(build),
              {
                  .options_digest = (uint32_t)TAG_BUILD_OPTIONS_DIGEST,
                  .flags = TAG_ID_F_SCRATCHPAD | TAG_ID_F_RING |
                           TAG_ID_F_RETAINED | TAG_ID_F_CONFIG_PAGE,
              }},
    .end = {TAG_ID_END, 0U},
};

/* Every entry must keep the 4-byte alignment readers rely on. */
_Static_assert(sizeof(TagIdentityRecord) % 4U == 0U,
               "tag identity record must be a multiple of 4 bytes");
_Static_assert(sizeof(TagIdentityRecord) <= 0xFFFFU,
               "tag identity record size must fit its 16-bit field");

/* Documented in tag_identity.h. */
const TagIdentityStrings tagIdentityStrings = {
    .board_desc = tagIdentity.board_desc.value,
    .firmware = tagIdentity.firmware.value,
    .git_repo = tagIdentity.git_repo.value,
    .git_hash = tagIdentity.git_hash.value,
    .git_date = tagIdentity.git_date.value,
    .source_path = tagIdentity.source_path.value,
    .monitor_version = tagIdentity.monitor_version.value,
};
