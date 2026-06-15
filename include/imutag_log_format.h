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

/*
 * IMUTag external-log wire-format constants shared by firmware and host tools.
 * These describe the compact log encoding, not the native sensor register
 * formats.
 */

#define IMUTAG_ENV_SKIP_RAW (-1)

/* t_DataHeader.millis uses the low ten bits for 1/1024-second subsecond ticks.
 * Host log writers convert those ticks to rounded integer milliseconds. The
 * remaining bits are header flags. */
#define IMUTAG_HEADER_MILLIS_MASK (0x03ffu)
#define IMUTAG_HEADER_RESYNC (0x0400u)
#define IMUTAG_HEADER_RESYNC_STORAGE_SKIP (0x0800u)

/* t_DataLog stores LPS22HH pressure as sensor_raw >> 8.
 * LPS22HH native sensitivity is 1/4096 hPa/LSB, so compact_raw / 16 = hPa. */
#define IMUTAG_COMPACT_PRESSURE_HPA_PER_LSB (1.0 / 16.0)

/* t_DataLog stores AK09940 magnetometer axes as sensor_raw >> 2.
 * AK09940 native sensitivity is 0.01 uT/LSB, so compact_raw * 0.04 = uT. */
#define IMUTAG_COMPACT_MAG_UT_PER_LSB (0.04)

#pragma pack(push, 1)

typedef struct {
    int16_t gx, gy, gz, ax, ay, az;
} t_ImuTagRawSensorData;

typedef struct {
    int16_t pressure;
    int16_t mx, my, mz;
    t_ImuTagRawSensorData raw_data[10];
} t_ImuTagDataLog;

typedef struct {
    int32_t epoch;
    uint16_t millis;
    int16_t rawtemp;
} t_ImuTagDataHeader;

#pragma pack(pop)

#endif
