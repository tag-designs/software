##############################################################################

# Build global options
# NOTE: Can be overridden externally.
#
# Compiler options here.
ifeq ($(USE_OPT),)
  USE_OPT = -Os -ggdb  --std=gnu99 -fomit-frame-pointer -falign-functions=16 
endif


# C specific options here (added to USE_OPT).
ifeq ($(USE_COPT),)
  USE_COPT = -IInc --std=gnu99
endif

# C++ specific options here (added to USE_OPT).
ifeq ($(USE_CPPOPT),)
  USE_CPPOPT =
endif

# Enable this if you want the linker to remove unused code and data
ifeq ($(USE_LINK_GC),)
  USE_LINK_GC = yes
endif

# Linker extra options here.
ifeq ($(USE_LDOPT),)
  USE_LDOPT = 
endif

# Enable this if you want link time optimizations (LTO)
ifeq ($(USE_LTO),)
  USE_LTO = yes
endif

# If enabled, this option allows to compile the application in THUMB mode.
ifeq ($(USE_THUMB),)
  USE_THUMB = yes
endif

# Enable this if you want to see the full log while compiling.
ifeq ($(USE_VERBOSE_COMPILE),)
  USE_VERBOSE_COMPILE = no
endif

# If enabled, this option makes the build process faster by not compiling
# modules not used in the current configuration.
ifeq ($(USE_SMART_BUILD),)
  USE_SMART_BUILD = yes
endif

#
# Build global options
##############################################################################

##############################################################################
# Architecture or project specific options
#

# Stack size to be allocated to the Cortex-M process stack. This stack is
# the stack used by the main() thread.
ifeq ($(USE_PROCESS_STACKSIZE),)
  USE_PROCESS_STACKSIZE = 0x400
endif

# Stack size to the allocated to the Cortex-M main/exceptions stack. This
# stack is used for processing interrupts and exceptions.
ifeq ($(USE_EXCEPTIONS_STACKSIZE),)
  USE_EXCEPTIONS_STACKSIZE = 0x400
endif

# Enables the use of FPU (no, softfp, hard).
ifeq ($(USE_FPU),)
  USE_FPU = softfp
endif

#
# Architecture or project specific options
##############################################################################

##############################################################################
# Project, sources and paths
#

# Define project name here
PROJECT = ch
include project.mk
# Imported source files and paths
#CHIBIOS = ../../../../ChibiOS
# find halconf.h chconf.h mcuconf.h
CONFDIR = ./cfg
#../common/Inc
include $(CHIBIOS)/os/license/license.mk
# Startup files.
include $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/mk/startup_stm32l4xx.mk
# HAL-OSAL files (optional).
include $(CHIBIOS)/os/hal/hal.mk
include $(CHIBIOS)/os/hal/ports/STM32/STM32L4xx/platform_l432.mk

include $(CHIBIOS)/os/hal/osal/rt-nil/osal.mk
# RTOS files (optional).
include $(CHIBIOS)/os/nil/nil.mk
include $(CHIBIOS)/os/common/ports/ARMCMx/compilers/GCC/mk/port_v7m.mk
# Other files (optional).
#include $(CHIBIOS)/os/hal/lib/streams/streams.mk
include $(CHIBIOS)/tools/mk/autobuild.mk
# Define linker script file here
LDSCRIPT= ../common/STM32L432xC.ld
#$(STARTUPLD)/

# C sources that can be compiled in ARM or THUMB mode depending on the global
# setting.


CSRC = $(ALLCSRC)

# C++ sources that can be compiled in ARM or THUMB mode depending on the global
# setting.
CPPSRC =

# C sources to be compiled in ARM mode regardless of the global setting.
# NOTE: Mixing ARM and THUMB mode enables the -mthumb-interwork compiler
#       option that results in lower performance and larger code size.
ACSRC =

# C++ sources to be compiled in ARM mode regardless of the global setting.
# NOTE: Mixing ARM and THUMB mode enables the -mthumb-interwork compiler
#       option that results in lower performance and larger code size.
ACPPSRC =

# C sources to be compiled in THUMB mode regardless of the global setting.
# NOTE: Mixing ARM and THUMB mode enables the -mthumb-interwork compiler
#       option that results in lower performance and larger code size.
TCSRC =

# C sources to be compiled in THUMB mode regardless of the global setting.
# NOTE: Mixing ARM and THUMB mode enables the -mthumb-interwork compiler
#       option that results in lower performance and larger code size.
TCPPSRC =

