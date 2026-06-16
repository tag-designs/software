#ifndef PRESTAG_LOG_FORMAT_H
#define PRESTAG_LOG_FORMAT_H

#include <stdint.h>

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
  #if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
    #error "This binary logging layout only supports little-endian architectures."
  #endif
#elif defined(__BIG_ENDIAN__)
  #error "This binary logging layout only supports little-endian architectures."
#endif

/*
 * PresTag raw-log constants and decoder-side structures shared by firmware and
 * host tools.
 *
 * A PresTagRawLog carries conversion constants plus one packed byte stream.
 * The samples field is a concatenation of t_PresTagRawBlock records. Each
 * block owns one timestamp/header group and one 60-sample raw data page:
 *
 *   PresTagRawLog
 *     temp_constant
 *     pres_constant
 *     samples -> [
 *       { epoch, voltage, temperature, 60 raw pressure/temperature pairs },
 *       { epoch, voltage, temperature, 60 raw pressure/temperature pairs },
 *       ...
 *     ]
 *
 * The binary payload is little-endian and stores floats in the target C float
 * representation used by the firmware build.
 */

#define PRESTAG_LOG_SAMPLES 60
#define PRESTAG_RAW_PRESSURE_END (-1)

#pragma pack(push, 1)

typedef struct {
    int16_t pressure;
    int16_t temperature;
} t_PresTagRawSample;

typedef struct {
    t_PresTagRawSample data[PRESTAG_LOG_SAMPLES];
} t_PresTagDataLog;

typedef struct {
    int32_t epoch;
    float voltage;
    float temperature;
    t_PresTagDataLog samples;
} t_PresTagRawBlock;

#pragma pack(pop)

typedef struct {
    float temp_constant;
    float pres_constant;
} t_PresTagRawLogScales;

#endif
