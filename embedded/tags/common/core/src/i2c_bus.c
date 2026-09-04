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

#if TAG_I2C_BUS_CLEAR
/**
 * @brief Half-period spacing for bus-clear clocks, comfortably under 100 kHz.
 *
 * @details A busy loop rather than a sleep: recovery runs before the scheduler
 *          on the startup path, and with the controller mutex held elsewhere.
 */
static void tagI2cBusClearDelay(void)
{
  for (volatile uint32_t i = 0U; i < 400U; i++) {
    __NOP();
  }
}

/**
 * @brief Clock a stuck slave off SDA and return the bus to idle.
 *
 * @details Hardware-backend sequence. The pins are handed to GPIO because a
 *          wedged peripheral cannot generate START/STOP itself, and the
 *          peripheral is reset afterwards because it latches BUSY from the bus
 *          independently of what the pins now read.
 *
 * @param[in] device Device whose board lines describe the bus.
 * @pre The controller must be disabled.
 * @post Pins are returned to their active alternate-function mode.
 */
static void tagI2cBusClearHardware(const TagI2cDevice *device)
{
  const TagI2cController *controller = device->controller;

  palSetLineMode(device->scl,
                 PAL_MODE_OUTPUT_OPENDRAIN | PAL_STM32_OSPEED_LOWEST);
  palSetLine(device->scl);
  tagI2cBusClearDelay();

  for (unsigned i = 0U; (i < 9U) && (palReadLine(device->sda) == PAL_LOW);
       i++) {
    palClearLine(device->scl);
    tagI2cBusClearDelay();
    palSetLine(device->scl);
    tagI2cBusClearDelay();
  }

  /* STOP: release SDA while SCL is high, so every slave sees a clean idle. */
  palSetLineMode(device->sda,
                 PAL_MODE_OUTPUT_OPENDRAIN | PAL_STM32_OSPEED_LOWEST);
  palClearLine(device->sda);
  tagI2cBusClearDelay();
  palSetLine(device->scl);
  tagI2cBusClearDelay();
  palSetLine(device->sda);
  tagI2cBusClearDelay();

  if (controller->reset) {
    controller->reset();
  }

  /*
   * Leave the pins as released open-drain outputs, NOT in alternate function.
   * Only tagI2cBusBegin() follows this with a controller start; at the other
   * call sites the peripheral stays disabled, and an AF pin with no peripheral
   * driving it is held low. The board pulls SCL and SDA up with 4.7k, so a
   * line parked low sinks about 700 uA -- which is exactly what an earlier
   * version of this cost at idle, 1031 uA against 4.09 uA. Released
   * open-drain against those pull-ups is the correct passive idle state and
   * draws nothing.
   */
}
#endif

#if TAG_I2C_BUS_CLEAR
bool tagI2cBusClearIfStuck(const TagI2cDevice *device)
{
  const TagI2cController *controller;

  if (device == NULL) {
    return false;
  }
  controller = device->controller;
  if (controller == NULL) {
    return false;
  }

  /*
   * Never drive an unpowered part. tagI2cDevicePowerOff() deliberately parks
   * SDA and SCL as analog inputs for exactly this reason, and clocking a
   * device whose supply is down injects current through its protection diodes.
   */
  if (tagLineIsValid(device->pwr) && (palReadLine(device->pwr) == PAL_LOW)) {
    return false;
  }

  /* Nothing to do unless a slave is actually holding the bus. */
  if (palReadLine(device->sda) != PAL_LOW) {
    return false;
  }

  switch (controller->backend) {
  case TAG_I2C_BACKEND_HARDWARE:
    tagI2cBusClearHardware(device);
    return true;

  case TAG_I2C_BACKEND_SOFTWARE:
    if (device->config.software != NULL) {
      tagSoftI2cBusClear(device->config.software);
      return true;
    }
    return false;
  }

  return false;
}
#endif

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

#if TAG_I2C_BUS_CLEAR
  /*
   * Recover before the pins are handed to the peripheral and before it is
   * enabled: a controller started against a bus that is not idle latches BUSY
   * and stays stuck. The clear leaves the pins released rather than in
   * alternate function, so applying the active pin mode must follow it.
   */
  (void)tagI2cBusClearIfStuck(device);
#endif

  tagI2cApplyActivePins(device);
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

  /*
   * Deliberately NO bus clear here. Clearing on the way out cost 1 mA at idle:
   * tagI2cBusClearHardware() has to drive SDA and SCL directly, so it leaves
   * them as plain open-drain GPIO outputs, and at this call site nothing ever
   * restores them -- only tagI2cBusBegin() follows a clear with
   * tagI2cApplyActivePins(). A write that ends with a slave holding SDA is
   * exactly when the clear fires, so setting the clock parked the pins as GPIO
   * into standby and the tag drew about 1 mA instead of 5 uA until the next
   * reset. Reads never tripped it, which is why it looked like a fault in the
   * RTC write path.
   *
   * Nothing is lost by dropping it. The clear is already performed where it
   * can be followed by a correct pin state: tagI2cBusBegin() clears before
   * enabling the controller, so the next user of this bus recovers it, and
   * tagRtcDeviceRuntimeInit() clears at boot, so a slave left holding SDA
   * across standby is recovered on the next startup.
   */

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
