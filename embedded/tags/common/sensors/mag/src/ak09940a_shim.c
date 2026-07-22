/**
 * @file ak09940a_shim.c
 * @brief Default-board binding for legacy AK09940A magnetometer entry points.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"
#include "custom.h"
#include "gpio_utils.h"
#include "power.h"
#include "ak09940a.h"

/*
 * Default AK09940A binding for the legacy tag firmware API.
 *
 * The driver in ak09940a.c is parameterized by TagRegisterDevice and only knows
 * about AK09940A register sequences. This shim is the one place that maps the
 * historical `mag*` and `ak09940_*` entry points onto a tag-provided
 * SPI/register descriptor. When a board still exposes the standard LINE_MAG_*
 * names, this shim also provides a weak default descriptor. Tags with mixed or
 * shared bus wiring should provide a strong tagAk09940aDevice()
 * implementation from devices.c instead.
 *
 * TODO: Convert remaining legacy AK09940A users to the TagRegisterDevice-based
 * API and remove this compatibility shim from the common magnetometer module.
 */

#if defined(LINE_MAG_CS)
#define AK09940A_DEFAULT_CS LINE_MAG_CS
#elif defined(LINE_AK_CS)
#define AK09940A_DEFAULT_CS LINE_AK_CS
#endif

#if defined(LINE_MAG_SCK) && defined(LINE_MAG_MISO) && defined(LINE_MAG_MOSI)
#define AK09940A_DEFAULT_SCK LINE_MAG_SCK
#define AK09940A_DEFAULT_MISO LINE_MAG_MISO
#define AK09940A_DEFAULT_MOSI LINE_MAG_MOSI
#endif

#if defined(AK09940A_DEFAULT_CS) && defined(AK09940A_DEFAULT_SCK) && \
    defined(AK09940A_DEFAULT_MISO) && defined(AK09940A_DEFAULT_MOSI)
#define AK09940A_HAS_DEFAULT_DEVICE 1
#endif

#if defined(LINE_MAG_TRG)
#define AK09940A_DEFAULT_TRG LINE_MAG_TRG
#define AK09940A_HAS_DEFAULT_TRG 1
#elif defined(LINE_AK_TRG)
#define AK09940A_DEFAULT_TRG LINE_AK_TRG
#define AK09940A_HAS_DEFAULT_TRG 1
#endif

#if defined(AK09940A_HAS_DEFAULT_DEVICE) && AK09940A_HAS_DEFAULT_DEVICE
static const TagRegisterDevice ak09940a_registers = {
  .kind = TAG_REGISTER_ST,
  .bus = TAG_BUS_SPI_INIT(
    TAG_SPI1_DEVICE_DEFAULTS(AK09940A_DEFAULT_CS),
    //.cs = AK09940A_DEFAULT_CS,
    .sck = AK09940A_DEFAULT_SCK,
    .miso = AK09940A_DEFAULT_MISO,
    .mosi = AK09940A_DEFAULT_MOSI,
    .pwr = TAG_NO_LINE,
    .dummy = 0xff,
    .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE),
  .read_mask = 0x80,
  .write_mask = 0x00,
};
#endif

#if defined(AK09940A_HAS_DEFAULT_TRG) && AK09940A_HAS_DEFAULT_TRG
/**
 * @brief Configure the default trigger pin direction for external triggering.
 *
 * @param[in] output true to drive the trigger pin, false to release it as input.
 */
static void ak09940a_default_trigger_mode(bool output)
{
  if (output)
    toOutput(AK09940A_DEFAULT_TRG);
  else
    toInput(AK09940A_DEFAULT_TRG);
}

/**
 * @brief Emit the short pulse used to trigger one AK09940A conversion.
 */
static void ak09940a_default_trigger(void)
{
  palSetLine(AK09940A_DEFAULT_TRG);
  palClearLine(AK09940A_DEFAULT_TRG);
}

/**
 * @brief Read the default AK09940A data-ready line.
 *
 * @return true when the pin is high.
 */
static bool ak09940a_default_data_ready_line(void)
{
  return palReadLine(AK09940A_DEFAULT_TRG) == PAL_HIGH;
}
#endif

/**
 * @brief Return the default AK09940A register descriptor for legacy callers.
 *
 * @return Default magnetometer register descriptor. Tag families may override
 *         this weak binding with a board-specific descriptor.
 */
#if defined(AK09940A_HAS_DEFAULT_DEVICE) && AK09940A_HAS_DEFAULT_DEVICE
const TagRegisterDevice *__attribute__((weak)) tagAk09940aDevice(void)
{
  return &ak09940a_registers;
}
#endif

