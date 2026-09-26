##############################################################################
# Common project definitions
#

MKFILE_PATH := $(realpath $(lastword $(MAKEFILE_LIST)))

PROJECT_ROOT ?= $(dir $(MKFILE_PATH))

PLUGIN_ROOT ?= $(realpath $(PROJECT_ROOT)/../..)
REPO_ROOT ?= $(realpath $(PLUGIN_ROOT)/../..)
SDK_ROOT ?= $(REPO_ROOT)/third_party/logue-sdk
PLATFORM_ROOT ?= $(SDK_ROOT)/platform/microkorg2

COMMON_INC_PATH ?= $(PLATFORM_ROOT)/common
COMMON_SRC_PATH ?= $(PLATFORM_ROOT)/common
INSTALLDIR ?= $(PROJECT_ROOT)
PLATFORM_COMMON_PATH ?= $(SDK_ROOT)/platform/common

GCC_BIN_PATH ?=
MK2_TOOLCHAIN ?= $(REPO_ROOT)/third_party/logue-sdk/tools/microkorg2-toolchain/arm-unknown-linux-gnueabihf/bin/arm-unknown-linux-gnueabihf-
ifneq ($(wildcard $(MK2_TOOLCHAIN)gcc),)
  CROSS_COMPILE ?= $(MK2_TOOLCHAIN)
else
  CROSS_COMPILE ?= $(GCC_BIN_PATH)/arm-none-eabi-
endif

include config.mk

PROJECT ?= tapeosc
TARGET_ARCH := armv7a
VERBOSE_COMPILE := no

DINCDIR := $(COMMON_INC_PATH)
PLATCOMMONDIR := $(PLATFORM_COMMON_PATH)

CSRC += $(realpath $(PLATFORM_COMMON_PATH)/_unit_base.c)

USE_CWARN ?= -W -Wall -Wextra
USE_CXXWARN ?= -W -Wall -Wextra -Wno-ignored-qualifiers

ifeq ($(USE_OPT),)
  USE_COPT = -pipe -ffast-math -fsigned-char -fno-stack-protector -fstrict-aliasing -falign-functions=16 -fno-math-errno
  USE_CXXOPT = -pipe -ffast-math -fsigned-char -fno-stack-protector -fstrict-aliasing -falign-functions=16 -fno-math-errno -fconcepts
endif

USE_COPT ?=
USE_CXXOPT ?=

ifneq ($(DEBUG),)
  USE_OPT += -ggdb3
  USE_LTO := no
  UDEFS += -DDEBUG
  ifeq ($(DEBUG_OPTIM),)
    USE_OPT += -Og
  else
    USE_OPT += $(DEBUG_OPTIM)
  endif
  USE_VECTORIZATION := no
else
  USE_COPT += -fomit-frame-pointer -finline-limit=9999999999 --param max-inline-insns-single=9999999999
  USE_CXXOPT += -fomit-frame-pointer -finline-limit=9999999999 --param max-inline-insns-single=9999999999 -fno-threadsafe-statics
  ifeq ($(OPTIM),)
    USE_OPT += -Os
  else
    USE_OPT += $(OPTIM)
  endif
  USE_VECTORIZATION := yes
endif

USE_LDOPT ?=
USE_LINK_GC := no
USE_LTO ?= yes

CC      := $(CROSS_COMPILE)gcc
CXXC    := $(CROSS_COMPILE)g++
LD      := $(CROSS_COMPILE)g++
AS      := $(CROSS_COMPILE)g++
AR      := $(CROSS_COMPILE)ar
CP      := $(CROSS_COMPILE)objcopy
OD      := $(CROSS_COMPILE)objdump
SZ      := $(CROSS_COMPILE)size
STRIP   := $(CROSS_COMPILE)strip
RM      := rm -f
HEX     := $(CP) -O ihex
BIN     := $(CP) -O binary

AOPT :=
CWARN := $(USE_CWARN)
CXXWARN := $(USE_CXXWARN)
OPT    := $(USE_OPT)
COPT   := $(USE_COPT)
CXXOPT := $(USE_CXXOPT)
LDOPT :=

ARCH_OPT := -march=armv7-a -mtune=cortex-a7 -marm
OPT += -mfloat-abi=hard -mfpu=neon-vfpv4
ifeq ($(USE_VECTORIZATION),yes)
  OPT += -mvectorize-with-neon-quad -ftree-vectorize
  OPT += -funsafe-math-optimizations
  DDEF += -D__NEON__
