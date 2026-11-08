#---------------------------------------------------------------------------------
# BotW Unexplored overlay (Tesla / Ultrahand). Adapted from the EdiZon-Overlay Makefile.
# libultrahand is cloned into libs/libultrahand by the CI (tag v2.5.3).
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

APP_TITLE		:=	BotW Unexplored
APP_FILENAME	:=	ovlBotwUnexplored
APP_AUTHOR		:=	Zayr64, data and parser from sklart
APP_VERSION		:=	0.2.1-zayr

TARGET			:=	ovlBotwUnexplored
OUTDIR			:=	out
BUILD			:=	build
SOURCES			:=	source
INCLUDES		:=	include ../source
DATA			:=	data

include ${TOPDIR}/libs/libultrahand/ultrahand.mk

null			:=
SPACE			:=	$(null) $(null)

ARCH	:=	-march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS	:=	-g -Wall -O2 -ffunction-sections -fdata-sections -flto \
			-fomit-frame-pointer -fuse-linker-plugin -fno-strict-aliasing \
			$(ARCH) $(DEFINES)

UI_OVERRIDE_PATH := /config/botw-unexplored-ovl/
CFLAGS	+=	-DUI_OVERRIDE_PATH="\"$(UI_OVERRIDE_PATH)\""
CFLAGS	+=	-DUSE_EXCEPTION_WRAP=1
# Enables FullMode/HeaderOverlayFrame(title, subtitle) for the guide box (same as Status Monitor mini mode).
CFLAGS	+=	-DIS_STATUS_MONITOR_DIRECTIVE=1
LDFLAGS	+=	-Wl,-wrap,__cxa_throw \
			-Wl,-wrap,_Unwind_Resume \
			-Wl,-wrap,__gxx_personality_v0

CFLAGS	+=	$(INCLUDE) -D__SWITCH__ -D__OVERLAY__ -I$(PORTLIBS)/include/freetype2 -Wno-deprecated-declarations
CFLAGS	+=	-DAPP_VERSION=\"$(APP_VERSION)\" -DAPP_TITLE=\""$(APP_TITLE)"\" -DAPP_AUTHOR=\""$(APP_AUTHOR)"\"

CXXFLAGS	:=	$(CFLAGS) -fexceptions -std=c++26

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	+=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:=	-lnx
LIBDIRS	:=	$(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:=	$(CURDIR)/$(OUTDIR)/$(APP_FILENAME)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export LD	:=	$(CXX)

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES		:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export NROFLAGS += --nacp=$(OUTPUT).nacp

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@ $(BUILD) $(OUTDIR)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo " RM   " $(BUILD) $(OUTDIR)
	@rm -fr $(BUILD) $(OUTDIR)

else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

all	:	$(OUTPUT).ovl

$(OUTPUT).ovl	:	$(OUTPUT).nro
	@cp $(OUTPUT).nro $(OUTPUT).ovl
	@printf 'ULTR' >> $(OUTPUT).ovl
	@echo "Ultrahand signature has been added."

$(OUTPUT).nro	:	$(OUTPUT).nso $(OUTPUT).nacp
$(OUTPUT).nso	:	$(OUTPUT).elf
$(OUTPUT).elf	:	$(OFILES)

$(OFILES_SRC)	: $(HFILES_BIN)

%.bin.o	%_bin.h :	%.bin
	@echo " BIN  " $@
	@$(bin2o)

-include $(DEPENDS)

endif
