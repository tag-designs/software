#############################################################################
# Build global options
#############################################################################

ifeq ($(USE_OPT),)
  USE_OPT = -O2 -fomit-frame-pointer -falign-functions=16
endif

ifeq ($(USE_COPT),)
  USE_COPT = -IInc
endif

ifeq ($(USE_CPPOPT),)
  USE_CPPOPT = -fno-rtti
endif

ifeq ($(USE_LINK_GC),)
  USE_LINK_GC = yes
endif

ifeq ($(USE_LDOPT),)
  USE_LDOPT =
endif

ifeq ($(USE_LTO),)
  USE_LTO = yes
endif

ifeq ($(USE_THUMB),)
  USE_THUMB = yes
endif

ifeq ($(USE_VERBOSE_COMPILE),)
  USE_VERBOSE_COMPILE = no
endif

ifeq ($(USE_SMART_BUILD),)
  USE_SMART_BUILD = yes
endif

#############################################################################
# Architecture or project specific options
#############################################################################

ifeq ($(USE_PROCESS_STACKSIZE),)
  USE_PROCESS_STACKSIZE = 0x400
endif

ifeq ($(USE_EXCEPTIONS_STACKSIZE),)
  USE_EXCEPTIONS_STACKSIZE = 0x400
endif

ifeq ($(USE_FPU),)
  USE_FPU = softfp
endif

#############################################################################
# Project, sources and paths
#############################################################################

PROJECT = ch

MCU_FAMILY = STM32
MCU_SERIES = STM32L4xx
MCU_STARTUP = stm32l4xx
ARMV = 7
MCU_LDSCRIPT = STM32L432xC

CONFDIR = ./cfg

include $(CHIBIOS)/os/license/license.mk
include $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/mk/startup_stm32l4xx.mk
include $(CHIBIOS)/os/hal/hal.mk
include $(CHIBIOS)/os/hal/ports/STM32/STM32L4xx/platform_l432.mk
include $(CHIBIOS)/os/hal/osal/rt-nil/osal.mk
include $(CHIBIOS)/os/rt/rt.mk
include $(CHIBIOS)/os/common/ports/ARMv7-M/compilers/GCC/mk/port.mk
include $(CHIBIOS)/tools/mk/autobuild.mk

LDSCRIPT = $(STARTUPLD)/STM32L432xC.ld

include project.mk

CSRC = $(ALLCSRC)
CPPSRC =
ACSRC =
ACPPSRC =
TCSRC =
TCPPSRC =
ASMSRC =
ASMXSRC = $(STARTUPASM) $(PORTASM) $(OSALASM)

INCDIR = ./inc $(CONFDIR) $(ALLINC)

#############################################################################
# Compiler settings
#############################################################################

MCU = cortex-m4
TRGT = arm-none-eabi-
CC = $(TRGT)gcc
CPPC = $(TRGT)g++
LD = $(TRGT)gcc
CP = $(TRGT)objcopy
AS = $(TRGT)gcc -x assembler-with-cpp
AR = $(TRGT)ar
OD = $(TRGT)objdump
SZ = $(TRGT)size
HEX = $(CP) -O ihex
BIN = $(CP) -O binary

AOPT =
TOPT = -mthumb -DTHUMB
CWARN = -Wall -Wextra -Wundef -Wstrict-prototypes
CPPWARN = -Wall -Wextra -Wundef

#############################################################################
# Project options
#############################################################################

UDEFS = -DUSE_FULL_LL_DRIVER=1 -DPB_NO_PACKED_STRUCTS=1 -DPB_BUFFER_ONLY=1 \
        -DUSEEPRINTF=1 -DUSE_CDC_EPRINTF=1 \
        -DSWD_DELAY_COUNT=$(SWD_DELAY_COUNT) -DSWD_SPI_BR='$(SWD_SPI_BR)'
UADEFS =
UINCDIR = ../common/inc
ULIBDIR =
ULIBS =

RULESPATH = $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/mk
include $(RULESPATH)/arm-none-eabi.mk
include $(RULESPATH)/rules.mk

VPATH := $(BUILDDIR) ./src ../common/src $(VPATH)
