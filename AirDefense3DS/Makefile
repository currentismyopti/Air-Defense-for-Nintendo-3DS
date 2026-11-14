#---------------------------------------------------------------------------------
# Air Defense Command for Nintendo 3DS
#   make        -> AirDefense.3dsx (Homebrew Launcher)
#   make cia    -> AirDefense.cia  (install with FBI; needs makerom and bannertool on PATH or in tools/bin)
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET          := AirDefense
BUILD           := build
SOURCES         := src
INCLUDES        := src
APP_TITLE       := Air Defense Command
APP_DESCRIPTION := Hold the line. Own the sky.
APP_AUTHOR      := Homebrew
ICON            := meta/icon.png

ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS   := -g -Wall -O2 -mword-relocations -ffunction-sections -fno-math-errno $(ARCH)
CFLAGS   += $(INCLUDE) -D__3DS__ -std=gnu11
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS     := -lcitro2d -lcitro3d -lctru -lm
LIBDIRS  := $(CTRULIB)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
export LD := $(CC)
export OFILES   := $(CFILES:.c=.o)
export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export APP_ICON := $(TOPDIR)/$(ICON)
export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh

# makerom and bannertool: use the copies fetched by tools/get_tools.py if they are there
export PATH := $(CURDIR)/tools/bin:$(PATH)

.PHONY: all clean cia

all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

cia: all
	bannertool makebanner -i meta/banner.png -a meta/banner.wav -o $(BUILD)/banner.bnr
	makerom -f cia -o $(TARGET).cia -elf $(TARGET).elf -rsf meta/app.rsf -target t -exefslogo \
	        -icon $(TARGET).smdh -banner $(BUILD)/banner.bnr
	@echo built ... $(TARGET).cia

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf $(TARGET).cia

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).3dsx: $(OUTPUT).elf $(OUTPUT).smdh
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
