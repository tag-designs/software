High Level
----------

Compass tag is designed to collect continuous directional and activity data with a 15 second sample rate.  The storage architecture is organized around 256B flash pages.  The external flash is 4 Mbytes or 16K pages.  Every page will be organized as follows

```
#include "assert.h"
#include "sensors.h"

#define DATALOG_SAMPLES 5
/*
  // defined in sensors.h
typedef struct {
    int16_t ax, ay, az, mx, my, mz;
} RawSensorData;
*/
   
typedef struct {
  uint16_t nDirty;
  struct { 
    RawSensorData sensors[4];
    uint16_t activity;
  } data[DATALOG_SAMPLES];
  uint16_t nValid;
} t_DataLog  __attribute__((aligned(256)));

static_assert(sizeof(t_DataLog) == 256, "t_DataLog size must be 256 bytes");
```

The nDirty entry indicates that this block is dirty (if 0) and the nValid (if 0) entry indicates that the block has been fully written.   The primary reasons for the the dirty entry is to optimize flash erase and to ensure that the data logs returned are valid.

Thus, a single page holds 5 minutes of data.

Prior to the start of every page, an 8 byte "page header" structure is written to internal flash.

```
typedef struct {
  int32_t epoch;    // current epoch
  uint16_t vdd100;  // voltage x 100
  uint16_t temp10;  // temperature C x 10
} t_DataHeader;
```

Error recovery is a particularly thorny problem because it is desirable to restart data collection.  The current log states can be read to help recover timestamps and indicate where to start collection again; however, reading external flash is expensive and should be avoided with the small batteries in use.  Thus, the simplest solution is to find the last page header that is written (we assume valid timestamps can't be -1 which is the default erased state of flash), and to simply abandon the last page -- updating the timestamp by 300 seconds.  In this instance, there will also be a entry in the tag error log.