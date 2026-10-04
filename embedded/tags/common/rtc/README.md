---
type: readme
status: current
summary: Common RTC API and RV3028 descriptor binding, older RTC drivers, and the remaining descriptor-ownership todo.
---

# RTC Support

`rtc` contains the common real-time-clock interface and RTC chip drivers. The
active common RTC path is descriptor-driven: runtime code calls neutral helpers
from `rtc_api.h`, and the selected driver uses a `TagRtcDevice` descriptor for
register access and power/session callbacks.

## Files

- `inc/rtc_api.h`: public runtime API used by core code.
- `inc/rtc_device.h`, `src/rtc_device.c`: default device binding. This provides
  a weak RV3028 descriptor for the shared RTC software-I2C bus. Tags can
  override the descriptor and runtime initializer to use hardware I2C or a
  tag-local software-I2C fixture without changing the RV3028 register driver.
- `inc/rv3028.h`, `src/rtc_rv3028.c`: active RV3028 implementation.
- `src/rtc_test.c`: shared self-test hook.
- `src/hal_rtc_lld.c`: repo-local ChibiOS RTC low-level override. ChibiOS adds
  this basename itself, so the module supplies this directory early in `VPATH`.
- `rv3032` and `rv8803` files: older RTC implementations retained for tags or
  boards that may be revived later.

The IMUTag family overrides the weak binding with its own hardware-I2C
`TagRtcDevice` in `families/IMUTag/src/devices.c`. Open descriptor work is in
[the tag TODO](../../TODO.md#cleanup).
