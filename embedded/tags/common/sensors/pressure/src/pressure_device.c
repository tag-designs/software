#include "lps.h"

void tagPressureDeviceBegin(const TagPressureDevice *device)
{
  tagBusPowerOn(&device->registers->bus);
  tagBusBegin(&device->registers->bus);
}

void tagPressureDeviceEnd(const TagPressureDevice *device)
{
  tagBusEnd(&device->registers->bus);
  tagBusPowerOff(&device->registers->bus);
}