/**
 * @brief Sample the default AK09940A device through the legacy API.
 *
 * @param[in] single true to request a single conversion first.
 * @param[out] xyz Destination for the raw 3-byte-per-axis sample.
 * @return true when a sample was read.
 */
bool magSample(bool single, uint8_t *xyz)
{
  return ak09940aSample(tagAk09940aDevice(), single, xyz);
}

/**
 * @brief Initialize the default AK09940A through the legacy API.
 *
 * @param[in] mode Legacy mode selector.
 */
void magInit(ak09940_mode_t mode)
{
  ak09940aInit(tagAk09940aDevice(), mode);
}

/**
 * @brief Run the default AK09940A presence test through the legacy API.
 *
 * @return true when the expected identity registers are present.
 */
bool magTest(void)
{
  return ak09940aTest(tagAk09940aDevice());
}

/**
 * @brief Check the default AK09940A WHO_AM_I registers.
 *
 * @return true when the expected device identity is present.
 */
bool ak09940_check_whoami(void)
{
  return ak09940aCheckWhoami(tagAk09940aDevice());
}

/**
 * @brief Put the default AK09940A into power-down mode.
 *
 * @return MSG_OK on success or a register-transport error.
 */
msg_t ak09940_init_power_down(void)
{
  return ak09940aInitPowerDown(tagAk09940aDevice());
}

/**
 * @brief Configure the default AK09940A for continuous sampling.
 *
 * @param[in] rate Output sample rate.
 * @param[in] drive Noise/power drive selection.
 * @param[in] temp_mode Temperature channel enable state.
 * @return MSG_OK on success or a register-transport error.
 */
msg_t ak09940_init_continuous(ak09940_rate_t rate, ak09940_drive_t drive,
                              ak09940_temp_mode_t temp_mode)
{
#if defined(AK09940A_HAS_DEFAULT_TRG) && AK09940A_HAS_DEFAULT_TRG
  ak09940a_default_trigger_mode(false);
#endif
  return ak09940aInitContinuous(tagAk09940aDevice(), rate, drive,
                                temp_mode);
}

/**
 * @brief Configure the default AK09940A for externally triggered sampling.
 *
 * @param[in] drive Noise/power drive selection.
 * @param[in] temp_mode Temperature channel enable state.
 * @return MSG_OK on success or a register-transport error.
 */
msg_t ak09940_init_triggered(ak09940_drive_t drive,
                             ak09940_temp_mode_t temp_mode)
{
#if defined(AK09940A_HAS_DEFAULT_TRG) && AK09940A_HAS_DEFAULT_TRG
  palClearLine(AK09940A_DEFAULT_TRG);
  ak09940a_default_trigger_mode(true);
#endif
  return ak09940aInitTriggered(tagAk09940aDevice(), drive, temp_mode);
}

/**
 * @brief Trigger one conversion on the default AK09940A.
 *
 * @return MSG_OK when the trigger callback is available and used.
 */
msg_t ak09940_trigger(void)
{
#if defined(AK09940A_HAS_DEFAULT_TRG) && AK09940A_HAS_DEFAULT_TRG
  ak09940a_default_trigger();
  return MSG_OK;
#else
  return MSG_RESET;
#endif
}

/**
 * @brief Report whether default AK09940A sample data is ready.
 *
 * @param[in] is_continuous true when using continuous mode status semantics.
 * @return true when data can be read.
 */
bool ak09940_data_ready(bool is_continuous)
{
#if defined(AK09940A_HAS_DEFAULT_TRG) && AK09940A_HAS_DEFAULT_TRG
  if (is_continuous)
    return ak09940a_default_data_ready_line();
#endif
  return ak09940aDataReady(tagAk09940aDevice(), is_continuous);
}

/**
 * @brief Read one raw sample from the default AK09940A.
 *
 * @param[out] mx_raw X-axis magnetic sample.
 * @param[out] my_raw Y-axis magnetic sample.
 * @param[out] mz_raw Z-axis magnetic sample.
 * @param[out] temp_raw Optional raw temperature sample.
 * @return true when the sample registers were read and valid.
 */
bool ak09940_read_sample(int32_t *mx_raw, int32_t *my_raw, int32_t *mz_raw,
                         int16_t *temp_raw)
{
  return ak09940aReadSample(tagAk09940aDevice(), mx_raw, my_raw, mz_raw,
                            temp_raw);
}

/**
 * @brief Run the default AK09940A self-test.
 *
 * @return true when self-test results are inside the expected range.
 */
bool ak09940_self_test(void)
{
  return ak09940aSelfTest(tagAk09940aDevice());
}
