# BitTagNG Family

Shared application code for the `BitTagNG` and `BitTagNG-lis2du12` build
variants lives here.

The variants currently share ChibiOS configuration, application configuration,
logging type declarations, and persistent-state declarations.  They
intentionally keep separate local source for:

- accelerometer selection and driver support
- runtime `state_run.c`
- download/logging implementation details in `datalog.c`
- self-test behavior in `test.c`
- `custom.h` tag constants and board-specific aliases

If those local files converge later, move the common implementation here and
remove the duplicate variant copies.

Variant-local `cfg/` overrides are still supported for bring-up experiments:
the common makefile searches the variant `cfg/` directory before this family
`cfg/` directory.
