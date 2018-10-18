
TOOLCHAIN_PATH :=  /tools/toolchain/arm-embedded/
TOOLCHAIN_VER  := gcc-arm-none-eabi-6-2017-q2-update
TOOLCHAIN_DIR  := $(TOOLCHAIN_PATH)/$(TOOLCHAIN_VER)/bin/
CC	       := $(TOOLCHAIN_PATH)/$(TOOLCHAIN_VER)/bin/arm-none-eabi-gcc

CFLAGS	+= -std=gnu11
CFLAGS	+= -Wall
CFLAGS	+= -g

CFLAGS	+= -mcpu=cortex-m0plus -fno-common -fno-builtin -fno-exceptions -ffunction-sections -fdata-sections -fomit-frame-pointer 

CFLAGS	+= -I../../src/common
CFLAGS	+= -I../../src/arm/stm32lx
CFLAGS	+= -I../../src/arm/CMSIS/Include
CFLAGS	+= -I../../src/arm/CMSIS/Device/ST/STM32L0xx/Include

LDFLAGS	+= -Wl,--gc-sections -Wl,-Map,$(basename $@).map
LDFLAGS	+= -nostdlib
LDFLAGS	+= -T../../src/arm/stm32lx/ld/STM32L0xxZ.ld
LDFLAGS	+= -Tfw.ld

FWTOOL	:= python ../../tools/fwtool/fwtool.py

hello.hex: hello
	$(TOOLCHAIN_PATH)/$(TOOLCHAIN_VER)/bin/arm-none-eabi-objcopy -O ihex $< $@
	$(FWTOOL) patch $@

hello: hello.o

clean:
	rm -f *.o *.map hello hello.hex

.PHONY: clean

.DELETE_ON_ERROR:
