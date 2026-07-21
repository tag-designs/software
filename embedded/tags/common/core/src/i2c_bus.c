/**
 * @file i2c_bus.c
 * @brief I2C controller setup, device bus sessions, and standby pin policy.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "power.h"

#include "gpio_utils.h"

#ifndef TAG_I2C_SOFTWARE_BUS_CLEAR_ON_BEGIN
#define TAG_I2C_SOFTWARE_BUS_CLEAR_ON_BEGIN 0
#endif

/** @name Controller and device lifecycle
 * Controller and device lifecycle.
 *
 * I2C uses a backend-tagged controller. Power on/off only handles optional
 * switched device power; bus begin/end owns the mutex, active pin mode, and
 * controller state.
 * @{
 */
/**
 * @brief Initialize the I2C driver object for a shared controller.
 *
 * @param[in] controller Shared controller whose driver object should be
 *            initialized before first use.
 */
void tagI2cControllerObjectInit(const TagI2cController *controller)
{
  if (controller)
  {
    switch (controller->backend)
    {
    case TAG_I2C_BACKEND_HARDWARE:
      i2cObjectInit(controller->driver.hardware);
      break;
    case TAG_I2C_BACKEND_SOFTWARE:
      tagSoftI2cObjectInit(controller->driver.software);
      break;
    }
  }
}

/**
 * @brief Start an I2C controller with the active device configuration.
 *
 * @param[in] controller Shared controller to enable.
 * @param[in] device Active device supplying the backend-specific config.
 */
void tagI2cControllerEnable(const TagI2cController *controller,
                            const TagI2cDevice *device)
{
  switch (controller->backend)
  {
  case TAG_I2C_BACKEND_HARDWARE:
    i2cStart(controller->driver.hardware, device->config.hardware);
    break;
  case TAG_I2C_BACKEND_SOFTWARE:
    tagSoftI2cStart(controller->driver.software, device->config.software);
    break;
  }
}

/**
 * @brief Stop an I2C controller after the active bus session ends.
 *
 * @param[in] controller Shared controller to disable.
 */
void tagI2cControllerDisable(const TagI2cController *controller)
{
  switch (controller->backend)
  {
  case TAG_I2C_BACKEND_HARDWARE:
    i2cStop(controller->driver.hardware);
    break;
  case TAG_I2C_BACKEND_SOFTWARE:
    tagSoftI2cStop(controller->driver.software);
    break;
  }
}

/**
 * @brief Assert a device's optional switched power line.
 *
 * @param[in] device I2C device descriptor whose power line should be enabled.
 */
void tagI2cDevicePowerOn(const TagI2cDevice *device)
{
  if (tagLineIsValid(device->pwr))
  { 
    palSetLine(device->pwr);
    palSetLineMode(device->pwr,
                   PAL_MODE_OUTPUT_PUSHPULL | PAL_STM32_OSPEED_LOWEST);
    palSetLine(device->sda);
    palSetLine(device->scl);
  }
}

/**
 * @brief Deassert a device's optional switched power line.
 *
 * @param[in] device I2C device descriptor whose power line should be disabled.
 */
void tagI2cDevicePowerOff(const TagI2cDevice *device)
{
  if (tagLineIsValid(device->pwr))
  {
    palSetLineMode(device->sda, PAL_MODE_INPUT_ANALOG);
    palSetLineMode(device->scl, PAL_MODE_INPUT_ANALOG);
    palClearLine(device->pwr);
  }
}

static uint8_t tagI2cAlternateFunction(const TagI2cDevice *device)
{
  if (device->alternate_function != 0U)
  {
    return device->alternate_function;
  }

  return TAG_I2C_DEFAULT_ALTERNATE_FUNCTION;
}