endif
DDEFS += -D__arm__ -D__cortex_a7__

COPT += -std=c11
CXXOPT += -std=gnu++14

BUILDDIR ?= build
OUTFILES := $(BUILDDIR)/$(PROJECT).mk2unit \
            $(BUILDDIR)/$(PROJECT).hex \
            $(BUILDDIR)/$(PROJECT).bin \
            $(BUILDDIR)/$(PROJECT).dmp \
            $(BUILDDIR)/$(PROJECT).list

SRC       := $(CSRC) $(CXXSRC)
SRCPATHS  := $(sort $(dir $(ASMXSRC)) $(dir $(ASMSRC)) $(dir $(SRC)))
OBJDIR    := $(BUILDDIR)/obj
LSTDIR    := $(BUILDDIR)/lst

COBJS     := $(addprefix $(OBJDIR)/, $(notdir $(CSRC:.c=.o)))
CXXOBJS   := $(addprefix $(OBJDIR)/, $(notdir $(CXXSRC:.cc=.o)))
ASMOBJS   := $(addprefix $(OBJDIR)/, $(notdir $(ASMSRC:.s=.o)))
ASMXOBJS  := $(addprefix $(OBJDIR)/, $(notdir $(ASMXSRC:.S=.o)))
OBJS      := $(ASMXOBJS) $(ASMOBJS) $(COBJS) $(CXXOBJS)

IINCDIR   := $(patsubst %,-I%,$(INCDIR) $(DINCDIR) $(UINCDIR) $(PLATCOMMONDIR))
LLIBDIR   := $(patsubst %,-L%,$(DLIBDIR) $(ULIBDIR))
DEFS      := $(DDEFS) $(UDEFS)
LIBS      := $(DLIBS) $(ULIBS)

MCFLAGS   := $(ARCH_OPT)
CFLAGS    = $(MCFLAGS) $(OPT) $(COPT) $(CWARN) -fPIC -Wa,-alms=$(LSTDIR)/$(notdir $(<:.c=.lst)) $(DEFS)
CXXFLAGS  = $(MCFLAGS) $(OPT) $(CXXOPT) $(CXXWARN) -fPIC -Wa,-alms=$(LSTDIR)/$(notdir $(<:.cc=.lst)) $(DEFS)
LDFLAGS   := $(MCFLAGS) $(OPT) $(LLIBDIR) -shared -Wl,-Map=$(BUILDDIR)/$(PROJECT).map,--cref$(LDOPT)

ifeq ($(USE_LTO),yes)
  OPT += -flto
endif

VPATH := $(SRCPATHS)

.PHONY: all install clean wasm-ci wasm

all: $(OBJS) $(OUTFILES)
	@echo Done

$(OBJS): | $(BUILDDIR) $(OBJDIR) $(LSTDIR)

$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(LSTDIR):
	@mkdir -p $(LSTDIR)

$(CXXOBJS) : $(OBJDIR)/%.o : %.cc Makefile
	@echo Compiling $(<F)
	@$(CXXC) -c $(CXXFLAGS) $(AOPT) -I. $(IINCDIR) $< -o $@

$(COBJS) : $(OBJDIR)/%.o : %.c Makefile
	@echo Compiling $(<F)
	@$(CC) -c $(CFLAGS) $(AOPT) -I. $(IINCDIR) $< -o $@

$(BUILDDIR)/$(PROJECT).mk2unit: $(OBJS)
	@echo Linking $@
	@$(LD) $(OBJS) $(LDFLAGS) $(LIBS) -o $@
	@$(STRIP) $@

%.hex: %.mk2unit
	@echo Creating $@
	@$(HEX) $< $@

%.bin: %.mk2unit
	@echo Creating $@
	@$(BIN) $< $@

%.dmp: %.mk2unit
	@echo Creating $@
	@$(OD) -x --syms $< > $@
	@$(SZ) $<

%.list: %.mk2unit
	@echo Creating $@
	@$(OD) -S $< > $@

install: $(BUILDDIR)/$(PROJECT).mk2unit
	@echo Deploying to $(INSTALLDIR)/$(PROJECT).mk2unit
	@mv $(BUILDDIR)/$(PROJECT).mk2unit $(INSTALLDIR)/$(PROJECT).mk2unit
	@echo Done

clean:
	@echo Cleaning
	-rm -fR $(BUILDDIR) $(PROJECT_ROOT)/$(PROJECT).mk2unit
	@echo Done

wasm-ci:
	@echo "Wasm build not supported for microkorg2 target"

wasm: wasm-ci
