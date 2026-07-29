/**
 * @file imutag_log_format.h
 * @brief IMUTag binary log layout shared by firmware and host tools.
 *
 * @details Defines the packed external flash page format, timing flag bits,
 *          and fixed-size IMU/auxiliary sample records used when decoding raw
 *          IMUTag logs. The layout is intentionally little-endian and exactly
 *          one NAND page wide.
 */

#ifndef IMUTAG_LOG_FORMAT_H
#define IMUTAG_LOG_FORMAT_H

#include <stdint.h>

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
  #if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
    #error "This binary logging layout only supports little-endian architectures."
  #endif
#elif defined(__BIG_ENDIAN__)
  #error "This binary logging layout only supports little-endian architectures."
#endif

/**
 * @def IMUTAG_HEADER_MILLIS_MASK
 * @brief Bit mask for the 1/1024-second subsecond field in t_DataHeader.millis.
 *
 * @details Host log writers convert those ticks to rounded integer
 *          milliseconds. Bits outside this mask carry IMUTag header flags.
 */
/* t_DataHeader.millis uses the low ten bits for 1/1024-second subsecond ticks.
 * Host log writers convert those ticks to rounded integer milliseconds. The
 * remaining bits are header flags. */
#define IMUTAG_HEADER_MILLIS_MASK (0x03ffu)
/** @brief Header flag marking a timestamp resynchronization boundary. */
#define IMUTAG_HEADER_RESYNC (0x0400u)
/** @brief Header flag marking pages skipped by the storage layer. */
#define IMUTAG_HEADER_RESYNC_STORAGE_SKIP (0x0800u)
/** @brief Header flag marking a checkpoint written after reset recovery. */
#define IMUTAG_HEADER_RESTART_RECOVERY (0x1000u)

/**
 * @def IMUTAG_CHECKPOINT_PAGES
 * @brief External log pages covered by one sparse NAND checkpoint.
 */
/* NAND-backed IMUTag variants write one STM32 internal checkpoint for each
 * group of external log pages. The group is aligned so it remains within one
 * NAND erase block. */
#define IMUTAG_CHECKPOINT_PAGES (8u)

/* LPS22HH/LPS22DF native sensitivity is 1/4096 hPa/LSB. */
#define IMUTAG_PRESSURE_HPA_PER_LSB (1.0 / 4096.0)

/* Pressure-sensor temperature is stored in the native ST 0.01 C/LSB unit. */
#define IMUTAG_PRESSURE_TEMPERATURE_C_PER_LSB (0.01)

/* AK09940 native sensitivity is 0.01 uT/LSB. */
#define IMUTAG_MAG_UT_PER_LSB (0.01)

/** @brief Size in bytes of one IMUTag external log page. */
#define IMUTAG_PAGE_SIZE 2048
/** @brief Number of high-rate IMU samples packed into one superframe. */
#define IMUTAG_IMU_SAMPLES_PER_SUPERFRAME 10
/** @brief Number of superframes stored after the page header. */
#define IMUTAG_SUPERFRAMES_PER_PAGE 15
/** @brief Total high-rate IMU samples in one external log page. */
#define IMUTAG_IMU_SAMPLES_PER_PAGE \
    (IMUTAG_IMU_SAMPLES_PER_SUPERFRAME * IMUTAG_SUPERFRAMES_PER_PAGE)

#pragma pack(push, 1)

/**
 * @brief One raw accelerometer/gyroscope IMU sample in sensor-native units.
 */
typedef struct {
    int16_t ax;
    int16_t ay;
    int16_t az;
    int16_t gx;
    int16_t gy;
    int16_t gz;
} t_ImuTagImuSample;

typedef t_ImuTagImuSample t_ImuTagRawSensorData;

/**
 * @brief One lower-rate pressure and magnetometer sample paired with IMU data.
 */
typedef struct {
    float pressure;
    float mag_x;
    float mag_y;
    float mag_z;
} t_ImuTagAuxSample;

/**
 * @brief Fixed-size group of high-rate IMU samples plus one auxiliary sample.
 */
typedef struct {
    t_ImuTagImuSample imu[IMUTAG_IMU_SAMPLES_PER_SUPERFRAME];
    t_ImuTagAuxSample aux;
} t_ImuTagSuperFrame;

/**
 * @brief Timestamp and environmental header stored at the start of each page.
 *
 * @details The low bits of @c millis are subsecond ticks; the upper bits may
 *          carry IMUTag header flags when the header is used as an internal
 *          checkpoint or host segment marker.
 */
typedef struct {
    int32_t epoch;
    uint16_t millis;
    int16_t rawtemp;
} t_ImuTagPageHeader;

typedef t_ImuTagPageHeader t_ImuTagDataHeader;

/**
 * @brief Complete 2048-byte IMUTag external log page.
 */
typedef struct {
    t_ImuTagPageHeader slow_data;
    t_ImuTagSuperFrame frames[IMUTAG_SUPERFRAMES_PER_PAGE];
} t_ImuTagDataLog;

typedef char imutag_imu_sample_size_must_be_12[
    sizeof(t_ImuTagImuSample) == 12 ? 1 : -1];
typedef char imutag_aux_sample_size_must_be_16[
    sizeof(t_ImuTagAuxSample) == 16 ? 1 : -1];
typedef char imutag_superframe_size_must_be_136[
    sizeof(t_ImuTagSuperFrame) == 136 ? 1 : -1];
typedef char imutag_page_header_size_must_be_8[
    sizeof(t_ImuTagPageHeader) == 8 ? 1 : -1];
typedef char imutag_page_size_must_be_2048[
    sizeof(t_ImuTagDataLog) == IMUTAG_PAGE_SIZE ? 1 : -1];

#pragma pack(pop)

#endif