static void tagI2cApplyActivePins(const TagI2cDevice *device)
{
  const TagI2cController *controller = device->controller;

  if (!controller)
  {
    return;
  }

  switch (controller->backend)
  {
  case TAG_I2C_BACKEND_HARDWARE:
    palSetLineMode(device->sda,
                   PAL_MODE_ALTERNATE(tagI2cAlternateFunction(device)) |
                       PAL_STM32_OTYPE_OPENDRAIN | PAL_STM32_PUPDR_PULLUP |
                       PAL_STM32_OSPEED_LOWEST);
    palSetLineMode(device->scl,
                   PAL_MODE_ALTERNATE(tagI2cAlternateFunction(device)) |
                       PAL_STM32_OTYPE_OPENDRAIN | PAL_STM32_PUPDR_PULLUP |
                       PAL_STM32_OSPEED_LOWEST);
    break;

  case TAG_I2C_BACKEND_SOFTWARE:
    palSetLine(device->sda);
    palSetLine(device->scl);
    palSetLineMode(device->sda,
                   PAL_MODE_OUTPUT_OPENDRAIN | PAL_STM32_PUPDR_PULLUP);
    palSetLineMode(device->scl,
                   PAL_MODE_OUTPUT_OPENDRAIN | PAL_STM32_PUPDR_PULLUP);
    break;
  }
}

/**
 * @brief Claim the shared controller and start a transaction session.
 *
 * @param[in] device I2C device descriptor supplying the controller and config.
 */
void tagI2cBusBegin(const TagI2cDevice *device)
{
  const TagI2cController *controller = device->controller;

  if (controller && controller->mutex)
  {
    chBSemWait(controller->mutex);
  }

  tagI2cApplyActivePins(device);

#if TAG_I2C_SOFTWARE_BUS_CLEAR_ON_BEGIN
  if ((controller != NULL) &&
      (controller->backend == TAG_I2C_BACKEND_SOFTWARE) &&
      (device->config.software != NULL)) {
    tagSoftI2cBusClear(device->config.software);
  }
#endif

  if (controller)
  {
    tagI2cControllerEnable(controller, device);
  }
}

/**
 * @brief Stop a transaction session and release the shared controller.
 *
 * @param[in] device I2C device descriptor whose controller should be released.
 */
void tagI2cBusEnd(const TagI2cDevice *device)
{
  const TagI2cController *controller = device->controller;

  if (controller)
  {
    tagI2cControllerDisable(controller);
  }

  if (controller && controller->mutex)
  {
    chBSemSignal(controller->mutex);
  }
}

/**
 * @brief Perform one I2C write/read transaction through a device's backend.
 *
 * @param[in] device I2C device descriptor.
 * @param[in] address 7-bit or 10-bit I2C address, matching the backend config.
 * @param[in] txbuf Transmit buffer.
 * @param[in] txbytes Number of bytes to transmit.
 * @param[out] rxbuf Optional receive buffer.
 * @param[in] rxbytes Number of bytes to receive.
 * @param[in] timeout ChibiOS timeout interval.
 * @return MSG_OK on success or a bus error.
 */
msg_t tagI2cMasterTransmitTimeout(const TagI2cDevice *device,
                                  i2caddr_t address,
                                  const uint8_t *txbuf, size_t txbytes,
                                  uint8_t *rxbuf, size_t rxbytes,
                                  sysinterval_t timeout)
{
  const TagI2cController *controller = device->controller;

  if (!controller)
  {
    return MSG_RESET;
  }

  switch (controller->backend)
  {
  case TAG_I2C_BACKEND_HARDWARE:
    return i2cMasterTransmitTimeout(controller->driver.hardware, address,
                                    txbuf, txbytes, rxbuf, rxbytes, timeout);

  case TAG_I2C_BACKEND_SOFTWARE:
    return tagSoftI2cMasterTransmitTimeout(controller->driver.software, address,
                                           txbuf, txbytes, rxbuf, rxbytes,
                                           timeout);
  }

  return MSG_RESET;
}

/**
 * @brief Apply the standby pin policy for an I2C-backed device.
 *
 * @param[in] device I2C device descriptor whose sleep policy should be applied.
 */
void tagI2cDevicePrepareSleep(const TagI2cDevice *device)
{
  switch (device->sleep_policy)
  {
  case TAG_I2C_SLEEP_PULLUP:
    if (!tagLineIsValid(device->pwr)) {
      tagEnableStandbyPullup(device->scl);
      tagEnableStandbyPullup(device->sda);
    } else {
      tagEnableStandbyPulldown(device->pwr);
    }
    break;

  case TAG_I2C_SLEEP_FLOAT:
  
    if (tagLineIsValid(device->pwr)) {
      tagEnableStandbyPulldown(device->pwr);
    }
    break;

  case TAG_I2C_SLEEP_CUSTOM:
    break;
  }
}
/** @} */
