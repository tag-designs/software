#ifndef TAG_CORE_DEVICE_H
#define TAG_CORE_DEVICE_H

#include <stddef.h>
#include <stdint.h>

/*
 * Generic runtime device adapter.
 *
 * Tag/family devices.c files own the concrete descriptors for the hardware on a
 * given board.  This adapter lets core code iterate over those devices without
 * knowing whether an entry is external flash, a sensor, an RTC, or another
 * board-level peripheral.
 */
typedef struct {
  const void *context;
  void (*on)(const void *context);
  void (*off)(const void *context);
  /*
   * State-aware device quiescing before standby.
   *
   * This hook is for device protocol work, such as putting external flash into
   * deep sleep for final states. It runs before the MCU standby pin pulls are
   * configured.
   */
  void (*prepare_standby)(const void *context, uint32_t state);
  /*
   * MCU standby pin configuration for this device.
   *
   * This hook should not talk to the device. It only configures the PWR standby
   * pullup/pulldown registers for the already-described device pins.
   */
  void (*apply_standby_pins)(const void *context);
} TagDevice;

typedef enum {
  TAG_DEVICE_PREPARE_STANDBY = 1u << 0,
  TAG_DEVICE_APPLY_STANDBY_PINS = 1u << 1,
} TagDeviceFlags;

typedef struct {
  const TagDevice *device;
  uint32_t flags;
} TagDeviceTableEntry;

extern const TagDeviceTableEntry tagDeviceTable[];
extern const size_t tagDeviceCount;

void tagDeviceOn(const TagDevice *device);
void tagDeviceOff(const TagDevice *device);
void tagDevicePrepareStandby(const TagDevice *device, uint32_t state);
void tagDeviceApplyStandbyPins(const TagDevice *device);
void tagDeviceTablePrepareStandby(uint32_t state);
void tagDeviceTableApplyStandbyPins(void);

#endif
