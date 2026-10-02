# UIUCTag AT25XE external loaders for STM32CubeProgrammer and tag-xflash.
#
# Two images from one source directory: the read-only forensic loader, and the
# read-write loader (erase and program) for rescue and bench testing. Both
# serve UIUCTag.

add_embedded_loader(AT25XE_UIUCTag DISTRIBUTE)
add_embedded_loader(AT25XE_UIUCTag-RW
  DIRECTORY AT25XE_UIUCTag
  ALLOW_WRITE
  DISTRIBUTE)
