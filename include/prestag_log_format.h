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

/*
 * PresTag high-resolution log (Config.lps27.high_resolution).
 *
 * A PresTagHRLog carries one packed byte stream, a concatenation of
 * t_PresTagHRBlock records. Each block is one internal header (epoch, voltage,
 * temperature) and one 240-byte external page of 48 five-byte samples. The
 * page is the same size as a PresTag page, so the header-to-page pairing is
 * unchanged: header i owns external bytes [i*240, (i+1)*240).
 *
 * A sample is the LPS27HHW output registers PRESS_OUT_XL..TEMP_OUT_H
 * (0x28..0x2C) exactly as one burst read returns them:
 *
 *   pressure     signed 24-bit little-endian, PRESTAG_HR_PRESSURE_LSB_PER_HPA
 *   temperature  signed 16-bit, PRESTAG_HR_TEMPERATURE_LSB_PER_C
 *
 * A page ends at its first pressure of PRESTAG_RAW_PRESSURE_END (-1): erased
 * flash reads 0xFFFFFF, which sign-extends to -1. A pressure of
 * PRESTAG_HR_PRESSURE_FAILED marks a sample whose conversion never became
 * ready; it holds its slot so later samples keep their timestamps.
 */

/** @brief Samples in one high-resolution PresTag page. */
#define PRESTAG_HR_LOG_SAMPLES 48
/** @brief Bytes in one high-resolution sample: 3 pressure, 2 temperature. */
#define PRESTAG_HR_SAMPLE_BYTES 5
/** @brief Raw LPS27 pressure counts per hPa at full resolution. */
#define PRESTAG_HR_PRESSURE_LSB_PER_HPA 4096
/** @brief Raw LPS27 temperature counts per degree Celsius. */
#define PRESTAG_HR_TEMPERATURE_LSB_PER_C 100
/** @brief Sign-extended 24-bit pressure written when a read failed. */
#define PRESTAG_HR_PRESSURE_FAILED (-8388608)

#pragma pack(push, 1)

/** @brief One full-resolution LPS27 sample, as the sensor returns it. */
typedef struct {
    uint8_t pressure[3];   /**< Signed 24-bit pressure, little-endian. */
    int16_t temperature;   /**< Signed 16-bit temperature. */
} t_PresTagHRSample;

/** @brief One high-resolution PresTag page. */
typedef struct {
    t_PresTagHRSample data[PRESTAG_HR_LOG_SAMPLES]; /**< Samples in time order. */
} t_PresTagHRDataLog;

/** @brief One header and its page, as packed in PresTagHRLog.samples. */
typedef struct {
    int32_t epoch;                /**< Header epoch: time of sample 0. */
    float voltage;                /**< Supply voltage at page start, V. */
    float temperature;            /**< Header second word * 0.01. */
    t_PresTagHRDataLog samples;   /**< The page. */
} t_PresTagHRBlock;

#pragma pack(pop)

/**
 * @brief Sign-extend a sample's 24-bit pressure.
 *
 * @param[in] sample Sample to decode.
 * @return Raw pressure in counts of 1/PRESTAG_HR_PRESSURE_LSB_PER_HPA hPa;
 *         -1 marks the end of a page, PRESTAG_HR_PRESSURE_FAILED a failed read.
 */
static inline int32_t prestagHRPressureRaw(const t_PresTagHRSample *sample)
{
    uint32_t u = (uint32_t)sample->pressure[0] |
                 ((uint32_t)sample->pressure[1] << 8) |
                 ((uint32_t)sample->pressure[2] << 16);
    if (u & 0x800000u)
        u |= 0xFF000000u;
    return (int32_t)u;
}

#endif
