TOOLCHAIN_PATH :=  /tools/toolchain/arm-embedded/
TOOLCHAIN_VER := gcc-arm-none-eabi-6-2017-q2-update
TOOLCHAIN_DIR := $(TOOLCHAIN_PATH)/$(TOOLCHAIN_VER)/bin/

ifeq ($(TOOLCHAIN),gcc)
    FLAGS	+= -MMD -MP
    FLAGS	+= -g
    CFLAGS	+= -std=gnu11
    LDFLAGS	+= -Wl,--gc-sections -Wl,-Map,$(basename $@).map 
    CC		:= $(TOOLCHAIN_DIR)$(CROSS_COMPILE)gcc
    AS		:= $(TOOLCHAIN_DIR)$(CROSS_COMPILE)as
    LD		:= $(TOOLCHAIN_DIR)$(CROSS_COMPILE)gcc
    HEX		:= $(TOOLCHAIN_DIR)$(CROSS_COMPILE)objcopy -O ihex
    BIN		:= $(TOOLCHAIN_DIR)$(CROSS_COMPILE)objcopy -O binary
endif

CDEFS		+= $(DEFS)
ASDEFS		+= $(DEFS) ASSEMBLY
CFLAGS		+= $(addprefix -D,$(CDEFS))
ASFLAGS		+= $(addprefix -D,$(ASDEFS))

CFLAGS		+= $(FLAGS)
ASFLAGS		+= $(FLAGS)
