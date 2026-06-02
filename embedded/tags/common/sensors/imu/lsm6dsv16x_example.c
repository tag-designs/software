/**
 * @file    lsm6dsv16x_example.c
 * @brief   Descriptor-based usage examples for the LSM6DSV16X driver.
 *
 * This file is documentation only; production tags provide the concrete
 * TagLsm6dsv16xDevice descriptor from their devices.c file.
 */

#include "lsm6dsv16x.h"

extern const TagLsm6dsv16xDevice tagExampleImuDevice;

static void platform_wait_int1(void) { }

void example_shutdown(void)
{
    lsm6dsv16x_init_shutdown(&tagExampleImuDevice);
}

void example_accel_only(void)
{
    int16_t ax, ay, az;
    lsm6dsv16x_accel_cfg_t accel_cfg = {
        .odr = LSM6DSV16X_XL_ODR_480Hz,
    };
    lsm6dsv16x_range_cfg_t range_cfg = {
        .xl_fs = LSM6DSV16X_XL_FS_4G,
        .g_fs = LSM6DSV16X_G_FS_2000DPS,
    };

    lsm6dsv16x_init_accel_only(&tagExampleImuDevice, &accel_cfg);
    lsm6dsv16x_set_ranges(&tagExampleImuDevice, &range_cfg);

    while (1) {
        if (lsm6dsv16x_read_accel(&tagExampleImuDevice, &ax, &ay, &az) == 0) {
            (void)ax;
            (void)ay;
            (void)az;
        }
    }
}

void example_accel_wakeup(void)
{
    lsm6dsv16x_wakeup_mode_cfg_t mode_cfg = {
        .odr = LSM6DSV16X_XL_ODR_15Hz,
    };
    lsm6dsv16x_motion_cfg_t motion_cfg = {
        .threshold = LSM6DSV16X_WK_THS_63mg,
        .wakeup_dur = LSM6DSV16X_WK_DUR_2,
        .sleep_dur = LSM6DSV16X_SLEEP_DUR_2,
    };

    lsm6dsv16x_init_accel_wakeup(&tagExampleImuDevice,
                                 &mode_cfg,
                                 &motion_cfg);

    while (1) {
        uint8_t src;

        platform_wait_int1();
        src = lsm6dsv16x_read_wakeup_src(&tagExampleImuDevice);
        if ((src & 0x10U) != 0U) {
            /* Wakeup event occurred. */
        }
    }
}

void example_accel_gyro_triggered(void)
{
    lsm6dsv16x_sample_t samples[64];
    lsm6dsv16x_trig_mode_cfg_t trig_cfg = {
        .trig_odr = LSM6DSV16X_TRIG_ODR_100HZ,
    };
    lsm6dsv16x_motion_cfg_t motion_cfg = {
        .threshold = LSM6DSV16X_WK_THS_125mg,
        .wakeup_dur = LSM6DSV16X_WK_DUR_2,
        .sleep_dur = LSM6DSV16X_SLEEP_DUR_4,
    };

    lsm6dsv16x_init_accel_gyro_triggered(&tagExampleImuDevice,
                                         &trig_cfg,
                                         &motion_cfg);
    lsm6dsv16x_set_fifo_watermark(&tagExampleImuDevice, 40U);

    while (1) {
        uint8_t wtm = 0;
        uint8_t ovr = 0;

        platform_wait_int1();
        (void)lsm6dsv16x_read_fifo_status(&tagExampleImuDevice, &wtm, &ovr);
        if (wtm != 0U) {
            (void)lsm6dsv16x_read_fifo(&tagExampleImuDevice, samples, 64U);
        }
    }
}

void example_query_trig_params(void)
{
    lsm6dsv16x_trig_params_t params;

    if (lsm6dsv16x_get_trig_params(LSM6DSV16X_TRIG_ODR_400HZ,
                                   &params) == 0) {
        (void)params;
    }
}

lsm6dsv16x_self_test_result_t example_self_test(void)
{
    return lsm6dsv16x_self_test_accel(&tagExampleImuDevice);
}
