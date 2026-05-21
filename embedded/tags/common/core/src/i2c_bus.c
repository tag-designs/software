#include "power.h"

#include "core_sync.h"
#include "gpio_utils.h"

/*
 * I2C controller setup, device bus-session pin state, and standby pull policy.
 * Register-oriented I2C transactions live with the other register adapters in
 * sensor_io.c.
 */

const TagI2cController tagI2c1DefaultController = {
  .driver = &I2CD1,
  .mutex = &I2Cmutex,
};

/*
 * Generic bus-device adapter.
 *
 * TagRegisterDevice stores a TagBusDevice so register drivers can open a bus
 * without knowing whether the concrete transport is SPI, USART, or I2C.
 */
static void tagI2cBusOpsPowerOn(const void *device)
{
  tagI2cDevicePowerOn((const TagI2cDevice *)device);
}

static void tagI2cBusOpsPowerOff(const void *device)
{
  tagI2cDevicePowerOff((const TagI2cDevice *)device);
}

static void tagI2cBusOpsBegin(const void *device)
{
  tagI2cBusBegin((const TagI2cDevice *)device);
}

static void tagI2cBusOpsEnd(const void *device)
{
  tagI2cBusEnd((const TagI2cDevice *)device);
}

static void tagI2cBusOpsPrepareSleep(const void *device)
{
  tagI2cDevicePrepareSleep((const TagI2cDevice *)device);
}

const TagBusOps tagI2cBusOps = {
    .power_on = tagI2cBusOpsPowerOn,
    .power_off = tagI2cBusOpsPowerOff,
    .bus_begin = tagI2cBusOpsBegin,
    .bus_end = tagI2cBusOpsEnd,
    .prepare_sleep = tagI2cBusOpsPrepareSleep,
};

/*
 * Controller and device lifecycle.
 *
 * I2C uses ChibiOS I2CConfig directly, so the active device descriptor carries
 * the config used by tagI2cBusBegin(). Power on/off only handles optional
 * switched device power; bus begin/end owns the mutex and controller state.
 */
void tagI2cControllerEnable(const TagI2cController *controller,
                            const I2CConfig *config)
{
  i2cStart(controller->driver, config);
}

void tagI2cControllerDisable(const TagI2cController *controller)
{
  i2cStop(controller->driver);
}

void tagI2cDevicePowerOn(const TagI2cDevice *device)
{
  if (tagLineIsValid(device->pwr))
  {
    toOutput(device->pwr);
    palSetLine(device->pwr);
  }
}

void tagI2cDevicePowerOff(const TagI2cDevice *device)
{
  if (tagLineIsValid(device->pwr))
  {
    palClearLine(device->pwr);
  }
}

void tagI2cBusBegin(const TagI2cDevice *device)
{
  const TagI2cController *controller = device->controller;

  if (controller && controller->mutex)
  {
    chBSemWait(controller->mutex);
  }

  palSetLine(device->sda);
  palSetLine(device->scl);
  toOutput(device->scl);
  toOutput(device->sda);

  if (controller)
  {
    tagI2cControllerEnable(controller, device->config);
  }
}

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

void tagI2cDevicePrepareSleep(const TagI2cDevice *device)
{
  switch (device->sleep_policy)
  {
  case TAG_I2C_SLEEP_PULLUP:
    tagEnableStandbyPullup(device->scl);
    tagEnableStandbyPullup(device->sda);
    tagEnableStandbyPulldown(device->pwr);
    break;

  case TAG_I2C_SLEEP_FLOAT:
    tagEnableStandbyPulldown(device->pwr);
    break;

  case TAG_I2C_SLEEP_CUSTOM:
    break;
  }
}
