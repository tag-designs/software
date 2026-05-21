#ifndef TAG_CORE_BUS_DEVICE_H
#define TAG_CORE_BUS_DEVICE_H

/*
 * Generic bus-session descriptor.
 *
 * Tag and family device files still describe concrete SPI, USART, or I2C
 * devices with the typed bus descriptors. This thin wrapper lets sensor and
 * storage drivers call power/session operations without either guessing the
 * bus type from a register adapter or requiring tag-local no-argument wrapper
 * functions.
 */
typedef struct {
  void (*power_on)(const void *device);
  void (*power_off)(const void *device);
  void (*bus_begin)(const void *device);
  void (*bus_end)(const void *device);
  void (*prepare_sleep)(const void *device);
} TagBusOps;

typedef struct {
  const TagBusOps *ops;
  const void *device;
} TagBusDevice;

static inline void tagBusPowerOn(const TagBusDevice *bus)
{
  if (bus && bus->ops && bus->ops->power_on)
    bus->ops->power_on(bus->device);
}

static inline void tagBusPowerOff(const TagBusDevice *bus)
{
  if (bus && bus->ops && bus->ops->power_off)
    bus->ops->power_off(bus->device);
}

static inline void tagBusBegin(const TagBusDevice *bus)
{
  if (bus && bus->ops && bus->ops->bus_begin)
    bus->ops->bus_begin(bus->device);
}

static inline void tagBusEnd(const TagBusDevice *bus)
{
  if (bus && bus->ops && bus->ops->bus_end)
    bus->ops->bus_end(bus->device);
}

static inline void tagBusPrepareSleep(const TagBusDevice *bus)
{
  if (bus && bus->ops && bus->ops->prepare_sleep)
    bus->ops->prepare_sleep(bus->device);
}

#endif
