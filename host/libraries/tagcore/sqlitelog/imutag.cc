#include "sqlitelog/internal.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>

#include "imutag_log_format.h"

namespace tagcore::sqlite_log {

// IMUTag raw downloads carry one 2048-byte page per ACK. Each page starts with
// an absolute timestamp/pressure-temperature header followed by 15 superframes.
// A superframe contains ten IMU samples plus one float pressure/magnetometer
// auxiliary slot.

namespace {

constexpr size_t kImuSamplesPerSuperFrame = IMUTAG_IMU_SAMPLES_PER_SUPERFRAME;
constexpr size_t kImuSuperFramesPerPage = IMUTAG_SUPERFRAMES_PER_PAGE;
constexpr size_t kImuPageBytes = sizeof(t_ImuTagDataLog);
constexpr uint32_t kImuHeaderSubsecondTicksPerSecond = 1024;
constexpr uint32_t kMillisecondsPerSecond = 1000;
constexpr uint32_t kMissingAuxBits = 0x7fc00000U;

static_assert(kImuPageBytes == IMUTAG_PAGE_SIZE,
              "IMUTag host decoder expects the shared page layout");

constexpr int imuHeaderTicksToRoundedMillisecond(uint32_t ticks)
{
    return static_cast<int>(
        (static_cast<uint64_t>(ticks) * kMillisecondsPerSecond
         + kImuHeaderSubsecondTicksPerSecond / 2)
        / kImuHeaderSubsecondTicksPerSecond);
}

static_assert(imuHeaderTicksToRoundedMillisecond(0) == 0);
static_assert(imuHeaderTicksToRoundedMillisecond(1) == 1);
static_assert(imuHeaderTicksToRoundedMillisecond(512) == 500);
static_assert(imuHeaderTicksToRoundedMillisecond(1023) == 999);

double imuAccelMgPerLsb(Lsm6dsv_ACCEL range)
{
    switch (range) {
    case Lsm6dsv_ACCEL_R2G:
        return 0.061;
    case Lsm6dsv_ACCEL_R4G:
        return 0.122;
    case Lsm6dsv_ACCEL_R8G:
        return 0.244;
    case Lsm6dsv_ACCEL_R16G:
        return 0.488;
    default:
        return 1.0;
    }
}

double imuGyroDpsPerLsb(Lsm6dsv_GYRO range)
{
    switch (range) {
    case Lsm6dsv_GYRO_R125dps:
        return 0.004375;
    case Lsm6dsv_GYRO_R250dps:
        return 0.00875;
    case Lsm6dsv_GYRO_R500dps:
        return 0.0175;
    case Lsm6dsv_GYRO_R1000dps:
        return 0.035;
    case Lsm6dsv_GYRO_R2000dps:
        return 0.07;
    case Lsm6dsv_GYRO_R4000dps:
        return 0.14;
    default:
        return 1.0;
    }
}

double pressureTemperatureRawToC(int16_t temperature_raw)
{
    return temperature_raw * IMUTAG_PRESSURE_TEMPERATURE_C_PER_LSB;
}

bool isMissingAux(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits == kMissingAuxBits;
}

bool hasPressureSample(const t_ImuTagAuxSample &aux)
{
    return !isMissingAux(aux.pressure);
}

bool hasMagSample(const t_ImuTagAuxSample &aux)
{
    return !isMissingAux(aux.mag_x)
        && !isMissingAux(aux.mag_y)
        && !isMissingAux(aux.mag_z);
}

bool isErasedSuperFrame(const t_ImuTagSuperFrame &frame)
{
    const auto *bytes = reinterpret_cast<const uint8_t *>(&frame);
    for (size_t i = 0; i < sizeof(frame); i++) {
        if (bytes[i] != 0xffU) {
            return false;
        }
    }
    return true;
}

sqlite3_int64 correctedNominalElapsedUs(
    uint64_t index,
    sqlite3_int64 nominal_timestep_us,
    double correction_factor)
{
    // Keep the correction non-accumulating: compute from the nominal elapsed
    // time for this index, then apply the ppm-derived factor once.
    const long double raw_elapsed_us =
        static_cast<long double>(index)
        * static_cast<long double>(nominal_timestep_us);
    return static_cast<sqlite3_int64>(
        std::llround(raw_elapsed_us * correction_factor));
}

sqlite3_int64 nominalElapsedUs(uint64_t index, sqlite3_int64 nominal_timestep_us)
{
    return static_cast<sqlite3_int64>(
        static_cast<long double>(index)
        * static_cast<long double>(nominal_timestep_us));
}

std::string imuHeaderEventName(uint32_t flags)
{
    if ((flags & IMUTAG_HEADER_RESYNC) == 0U) {
        return "";
    }
    if ((flags & IMUTAG_HEADER_RESYNC_STORAGE_SKIP) != 0U) {
        return "RESYNC_STORAGE_SKIP";
    }
    if ((flags & IMUTAG_HEADER_RESTART_RECOVERY) != 0U) {
        return "RESTART_RECOVERY";
    }
    return "RESYNC";
}

int dumpIMUTagPage(WriterContext &ctx,
                   const t_ImuTagDataLog &page,
                   uint32_t raw_millisecond_flags)
{
    if (!ctx.createLogTables()) {
        return -2;
    }

    if (!ctx.config.has_lsm6()) {
        ctx.setLastError("IMUTag SQLite output requires an LSM6 configuration");
        return -2;
    }
    if (ctx.imu == nullptr) {
        ctx.setLastError("IMUTag SQLite output missing decode state");
        return -2;
    }

    const Lsm6dsv_ODR odr = ctx.config.lsm6().odr();
    if (odr == Lsm6dsv_ODR_ODR_UNSPECIFIED || !Lsm6dsv_ODR_IsValid(odr)) {
        ctx.setLastError("IMUTag SQLite output requires a valid IMU ODR");
        return -2;
    }
    const int odr_hz = static_cast<int>(odr);

    const sqlite3_int64 sample_period_us = 1000000 / odr_hz;
    const sqlite3_int64 superframe_period_us =
        sample_period_us * static_cast<sqlite3_int64>(kImuSamplesPerSuperFrame);
    const double accel_scale = imuAccelMgPerLsb(ctx.config.lsm6().accel_rng());
    const double gyro_scale = imuGyroDpsPerLsb(ctx.config.lsm6().gyro_rng());

    Statement header_insert(
        ctx.db,
        "INSERT INTO ImuHeader "
        "(HeaderIndex, SegmentId, StartElapsedUs, Epoch, Millisecond, "
        "SubsecondTicks, SubsecondHz, Flags, Temperature) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
    Statement segment_insert(
        ctx.db,
        "INSERT INTO ImuSegment "
        "(SegmentId, HeaderIndex, StartElapsedUs, Epoch, Millisecond, "
        "SubsecondTicks, SubsecondHz, Flags, Event, FirstSampleIndex, "
        "ConfiguredOdrHz, CorrectionPpm) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    Statement event_insert(
        ctx.db,
        "INSERT INTO ImuEvent (StartElapsedUs, HeaderIndex, Event) VALUES (?, ?, ?)");
    Statement pressure_insert(
        ctx.db,
        "INSERT INTO ImuPressure "
        "(SegmentId, SampleIndex, RawElapsedUs, ElapsedUs, Pressure) "
        "VALUES (?, ?, ?, ?, ?)");
    Statement temperature_insert(
        ctx.db,
        "INSERT INTO ImuTemperature "
        "(SegmentId, SampleIndex, RawElapsedUs, ElapsedUs, Temperature) "
        "VALUES (?, ?, ?, ?, ?)");
    Statement mag_insert(
        ctx.db,
        "INSERT INTO ImuMag "
        "(SegmentId, SampleIndex, RawElapsedUs, ElapsedUs, mx, my, mz) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)");
    Statement accel_insert(
        ctx.db,
        "INSERT INTO ImuAccel "
        "(SegmentId, SampleIndex, RawElapsedUs, ElapsedUs, ax, ay, az) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)");
    Statement gyro_insert(
        ctx.db,
        "INSERT INTO ImuGyro "
        "(SegmentId, SampleIndex, RawElapsedUs, ElapsedUs, gx, gy, gz) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)");

    if (!header_insert.valid()
        || !segment_insert.valid()
        || !event_insert.valid()
        || !pressure_insert.valid()
        || !temperature_insert.valid()
        || !mag_insert.valid()
        || !accel_insert.valid()
        || !gyro_insert.valid()) {
        ctx.setLastSqliteError("Could not prepare IMUTag log insert");
        return -2;
    }

    const uint32_t page_subsecond_ticks =
        page.slow_data.millis & IMUTAG_HEADER_MILLIS_MASK;
    const int page_millisecond =
        imuHeaderTicksToRoundedMillisecond(page_subsecond_ticks);
    const int header_flags =
        static_cast<int>(raw_millisecond_flags & ~IMUTAG_HEADER_MILLIS_MASK);
    const bool header_resync =
        (raw_millisecond_flags & IMUTAG_HEADER_RESYNC) != 0;
    const bool header_storage_skip =
        (raw_millisecond_flags & IMUTAG_HEADER_RESYNC_STORAGE_SKIP) != 0;
    const std::string header_event = imuHeaderEventName(header_flags);
    const sqlite3_int64 page_epoch_ms =
        static_cast<sqlite3_int64>(page.slow_data.epoch) * 1000 + page_millisecond;

    if (!ctx.imu->have_collection_anchor) {
        ctx.imu->collection_anchor_epoch_ms = page_epoch_ms;
        ctx.imu->elapsed_base_us = 0;
        ctx.imu->segment_block_count = 0;
        ctx.imu->segment_id = 0;
        ctx.imu->have_collection_anchor = true;
    } else if (header_resync) {
        const sqlite3_int64 elapsed_from_anchor_us =
            (page_epoch_ms - ctx.imu->collection_anchor_epoch_ms) * 1000;
        const sqlite3_int64 current_end_us =
            ctx.imu->elapsed_base_us
            + correctedNominalElapsedUs(ctx.imu->segment_block_count,
                                        superframe_period_us,
                                        ctx.imu->clock_correction_factor);
        ctx.imu->elapsed_base_us =
            elapsed_from_anchor_us > current_end_us ? elapsed_from_anchor_us : current_end_us;
        ctx.imu->segment_block_count = 0;
        ctx.imu->segment_id++;
    }

    const sqlite3_int64 page_start_us =
        ctx.imu->elapsed_base_us
        + correctedNominalElapsedUs(ctx.imu->segment_block_count,
                                    superframe_period_us,
                                    ctx.imu->clock_correction_factor);
    const double page_temperature = pressureTemperatureRawToC(page.slow_data.rawtemp);
    const uint64_t first_sample_index =
        ctx.imu->segment_block_count * kImuSamplesPerSuperFrame;

    if (!header_insert.bindInt64(1, static_cast<sqlite3_int64>(ctx.imu->header_count))
        || !header_insert.bindInt64(2, static_cast<sqlite3_int64>(ctx.imu->segment_id))
        || !header_insert.bindInt64(3, page_start_us)
        || !header_insert.bindInt64(4, page.slow_data.epoch)
        || !header_insert.bindInt64(5, page_millisecond)
        || !header_insert.bindInt64(6, page_subsecond_ticks)
        || !header_insert.bindInt64(7, kImuHeaderSubsecondTicksPerSecond)
        || !header_insert.bindInt64(8, header_flags)
        || !header_insert.bindDouble(9, page_temperature)
        || !header_insert.stepDone()) {
        ctx.setLastSqliteError("IMUTag header insert failed");
        return -2;
    }

    if ((ctx.imu->header_count == 0 || header_resync)
        && (!segment_insert.bindInt64(1, static_cast<sqlite3_int64>(ctx.imu->segment_id))
            || !segment_insert.bindInt64(2, static_cast<sqlite3_int64>(ctx.imu->header_count))
            || !segment_insert.bindInt64(3, page_start_us)
            || !segment_insert.bindInt64(4, page.slow_data.epoch)
            || !segment_insert.bindInt64(5, page_millisecond)
            || !segment_insert.bindInt64(6, page_subsecond_ticks)
            || !segment_insert.bindInt64(7, kImuHeaderSubsecondTicksPerSecond)
            || !segment_insert.bindInt64(8, header_flags)
            || !segment_insert.bindText(9, header_event.empty() ? "COLLECTION_START" : header_event)
            || !segment_insert.bindInt64(10, static_cast<sqlite3_int64>(first_sample_index))
            || !segment_insert.bindInt64(11, odr_hz)
            || !segment_insert.bindDouble(12, ctx.imu->clock_correction_ppm)
            || !segment_insert.stepDone())) {
        ctx.setLastSqliteError("IMUTag segment insert failed");
        return -2;
    }

    if (!temperature_insert.bindInt64(1, static_cast<sqlite3_int64>(ctx.imu->segment_id))
        || !temperature_insert.bindInt64(2, static_cast<sqlite3_int64>(first_sample_index))
        || !temperature_insert.bindInt64(3, nominalElapsedUs(first_sample_index, sample_period_us))
        || !temperature_insert.bindInt64(4, page_start_us)
        || !temperature_insert.bindDouble(5, page_temperature)
        || !temperature_insert.stepDone()) {
        ctx.setLastSqliteError("IMUTag pressure temperature insert failed");
        return -2;
    }

    if (header_resync
        && (!event_insert.bindInt64(1, page_start_us)
            || !event_insert.bindInt64(2, static_cast<sqlite3_int64>(ctx.imu->header_count))
            || !event_insert.bindText(3, header_storage_skip ? "RESYNC_STORAGE_SKIP" : header_event)
            || !event_insert.stepDone())) {
        ctx.setLastSqliteError("IMUTag resync event insert failed");
        return -2;
    }
    ctx.imu->header_count++;

    for (size_t frame_index = 0; frame_index < kImuSuperFramesPerPage; frame_index++) {
        const t_ImuTagSuperFrame &frame = page.frames[frame_index];
        if (isErasedSuperFrame(frame)) {
            break;
        }

        const sqlite3_int64 frame_start_us =
            ctx.imu->elapsed_base_us
            + correctedNominalElapsedUs(ctx.imu->segment_block_count,
                                        superframe_period_us,
                                        ctx.imu->clock_correction_factor);
        const uint64_t frame_sample_index =
            ctx.imu->segment_block_count * kImuSamplesPerSuperFrame;
        const sqlite3_int64 frame_raw_elapsed_us =
            nominalElapsedUs(frame_sample_index, sample_period_us);

        if (hasPressureSample(frame.aux)) {
            if (!pressure_insert.bindInt64(1, static_cast<sqlite3_int64>(ctx.imu->segment_id))
                || !pressure_insert.bindInt64(2, static_cast<sqlite3_int64>(frame_sample_index))
                || !pressure_insert.bindInt64(3, frame_raw_elapsed_us)
                || !pressure_insert.bindInt64(4, frame_start_us)
                || !pressure_insert.bindDouble(5, frame.aux.pressure)
                || !pressure_insert.stepDone()) {
                ctx.setLastSqliteError("IMUTag pressure insert failed");
                return -2;
            }
        }

        if (hasMagSample(frame.aux)) {
            if (!mag_insert.bindInt64(1, static_cast<sqlite3_int64>(ctx.imu->segment_id))
                || !mag_insert.bindInt64(2, static_cast<sqlite3_int64>(frame_sample_index))
                || !mag_insert.bindInt64(3, frame_raw_elapsed_us)
                || !mag_insert.bindInt64(4, frame_start_us)
                || !mag_insert.bindDouble(5, frame.aux.mag_x)
                || !mag_insert.bindDouble(6, frame.aux.mag_y)
                || !mag_insert.bindDouble(7, frame.aux.mag_z)
                || !mag_insert.stepDone()) {
                ctx.setLastSqliteError("IMUTag magnetometer insert failed");
                return -2;
            }
        }

        for (size_t sample = 0; sample < kImuSamplesPerSuperFrame; sample++) {
            const t_ImuTagImuSample &raw = frame.imu[sample];
            const uint64_t sample_index =
                ctx.imu->segment_block_count * kImuSamplesPerSuperFrame + sample;
            const sqlite3_int64 raw_elapsed_us =
                nominalElapsedUs(sample_index, sample_period_us);
            const sqlite3_int64 sample_elapsed_us =
                ctx.imu->elapsed_base_us
                + correctedNominalElapsedUs(sample_index,
                                            sample_period_us,
                                            ctx.imu->clock_correction_factor);

            if (!accel_insert.bindInt64(1, static_cast<sqlite3_int64>(ctx.imu->segment_id))
                || !accel_insert.bindInt64(2, static_cast<sqlite3_int64>(sample_index))
                || !accel_insert.bindInt64(3, raw_elapsed_us)
                || !accel_insert.bindInt64(4, sample_elapsed_us)
                || !accel_insert.bindDouble(5, raw.ax * accel_scale)
                || !accel_insert.bindDouble(6, raw.ay * accel_scale)
                || !accel_insert.bindDouble(7, raw.az * accel_scale)
                || !accel_insert.stepDone()
                || !gyro_insert.bindInt64(1, static_cast<sqlite3_int64>(ctx.imu->segment_id))
                || !gyro_insert.bindInt64(2, static_cast<sqlite3_int64>(sample_index))
                || !gyro_insert.bindInt64(3, raw_elapsed_us)
                || !gyro_insert.bindInt64(4, sample_elapsed_us)
                || !gyro_insert.bindDouble(5, raw.gx * gyro_scale)
                || !gyro_insert.bindDouble(6, raw.gy * gyro_scale)
                || !gyro_insert.bindDouble(7, raw.gz * gyro_scale)
                || !gyro_insert.stepDone()) {
                ctx.setLastSqliteError("IMUTag IMU sample insert failed");
                return -2;
            }
        }

        ctx.imu->block_count++;
        ctx.imu->segment_block_count++;
    }

    return 1;
}

} // namespace

int dumpIMUTagLog(WriterContext &ctx, const IMUTagLog &)
{
    ctx.setLastError("Legacy IMUTagLog protobuf is not supported; use IMUTagRawLog");
    return -2;
}

int dumpIMUTagRawLog(WriterContext &ctx, const IMUTagRawLog &log)
{
    const std::string &payload = log.samples();
    if (payload.size() != kImuPageBytes) {
        std::ostringstream error;
        error << "IMUTag raw page has " << payload.size()
              << " bytes, expected " << kImuPageBytes;
        ctx.setLastError(error.str());
        return -2;
    }

    t_ImuTagDataLog page;
    std::memcpy(&page, payload.data(), sizeof(page));

    return dumpIMUTagPage(ctx, page, static_cast<uint32_t>(log.millisecond()));
}

} // namespace tagcore::sqlite_log
