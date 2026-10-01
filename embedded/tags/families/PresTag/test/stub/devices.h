#ifndef STUB_DEVICES_H
#define STUB_DEVICES_H
typedef struct { int unused; } TagStorageDevice;
typedef struct { int unused; } TagRegisterDevice;
extern const TagStorageDevice tagExternalFlash;
extern const TagRegisterDevice tagPressureDevice;
#define TAG_EXTERNAL_FLASH (&tagExternalFlash)
#define TAG_PRESSURE_DEVICE (&tagPressureDevice)
#endif
