#---------------------------------------------------------------------------------
TARGET		:=	RedRoulette
BUILD		:=	build
SOURCES		:=	source
DATA		:=	data
INCLUDES	:=	include
ROMFS		:=	romfs
#---------------------------------------------------------------------------------
# --- APP METADATA ---
export APP_TITLE	:=	Red Roulette
export APP_AUTHOR	:=	Sick Duck
export APP_VERSION	:=	1.0.0

# --- Force devkitPro Paths to fix switch.h errors ---
export DEVKITPRO := /opt/devkitpro
export DEVKITA64 := $(DEVKITPRO)/devkitA64
export LIBNX     := $(DEVKITPRO)/libnx
export PORTLIBS  := $(DEVKITPRO)/portlibs/switch

ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

PREFIX	:=	aarch64-none-elf-
CC	:=	$(PREFIX)gcc
CXX	:=	$(PREFIX)g++
AR	:=	$(PREFIX)gcc-ar
OBJCOPY	:=	$(PREFIX)objcopy
NM	:=	$(PREFIX)nm
export PATH := $(PORTLIBS)/bin:$(DEVKITA64)/bin:$(PATH)

CFLAGS	:=	-g -Wall -O2 -ffunction-sections \
			$(ARCH) $(DEFINES)

CFLAGS	+=	$(INCLUDE) -D__SWITCH__

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(LIBNX)/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

# --- Link SDL2 and libnx ---
LIBS	:= -lSDL2_ttf -lfreetype -lharfbuzz -lbz2 -lSDL2_image -lwebp -lpng -ljpeg -lSDL2 -lglad -lEGL -lglapi -ldrm_nouveau -lnx -lm -lz
LIBDIRS	:= $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

# Point the compiler to your icon file
export APP_ICON := $(TOPDIR)/icon.jpg

ifeq ($(strip $(ROMFS)),)
	export NROFLAGS :=
else
	export NROFLAGS := --romfsdir=$(TOPDIR)/$(ROMFS)
endif

# Append Metadata Flags to the `.nro` builder
export NROFLAGS += --icon=$(APP_ICON) --nacp=$(OUTPUT).nacp

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(TOPDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).pfs0 $(TARGET).nro $(TARGET).elf $(TARGET).nacp

else
DEPENDS	:=	$(OFILES:.o=.d)

# Make sure nacp builds before nro
$(OUTPUT).nro	:	$(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf	:	$(OFILES)

# Generate the Switch metadata file
$(OUTPUT).nacp:
	@echo creating $(notdir $@)
	@nacptool --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

%.elf:
	@echo linking $(notdir $@)
	@$(CXX) $(LDFLAGS) $(OFILES) $(LIBPATHS) $(LIBS) -o $@
	@$(NM) -CSn $@ > $(notdir $*.lst)

%.nro: %.elf
	@echo creating $(notdir $@)
	@elf2nro $< $@ $(NROFLAGS)

%.o: %.cpp
	@echo $(notdir $<)
	@$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.c
	@echo $(notdir $<)
	@$(CC) $(CFLAGS) -c $< -o $@

%.o: %.s
	@echo $(notdir $<)
	@$(CC) $(ASFLAGS) -c $< -o $@

%.bin.o	:	%.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)
endif