# Tag Designs Firmware: Licenses

The project's firmware source is Copyright (c) 2018-2026 The Trustees of
Indiana University and licensed under the [MIT License](LICENSE). Some
base-firmware files carry their own notice, "Copyright 2018 Geoffrey Brown,
Licensed under the Apache License, Version 2.0", and remain under that
license.

The firmware includes the third-party software listed below, each used under
its own license. Every component has a notice file linked in the first column.
That file gives its copyright, where it is used and where its source is, and
holds its full license text or names the shared text in [texts/](texts/).

## Firmware images are distributed under the GNU GPL, version 3

Every tag and base-board firmware image links the [ChibiOS/RT](third-party/chibios-rt.txt)
kernel, which is licensed under the GPL, version 3 only. The firmware images
(`.elf`, `.bin`, `.hex`) are therefore distributed under the
[GNU General Public License, version 3](texts/GPL-3.0-only.txt). The project's
own code in them remains available under the MIT License.

The external flash loaders contain no GPL code. They consist of the project's
MIT code, the Apache-2.0 [ChibiOS/HAL](third-party/chibios-hal.txt) and
[CMSIS](third-party/arm-cmsis.txt), and the ST device headers.

**Source code.** The complete corresponding source of every image is the Tag
Designs software repository, https://github.com/tag-designs/software, at the
release tag it was built from (`fw-vX.Y` for firmware, `vX.Y` for loaders).
The ChibiOS submodule is at the commit that tag records, which is also written
into each image's `*-build-manifest.json`; ChibiOS itself is at
https://github.com/ChibiOS/ChibiOS. The repository contains the build scripts,
linker scripts, and generated board and nanopb sources used to build the
images. This offer is valid for at least three years from the release date,
for anyone who receives the images.

## Third-party software

| Component | Version | License | Used in |
| --- | --- | --- | --- |
| [ChibiOS/RT kernel, OS library, Cortex-M ports](third-party/chibios-rt.txt) | 2021.11.5 | [GPL-3.0-only](texts/GPL-3.0-only.txt) | tag and base firmware |
| [ChibiOS/HAL, OSAL, STM32 ports, startup, streams](third-party/chibios-hal.txt) | 9.1.0 | [Apache-2.0](texts/Apache-2.0.txt) | all firmware and loaders |
| [Arm CMSIS Core(M)](third-party/arm-cmsis.txt) | 5 | [Apache-2.0](texts/Apache-2.0.txt) | all firmware and loaders |
| [ST CMSIS device headers, STM32L4xx](third-party/st-cmsis-l4.txt) | — | BSD-3-Clause | STM32L432 tags and loaders |
| [ST CMSIS device headers, STM32U3xx and STM32C0xx](third-party/st-cmsis-u3-c0.txt) | 1.2.0, 1.3.0 | [Apache-2.0](texts/Apache-2.0.txt) | STM32U375 tags and loaders; C071 base |
| [ST CMSIS device headers, STM32F0xx](third-party/st-cmsis-f0.txt) | — | BSD-3-Clause | F042 base |
| [nanopb runtime (pb_common.c, pb_decode.c, pb_encode.c)](third-party/nanopb.txt) | 0.4.9.1 | Zlib | tag firmware; tag-breakout-base-l432-u375-1v8 |
| [Bosch BMP5 SensorAPI](third-party/bosch-bmp5.txt) | 1.5.0 | BSD-3-Clause | UIUCTag, IMUTagNandBmp581 |
| [Bosch BMM350 SensorAPI (portions)](third-party/bosch-bmm350.txt) | 1.10.0 | BSD-3-Clause | IMUTagNandBmp581 |
| [stlink stlink.h](third-party/stlink-h.txt) | — | BSD-3-Clause | base firmware |
| [Arm debug_cm.h](third-party/arm-debug-cm.txt) | — | [Apache-2.0](texts/Apache-2.0.txt) | base firmware |
| [ST LL CRS headers](third-party/st-ll-crs.txt) | — | BSD-3-Clause | base firmware |
| [newlib (memcpy, memset, memmove, strncpy) and libgcc](third-party/newlib-libgcc.txt) | 14.2.Rel1 | BSD-3-Clause and newlib notice; GCC Runtime Library Exception | tag firmware |

## License texts

| Text | File |
| --- | --- |
| MIT License (this project) | [LICENSE](LICENSE) |
| GNU General Public License v3 | [texts/GPL-3.0-only.txt](texts/GPL-3.0-only.txt) |
| Apache License 2.0 | [texts/Apache-2.0.txt](texts/Apache-2.0.txt) |

The BSD and Zlib licenses are reproduced in full, with their copyright
notices, in each component's file.
