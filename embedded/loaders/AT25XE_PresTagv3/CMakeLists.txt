# PresTagv3 AT25XE external loaders for STM32CubeProgrammer.
#
# Two images from one source directory: the read-only forensic loader, and the
# read-write loader (erase and program) for rescue and bench testing. Both serve PresTag and PresTagRaw.

add_embedded_loader(AT25XE_PresTagv3 DISTRIBUTE)
add_embedded_loader(AT25XE_PresTagv3-RW
  DIRECTORY AT25XE_PresTagv3
  ALLOW_WRITE
  DISTRIBUTE)
