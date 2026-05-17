# CompassTag Family

Shared application code for the `CompassTag`, `CompassTagAT25`, and
`CompassTagAT25Breakout` build variants lives here.

The variants currently share:

- configuration handling
- LIS2DU12 accelerometer support
- shared headers for logging, persistence, sensors, and configuration

The family also supplies `test.c`, but the family manifest does not list it in
`ALLCSRC` because the `tag_test` module already adds the `test.c` basename.  The
common makefile's VPATH order lets this family copy override the common test
module for these variants.

The variants still keep their own `custom.h`, `project.mk`, ChibiOS `cfg/`
files, and any source files that have intentionally diverged during board
bring-up.  In particular, the storage module choice stays in each variant's
`project.mk`: the original `CompassTag` uses MX25R flash, while the AT25
variants use AT25XE flash.  If a divergent source becomes common again, move it
here and remove the local copy from all variants.
