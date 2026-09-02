/**
 * @file sensors.h
 * @brief IMUTag family sensor sampling and calibration API.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef _SENSORS_H
#define _SENSORS_H

#include <stdbool.h>
#include <stdint.h>

#include "imutag_log_format.h"
#include <tag.pb.h>
#include "core_state.h"

/** One raw synchronized IMU sample pair stored in the data log. */
typedef t_ImuTagRawSensorData RawSensorData;

/**
 * @brief Clear one raw sensor sample; retained for legacy RUNNING code.
 *
 * @param[out] data Raw sample record to clear.
 * @return true after the record has been initialized.
 */
bool sensorSample(RawSensorData *data);
/** @name Collection-init failure detail
 * Bit assignments for the detail word reported through
 * tagStateMarkerDetail() when initDataCollection() fails and the run aborts.
 *
 * Bits rather than an enum because more than one device can fail in the same
 * attempt, and that distinction matters: one sensor failing repeatedly points
 * at that part, several failing together points at the shared bus or supply.
 * @{
 */
/** @brief The stored configuration did not yield a usable LSM6 mode. */
#define IMUTAG_INIT_FAIL_LSM_CONFIG (1U << 0)
/** @brief Magnetometer collection configuration failed. */
#define IMUTAG_INIT_FAIL_MAG        (1U << 1)
/** @brief Pressure-sensor collection configuration failed. */
#define IMUTAG_INIT_FAIL_PRESSURE   (1U << 2)
/** @brief Shift of the first failing device's driver status. */
#define IMUTAG_INIT_FAIL_STATUS_SHIFT 8U
/** @brief Mask of the first failing device's driver status. */
#define IMUTAG_INIT_FAIL_STATUS_MASK  (0xFFU << IMUTAG_INIT_FAIL_STATUS_SHIFT)
/** @} */

/**
 * @brief Configure IMU, magnetometer, and pressure sensor for collection.
 *
 * @details Latches which stage failed for tagStateMarkerDetail(), so an abort
 *          at start records the cause in the state marker rather than reaching
 *          flash as a bare State_EVENT_UNKNOWN.
 *
 * @return true when all collection devices are configured and ready. On false,
 *         imuTagCollectionInitFailure() reports which stages failed.
 *
 * @note Auxiliary-sensor failures do not stop the remaining configuration; the
 *       function continues and reports the aggregate. See @ref
 *       imuTagCollectionInitFailure().
 */
bool initDataCollection(void);

/**
 * @brief Report which stages of the last initDataCollection() failed.
 *
 * @return Bitwise OR of IMUTAG_INIT_FAIL_* for the most recent attempt this
 *         boot, with the first failing device's driver status in
 *         IMUTAG_INIT_FAIL_STATUS_MASK. Zero when the last attempt succeeded
 *         or none has run.
 */
uint32_t imuTagCollectionInitFailure(void);
/**
 * @brief Fill one superframe when the IMU FIFO has enough samples.
 *
 * @param[out] frame Superframe populated with IMU and auxiliary samples.
 * @return true when a complete superframe was captured.
 */
bool sampleDataCollection(t_ImuTagSuperFrame *frame);
/**
 * @brief Return the latest raw LPS22HH temperature in hundredths of a degree C.
 *
 * @param[out] rawtemp Latest pressure-sensor temperature sample.
 * @return true when a pressure sample has been captured since collection start.
 */
bool latestDataCollectionRawTemp(int16_t *rawtemp);
/**
 * @brief Shut down collection sensors and trigger generation.
 *
 * @return true when devices were returned to a low-power state.
 */
bool deinitDataCollection(void);
/**
 * @brief State-machine handler for live calibration mode.
 *
 * @param[in] t State transition phase.
 * @param[in] reason Event that caused this calibration-state action.
 * @return Requested sleep mode after calibration handling.
 */
enum Sleep Calibrating(enum StateTrans t, State_Event reason);
/**
 * @brief Populate a monitor ACK with one live calibration sample.
 *
 * @param[out] ack Monitor acknowledgement to populate.
 * @return 0 on success or a negative error when no sample is available.
 */
int calibration_logAck(Ack *ack);
/**
 * @brief Persist calibration constants from the host.
 *
 * @param[in] constants Calibration constants supplied by the host.
 * @return 0 on success or a negative storage/validation error.
 */
int write_calibration(CalibrationConstants *constants);
/**
 * @brief Read stored calibration constants for the host.
 *
 * @param[in] index Calibration record index requested by the host.
 * @param[out] ack Monitor acknowledgement to populate.
 * @return 0 on success or a negative error when the record is unavailable.
 */
int read_calibration(int32_t index, Ack *ack);
/**
 * @brief Report whether at least one calibration entry is stored.
 *
 * @return true when stored calibration data is available.
 */
bool sensorsHaveCalibration(void);


#endif
