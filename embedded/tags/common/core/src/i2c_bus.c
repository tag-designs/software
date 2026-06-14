/**
 * @file i2c_bus.c
 * @brief I2C controller setup, device bus sessions, and standby pin policy.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "power.h"

#include "gpio_utils.h"

/** @name Controller and device lifecycle
 * Controller and device lifecycle.
 *
 * I2C uses ChibiOS I2CConfig directly, so the active device descriptor carries
 * the config used by tagI2cBusBegin(). Power on/off only handles optional
 * switched device power; bus begin/end owns the mutex and controller state.
 * @{
 */
/**
 * @brief Initialize the ChibiOS I2C driver object for a shared controller.
 *
 * @param[in] controller Shared controller whose driver object should be
 *            initialized before first use.
 */
void tagI2cControllerObjectInit(const TagI2cController *controller)
{
  if (controller)
  {
    i2cObjectInit(controller->driver);
  }
}

/**
 * @brief Start an I2C controller with the active device configuration.
 *
 * @param[in] controller Shared controller to enable.
 * @param[in] config ChibiOS bus timing and line-behavior configuration.
 */
void tagI2cControllerEnable(const TagI2cController *controller,
                            const I2CConfig *config)
{
  i2cStart(controller->driver, config);
}

/**
 * @brief Stop an I2C controller after the active bus session ends.
 *
 * @param[in] controller Shared controller to disable.
 */
void tagI2cControllerDisable(const TagI2cController *controller)
{
  i2cStop(controller->driver);
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
    palSetLineMode(device->pwr, PAL_MODE_OUTPUT_PUSHPULL| PAL_STM32_OSPEED_LOWEST);
    palSetLine(device->sda);
    palSetLine(device->scl);
    palSetLineMode(device->scl,PAL_MODE_OUTPUT_OPENDRAIN | PAL_STM32_PUPDR_PULLUP);
    palSetLineMode(device->sda,PAL_MODE_OUTPUT_OPENDRAIN | PAL_STM32_PUPDR_PULLUP);
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
    palSetLineMode(device->sda,PAL_MODE_INPUT_ANALOG );
    palSetLineMode(device->scl,PAL_MODE_INPUT_ANALOG );
    palClearLine(device->pwr);
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

  palSetLineMode(device->sda,PAL_MODE_OUTPUT_OPENDRAIN | PAL_STM32_PUPDR_PULLUP);
  palSetLineMode(device->scl,PAL_MODE_OUTPUT_OPENDRAIN | PAL_STM32_PUPDR_PULLUP);

  if (controller)
  {
    tagI2cControllerEnable(controller, device->config);
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
