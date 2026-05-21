#include "lps.h"

void tagPressureDeviceBegin(const TagPressureDevice *device)
{
  tagBusPowerOn(device->registers->bus);
  tagRegisterBusBegin(device->registers);
}

void tagPressureDeviceEnd(const TagPressureDevice *device)
{
  tagRegisterBusEnd(device->registers);
  tagBusPowerOff(device->registers->bus);
}
