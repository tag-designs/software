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

#define IMUTAG_BLOCK_PRESSURE_VALID (0x0001u)
#define IMUTAG_BLOCK_MAG_VALID (0x0002u)
#define IMUTAG_BLOCK_PRESSURE_TEMPERATURE_VALID (0x0004u)
#define IMUTAG_BLOCK_UNWRITTEN (0x8000u)
#define IMUTAG_BLOCK_IS_WRITTEN(flags) (((flags) & IMUTAG_BLOCK_UNWRITTEN) == 0)

/* t_DataHeader.millis uses the low ten bits for 1/1024-second subsecond ticks.
 * Host log writers convert those ticks to rounded integer milliseconds. The
 * remaining bits are header flags. */
#define IMUTAG_HEADER_MILLIS_MASK (0x03ffu)
#define IMUTAG_HEADER_RESYNC (0x0400u)
#define IMUTAG_HEADER_RESYNC_STORAGE_SKIP (0x0800u)

/* t_DataLog stores pressure in the native ST 24-bit raw unit, sign-extended
 * into int32_t. LPS22HH/LPS22DF native sensitivity is 1/4096 hPa/LSB. */
#define IMUTAG_PRESSURE_HPA_PER_LSB (1.0 / 4096.0)

/* Pressure-sensor temperature is stored in the native ST 0.01 C/LSB unit. */
#define IMUTAG_PRESSURE_TEMPERATURE_C_PER_LSB (0.01)

/* t_DataLog stores AK09940 magnetometer axes as full 18-bit native raw values,
 * sign-extended into int32_t. AK09940 native sensitivity is 0.01 uT/LSB. */
#define IMUTAG_MAG_UT_PER_LSB (0.01)

#pragma pack(push, 1)

typedef struct {
    int16_t gx, gy, gz, ax, ay, az;
} t_ImuTagRawSensorData;

#define IMUTAG_IMU_SAMPLES_PER_BLOCK 8

typedef struct {
    int32_t mx, my, mz;
    int32_t pressure;
    int16_t pressure_temperature;
    uint8_t reserved[12];
    t_ImuTagRawSensorData raw_data[IMUTAG_IMU_SAMPLES_PER_BLOCK];
    uint16_t flags;
} t_ImuTagDataLog;

typedef struct {
    int32_t epoch;
    uint16_t millis;
    int16_t rawtemp;
} t_ImuTagDataHeader;

#pragma pack(pop)

#endif
