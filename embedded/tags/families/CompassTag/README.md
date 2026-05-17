# CompassTag Family

Shared application code for the `CompassTag`, `CompassTagAT25`, and
`CompassTagAT25Breakout` build variants lives here.

The variants currently share:

- configuration handling
- LIS2DU12 accelerometer support
- the LIS2DU12 self-test hook used by the shared test driver
- shared headers for logging, persistence, sensors, and configuration

The family does not override the shared test driver. Instead,
`lis2du12_test.c` supplies the `tag_test_lis2du12()` hook used by
`common/test/src/test.c`.

The variants still keep their own `custom.h`, `project.mk`, ChibiOS `cfg/`
files, and any source files that have intentionally diverged during board
bring-up.  In particular, the storage module choice stays in each variant's
`project.mk`: the original `CompassTag` uses MX25R flash, while the AT25
variants use AT25XE flash.  If a divergent source becomes common again, move it
here and remove the local copy from all variants.
