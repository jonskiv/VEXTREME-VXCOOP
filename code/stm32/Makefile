
# make output a little less noisy
# use with "make clean all -s" for maximum impact
VERBOSE=@
VERBOSE_REDIRECT= > /dev/null 2>&1

DOCKER_ARGS=--rm -t -v `pwd`:/build/stm32 -u `id -u ${USER}`:`id -g ${USER}`
STM_DOCKER=docker run ${DOCKER_ARGS} stm32-build
STM_DOCKER_I=docker run -i ${DOCKER_ARGS} stm32-build

# define a symbol MAKE_OS for the current operating system
# WINDOWS | LINUX | OSX
ifeq ($(OS),Windows_NT)
    MAKE_OS = WINDOWS
    ifeq ($(PROCESSOR_ARCHITECTURE),AMD64)
        CPU=AMD64
    endif
    ifeq ($(PROCESSOR_ARCHITECTURE),x86)
        CPU=IA32
    endif
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        MAKE_OS=LINUX
    endif
    ifeq ($(UNAME_S),Darwin)
        MAKE_OS=OSX
    endif
endif

# HW Version Info
# USAGE: make clean all USE_HW=v0.2 for v0.2 VEXTREME build
# USAGE: make clean all USE_HW=v0.3 for v0.3 VEXTREME build
ifndef HW_VER_VAL
HW_VER_VAL?=255
endif
ifeq ($(USE_HW),v0.1)
HW_VER_VAL = 1
HW_VER_ARGS="USE_HW=v0.1"
else ifeq ($(USE_HW),v0.2)
HW_VER_VAL = 2
HW_VER_ARGS="USE_HW=v0.2"
else ifeq ($(USE_HW),v0.3)
HW_VER_VAL = 3
HW_VER_ARGS="USE_HW=v0.3"
endif
CFLAGS += -DHW_VER=$(HW_VER_VAL)
ASFLAGS += -DHW_VER=$(HW_VER_VAL)

ifeq ($(USE_UF2),1)
UF2_VAL=1
UF2_ARGS="USE_UF2=1"
LDSCRIPT=stm32f411rct6-uf2.ld
else
UF2_VAL=0
UF2_ARGS="USE_UF2=0"
LDSCRIPT=stm32f411rct6.ld
endif

BINARY=stm32

# VXT_ENABLE_CAL: link the calibration/measurement rig (vxt/vxt_cal.c,
# driven by the code/app6809/test8_cal/ cart)? Default 1. Its cost is
# deliberately small: every table in vxt_cal.c is const (flash, not RAM)
# and its only state is a handful of scalars.
VXT_ENABLE_CAL ?= 1
ifeq ($(VXT_ENABLE_CAL),1)
CAL_OBJS=vxt/vxt_cal.o
else
CAL_OBJS=
endif
CFLAGS += -DVXT_ENABLE_CAL=$(VXT_ENABLE_CAL)

# VXT_ENABLE_VOOM: link the VOOM-on-vxt_smart port (game/VOOM.c,
# game/voom_smart.c, vxt/voom_smart_handler.c, game/voom_level_data.S)?
# Default 1. All seven e1l1 lumps live in flash (28,364 bytes), and
# VOOM's z-buffer + line list are overlaid on menuData, so there is no
# per-frame SD I/O and bss cost is small.
#
# Flipping this flag requires `make clean` first - see GATE_ARGS below.
VXT_ENABLE_VOOM ?= 1
ifeq ($(VXT_ENABLE_VOOM),1)
VOOM_OBJS=game/VOOM.o game/voom_smart.o vxt/voom_smart_handler.o \
		game/voom_level_data.o
else
VOOM_OBJS=
endif
CFLAGS += -DVXT_ENABLE_VOOM=$(VXT_ENABLE_VOOM)

# VXT_ENABLE_VXCOOP: link the VX-COOP toolkit demonstration handler
# (vxcoop/, RPC 78/79), driven by code/app6809/VXCOOP/VXCOOP.asm? Default
# 1. See docs/VX-COOP/VX-COOP_Textbook.md.
VXT_ENABLE_VXCOOP ?= 1
ifeq ($(VXT_ENABLE_VXCOOP),1)
VXCOOP_OBJS=vxcoop/vxcoop_handler.o vxcoop/vxcoop_music_example.o
else
VXCOOP_OBJS=
endif
CFLAGS += -DVXT_ENABLE_VXCOOP=$(VXT_ENABLE_VXCOOP)

OBJS=main.o menu.o romemu.o msc.o xprintf.o usb_msc.o ff_diskio.o \
		fatfs/ff.o fatfs/option/unicode.o flash.o led.o delay.o \
		highscore.o settings.o vxt/vxt_rpc.o vxt/vxt_frame.o \
		vxt/vxt_sound.o vxt/vxt_music.o vxt/vxt_smart.o \
		vxt/vxt_smart_text.o vxt/vxt_font_western.o \
		vxt/vxt_cal_load.o vxt/vxt_bounds.o vxt/test6c_handler.o \
		gamelib/gamelib_beam.o gamelib/gamelib_proj3d.o \
		$(CAL_OBJS) $(VOOM_OBJS) $(VXCOOP_OBJS)

LIBNAME = opencm3_stm32f4
ARCH_FLAGS = -mfloat-abi=hard -mfpu=fpv4-sp-d16 -mthumb -mcpu=cortex-m4 -DSTM32F4

