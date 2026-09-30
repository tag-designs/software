##############################################################################
# Shared build rules for STM32CubeProgrammer external loaders.
#
# A loader target mirrors a tag target: a Makefile that includes this file, a
# project.mk naming the board, part and MCU config, and optional ./cfg, ./inc
# and ./src directories whose files override same-named files in ../common.
#
# It differs from a tag in what it leaves out. There is no crt0, no vector
# table and no RT kernel: STM32CubeProgrammer downloads the linked image into
# SRAM and calls its entry points (Init, Read, SectorErase, MassErase, ...)
# directly, with interrupts disabled. ChibiOS supplies the register headers,
# the board description and PAL; the os-less OSAL takes the place of chconf.h
# with osalconf.h. Of the HAL sources, only the PAL low-level driver is
# compiled -- nothing else is called, and in particular halInit() and
# stm32_clock_init() must not be (see cfg/stm32l4/mcuconf.h).
#
# Variables supplied by the caller (CMake passes these; see
# add_embedded_loader() in embedded/CMakeLists.txt):
#   CHIBIOS             ChibiOS source tree.
#   BUILDDIR, DEPDIR    Output and dependency directories.
#   PROJECT             Image name; the loader is $(BUILDDIR)/$(PROJECT).stldr.
#   LOADER_ALLOW_WRITE  1 builds the read-write image (erase and program);
#                       0 (the default) builds the read-only forensic image.
#
# Variables supplied by project.mk:
#   LOADER_BOARD_INC    Directory holding the board's committed board.h.
#   LOADER_CSRC         Part-driver and board sources, as basenames.
##############################################################################

# Loaders are small and run once; favour size and plain symbols. LTO stays
# off so the entry-point symbols STM32CubeProgrammer looks up by name survive
# exactly as written.
ifeq ($(USE_OPT),)
  USE_OPT = -Os -fomit-frame-pointer
endif
ifeq ($(USE_COPT),)
  USE_COPT = -std=gnu11
endif
ifeq ($(USE_CPPOPT),)
  USE_CPPOPT =
endif
USE_LINK_GC = yes
USE_LTO = no
USE_THUMB = yes
USE_VERBOSE_COMPILE ?= no
USE_SMART_BUILD = yes

# The programmer does not enable the FPU (CPACR) before calling an entry
# point, so no floating-point instructions may be emitted.
USE_FPU = no

# Unused by a loader, which runs on the programmer's stack, but rules.mk
# expects them to be defined.
USE_PROCESS_STACKSIZE = 0
USE_EXCEPTIONS_STACKSIZE = 0

LOADER_ALLOW_WRITE ?= 0

PROJECT ?= loader
include project.mk

LOADER_COMMON_DIR ?= ../common
LOADER_MCU_CFG_DIR ?= $(LOADER_COMMON_DIR)/cfg/stm32l4

# The shared AT25XE command set lives beside the firmware driver so both use
# one copy of the opcodes and timing budgets.
TAG_STORAGE_INC_DIR ?= ../../tags/common/storage/inc

# Configuration lookup mirrors the tags: the target's ./cfg first, then the
# per-MCU defaults. HALCONFDIR must be a single directory because hal.mk reads
# halconf.h from it to decide which HAL sources the smart build selects.
LOADER_CFG_DIRS := ./cfg $(LOADER_MCU_CFG_DIR)
HALCONFDIR := $(firstword $(foreach dir,$(LOADER_CFG_DIRS),$(if $(wildcard $(dir)/halconf.h),$(dir))))
CONFDIR := $(HALCONFDIR)

include $(CHIBIOS)/os/license/license.mk
# Only STARTUPINC is used from the startup makefile: its crt0, crt1 and vector
# table are for a reset-started image, and a loader is not one.
include $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/mk/startup_stm32l4xx.mk
include $(CHIBIOS)/os/hal/hal.mk
include $(CHIBIOS)/os/hal/ports/STM32/STM32L4xx/platform_l432.mk
include $(CHIBIOS)/os/hal/osal/os-less/ARMCMx/osal.mk

LDSCRIPT = $(LOADER_COMMON_DIR)/STM32L432-loader.ld

# Shared loader sources, as basenames resolved through VPATH so a target's
# ./src can override any of them.
LOADER_COMMON_CSRC = \
       loader_entry.c \
       loader_clock.c \
       loader_delay.c \
       loader_spi.c

# PAL low-level driver: the one ChibiOS source a loader needs, for
# palSetLineMode().
LOADER_CHIBIOS_CSRC := $(filter %/hal_pal_lld.c,$(ALLCSRC))

CSRC = $(LOADER_CHIBIOS_CSRC) $(LOADER_COMMON_CSRC) $(LOADER_CSRC)
CPPSRC =
ACSRC =
ACPPSRC =
TCSRC =
TCPPSRC =
ASMSRC =
ASMXSRC =

INCDIR = $(LOADER_CFG_DIRS) ./inc $(LOADER_COMMON_DIR)/inc $(LOADER_BOARD_INC) \
         $(TAG_STORAGE_INC_DIR) $(ALLINC)

MCU  = cortex-m4

TRGT = arm-none-eabi-
CC   = $(TRGT)gcc
CPPC = $(TRGT)g++
LD   = $(TRGT)gcc
CP   = $(TRGT)objcopy
AS   = $(TRGT)gcc -x assembler-with-cpp
AR   = $(TRGT)ar
OD   = $(TRGT)objdump
SZ   = $(TRGT)size
HEX  = $(CP) -O ihex
BIN  = $(CP) -O binary

AOPT =
TOPT = -mthumb -DTHUMB

CWARN = -Wall -Wextra -Wundef -Wstrict-prototypes
CPPWARN = -Wall -Wextra -Wundef

UDEFS += -DLOADER_ALLOW_WRITE=$(LOADER_ALLOW_WRITE)
UADEFS =

# $(BUILDDIR)/.. holds the version.h written by CMake.
UINCDIR = $(BUILDDIR)/..
ULIBDIR =
ULIBS =

RULESPATH = $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/mk
include $(RULESPATH)/rules.mk

# rules.mk sorts the include and source search paths. Restore declared order,
# as the tags do, so ./cfg, ./inc and ./src override the shared defaults.
IINCDIR := $(patsubst %,-I%,$(INCDIR) $(DINCDIR) $(UINCDIR))
VPATH := ./src $(LOADER_COMMON_DIR)/src $(sort $(dir $(LOADER_CHIBIOS_CSRC)))

# The .stldr STM32CubeProgrammer loads is the linked ELF under another name.
POST_MAKE_ALL_RULE_HOOK: $(BUILDDIR)/$(PROJECT).stldr

$(BUILDDIR)/$(PROJECT).stldr: $(BUILDDIR)/$(PROJECT).elf
	@echo Creating $@
	@cp $< $@

print-% : ; @echo $* = $($*)
