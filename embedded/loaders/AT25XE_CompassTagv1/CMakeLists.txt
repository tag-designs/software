# CompassTagv1 AT25XE external loaders for STM32CubeProgrammer and tag-xflash.
#
# Two images from one source directory: the read-only forensic loader, and the
# read-write loader (erase and program) for rescue and bench testing. Both
# serve CompassTagAT25 and CompassTagAT25Breakout.

add_embedded_loader(AT25XE_CompassTagv1 DISTRIBUTE)
add_embedded_loader(AT25XE_CompassTagv1-RW
  DIRECTORY AT25XE_CompassTagv1
  ALLOW_WRITE
  DISTRIBUTE)