PREFIX	= arm-none-eabi
CC		= $(PREFIX)-gcc
LD		= $(PREFIX)-gcc
OBJCOPY	= $(PREFIX)-objcopy
OBJDUMP	= $(PREFIX)-objdump
# Uncomment this line if you want to use the installed (not local) library.
#TOOLCHAIN_DIR = `dirname \`which $(CC)\``/../$(PREFIX)
TOOLCHAIN_DIR=../libopencm3
CFLAGS		+= -O3 -g -Wextra -Wshadow -Wimplicit-function-declaration -Wredundant-decls \
		-Wmissing-prototypes -Wstrict-prototypes -fno-common -ffunction-sections -fdata-sections \
		-MD -Wall -Wundef -I$(TOOLCHAIN_DIR)/include $(ARCH_FLAGS)
ASFLAGS	+= $(ARCH_FLAGS)

LDFLAGS		+= --static -nostartfiles -L$(TOOLCHAIN_DIR)/lib  \
		   -Xlinker -Map=$(BINARY).map -T$(LDSCRIPT) -Wl,--gc-sections -mthumb $(CFLAGS) -lm \
			-Wl,--start-group -lc -lgcc -lnosys -Wl,--end-group -l$(LIBNAME)

#include libopencm3/ld/Makefile.linker

.SUFFIXES: .elf .bin .hex .srec .list .images
.SECONDEXPANSION:
.SECONDARY:

# Unzipped release files to code/releases/
# Specify release version like so
# make flash-bin HW_VER=v0.3 SW_VER=v0.24
flash-release:
	dfu-util -a 0 -d 0483:df11 -s 0x08000000 -D ../releases/$(SW_VER)/stm32/hardware-$(HW_VER)/stm32-sw.$(SW_VER)-test.hw.$(HW_VER).bin

# Specify bin like so
# make flash-bin BIN=../releases/v0.24/stm32/hardware-v0.3/stm32-sw.v0.24-test.hw.v0.3.bin
flash-bin:
	dfu-util -a 0 -d 0483:df11 -s 0x08000000 -D $(BIN)

all: menu.ld docker

# Convenience target bundling the required clean (see the .SECONDARY: note
# on GATE_ARGS above - flipping a gate needs one). Builds the toolkit plus
# the VX-COOP reference handler, with no other application layer linked.
# USE_HW is a command-line override, e.g. `make vxcoop USE_HW=v0.3`, and
# propagates to the nested $(MAKE) calls automatically.
vxcoop:
	$(MAKE) clean
	$(MAKE) all VXT_ENABLE_VXCOOP=1

docker-build: Dockerfile
	docker build . -t stm32-build

# Forward every gate into the container, defaulting to the same values as
# above, so a no-flag build is byte-identical to what it was.
# NOTE: `.SECONDARY:` below marks all objects intermediate, so make will
# NOT rebuild a missing $(VOOM_OBJS)-style object if the .elf already
# looks up to date - flipping a gate still requires `make clean`.
GATE_ARGS="VXT_ENABLE_VOOM=$(VXT_ENABLE_VOOM)" "VXT_ENABLE_CAL=$(VXT_ENABLE_CAL)" "VXT_ENABLE_VXCOOP=$(VXT_ENABLE_VXCOOP)"

docker:
	${STM_DOCKER} make images $(HW_VER_ARGS) $(UF2_ARGS) $(GATE_ARGS)

uf2: $(BINARY).bin
	${STM_DOCKER} uf2conv.py -b 0x08008000 -o $(BINARY).uf2 -c $(BINARY).bin

# '$ make bash' puts you in the docker container interactive shell for debugging
bash:
	${STM_DOCKER_I} bash

images: $(BINARY).images
flash: $(BINARY).flash

rom.o: rom.inc rom.c rom.h
	$(CC) $(CFLAGS) -o $@ -c rom.c

%.images: %.bin %.hex %.srec %.list
	@#echo "*** $* images generated ***"

%.bin: %.elf
	$(VERBOSE)$(OBJCOPY) -Obinary $(*).elf $(*).bin

%.hex: %.elf
	$(VERBOSE)$(OBJCOPY) -Oihex $(*).elf $(*).hex

%.srec: %.elf
	$(VERBOSE)$(OBJCOPY) -Osrec $(*).elf $(*).srec

%.list: %.elf
	$(VERBOSE)$(OBJDUMP) -S $(*).elf > $(*).list

%.elf: $(OBJS) $(LDSCRIPT) menu.ld
	$(LD) -o $(*).elf $(OBJS) $(LDFLAGS)
	$(PREFIX)-size $(*).elf

%.o: %.c Makefile
	$(CC) $(CFLAGS) -o $@ -c $<

# Real prerequisites on the 6809 sources - without them, once
# stm32/menu.bin exists on disk from any earlier build, make treats it as
# permanently up to date and silently keeps embedding that stale copy
# into the STM32 firmware even after code/menu/*.asm|*.i changes.
MENU_SOURCES = $(wildcard ../menu/*.asm) $(wildcard ../menu/*.i)
menu.bin: $(MENU_SOURCES)
	make -C ../menu/ clean docker-build all
	cp ../menu/menu.bin .

menu.ld: menu.bin
	cat $< | hexdump -v -e '"BYTE(0x" 1/1 "%02X" ")\n"' > $@

clean:
	rm -f menu.ld
	rm -f $(OBJS)
	rm -f *.elf
	rm -f *.bin
	rm -f *.hex
	rm -f *.srec
	rm -f *.list
	rm -f *.map
	rm -rf *.d

%.flash: %.bin
	dfu-util -a 0 -d 0483:df11 -s 0x08000000 -D $(*).bin

.PHONY: images clean flash dummy vxcoop