# List ASM source files here
ASMSRC =
ASMXSRC = $(STARTUPASM) $(PORTASM) $(OSALASM)

INCDIR = $(CONFDIR) $(ALLINC) $(TESTINC)

#
# Project, sources and paths
##############################################################################

##############################################################################
# Compiler settings
#

MCU  = cortex-m4

#TRGT = arm-elf-
TRGT = arm-none-eabi-
CC   = $(TRGT)gcc
CPPC = $(TRGT)g++
# Enable loading with g++ only if you need C++ runtime support.
# NOTE: You can use C++ even without C++ support if you are careful. C++
#       runtime support makes code size explode.
LD   = $(TRGT)gcc
#LD   = $(TRGT)g++
CP   = $(TRGT)objcopy
AS   = $(TRGT)gcc -x assembler-with-cpp
AR   = $(TRGT)ar
OD   = $(TRGT)objdump
SZ   = $(TRGT)size
HEX  = $(CP) -O ihex
BIN  = $(CP) -O binary

# ARM-specific options here
AOPT =

# THUMB-specific options here
TOPT = -mthumb -DTHUMB

# Define C warning options here
CWARN = -Wall -Wextra -Wundef -Wstrict-prototypes

# Define C++ warning options here
CPPWARN = -Wall -Wextra -Wundef

#
# Compiler settings
##############################################################################

##############################################################################
# Start of user section
#

# List all user C define here, like -D_DEBUG=1

UDEFS += -DPB_NO_PACKED_STRUCTS=1 -DPB_BUFFER_ONLY=1 -DSOURCEDIR=$(SOURCEDIR) 
UDEFS += -DSTM32_I2C_USE_DMA=FALSE -DSTM32_SPI_DMA_REQUIRED=FALSE

# Define ASM defines here
UADEFS =  -DCRT0_INIT_STACKS=0 
# doesn't work -DCRT0_INIT_BSS=0


# List all user directories here

UINCDIR = $(NANOPBDIR) $(BUILDDIR) $(BUILDDIR)/.. 
UINCDIR += inc cfg ../common/inc ${MONITORINCDIR} $(PROTODIR)

# List the user directory to look for the libraries here

ULIBDIR =

# List all user libraries here

ULIBS =

#
# End of user defines
##############################################################################

RULESPATH = $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/mk
include $(RULESPATH)/rules.mk

VPATH := $(BUILDDIR) ./src ../common/src $(PROTODIR) $(NANOPBDIR) $(VPATH)

UNAME_S := $(shell uname -s)

# ifeq ($(UNAME_S),Darwin)
# 	OPENOCD := /usr/local/bin/openocd
# 	OCDSCRIPTS := /usr/local/share/openocd/scripts
# 	DFU	:= /usr/local/bin/dfu-util
# else
# 	OPENOCD := /usr/bin/openocd
# 	OCDSCRIPTS := /usr/share/openocd/scripts
# 	DFU	:= /usr/bin/dfu-util
# endif

# ifndef OPENOCDCFG
# OPENOCDCFG = stm32l4_test.cfg
# endif

# erase:
# 	$(OPENOCD) -s $(OCDSCRIPTS) -f $(OPENOCDCFG)  \
# 	    -c "init" -c "reset init" -c "stm32l4x mass_erase 0" -c "exit"	
# erasestlink:
# 	$(OPENOCD) -s $(OCDSCRIPTS) -f stm32l4.cfg  \
# 	    -c "init" -c "reset init" -c "stm32l4x mass_erase 0" -c "exit"	
# download: $(BUILDDIR)/ch.elf
# 	$(OPENOCD) -s $(OCDSCRIPTS) -f $(OPENOCDCFG)  \
# 	    -c "program $(BUILDDIR)/ch.elf verify reset exit" || true

# reset:
# 	$(OPENOCD) -f reset.cfg  || true

# downloadstlink: $(BUILDDIR)/ch.elf
# 	$(OPENOCD) -s $(OCDSCRIPTS) -f stm32l4.cfg  \
# 	    -c "program $(BUILDDIR)/ch.elf verify reset exit"

#$(BUILDDIR)/version.h: .PHONY

#.PHONY:
#	echo "#define VERSION_HASH \""$(shell git rev-parse --short HEAD)"\"" > $(BUILDDIR)/version.h
#	echo "#define GIT_REPO \""$(shell  git config --get remote.origin.url)"\"" >> $(BUILDDIR)/version.h

print-% : ; @echo $* = $($*)

PRE_MAKE_ALL_RULE_HOOK: $(BUILDDIR) #$(BUILDDIR)/version.h

