ifneq ($(shell command -v riscv64-unknown-elf-gcc 2>/dev/null),)
CROSS_COMPILE ?= riscv64-unknown-elf-
else ifneq ($(shell command -v riscv64-elf-gcc 2>/dev/null),)
CROSS_COMPILE ?= riscv64-elf-
else
CROSS_COMPILE ?= riscv64-unknown-elf-
endif
CC := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump
GDB := $(CROSS_COMPILE)gdb
QEMU := qemu-system-riscv64

BUILD_DIR := build
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
KERNEL_BIN := $(BUILD_DIR)/kernel.bin
KERNEL_MAP := $(BUILD_DIR)/kernel.map

ARCH_CFLAGS := -march=rv64imac_zicsr -mabi=lp64 -mcmodel=medany
COMMON_CFLAGS := -ffreestanding -fno-common -fno-builtin -fno-stack-protector
COMMON_CFLAGS += -Wall -Wextra -Werror -O2 -g
CFLAGS := $(ARCH_CFLAGS) $(COMMON_CFLAGS) -Ikernel
ASFLAGS := $(ARCH_CFLAGS) $(COMMON_CFLAGS) -Ikernel
LDFLAGS := -T linker.ld -nostdlib -Wl,--gc-sections -Wl,-Map=$(KERNEL_MAP)

KERNEL_SRCS := \
	kernel/arch/riscv64/boot.S \
	kernel/arch/riscv64/trap.S \
	kernel/core/main.c \
	kernel/core/panic.c \
	kernel/core/trap.c \
	kernel/drivers/timer.c \
	kernel/drivers/uart.c

KERNEL_OBJS := $(patsubst %.S,$(BUILD_DIR)/%.o,$(filter %.S,$(KERNEL_SRCS)))
KERNEL_OBJS += $(patsubst %.c,$(BUILD_DIR)/%.o,$(filter %.c,$(KERNEL_SRCS)))
DEPS := $(KERNEL_OBJS:.o=.d)

.PHONY: all run debug test boot-test clean toolcheck

all: $(KERNEL_ELF) $(KERNEL_BIN)

toolcheck:
	@command -v $(CC) >/dev/null || { echo "missing compiler: $(CC)"; exit 1; }
	@command -v $(OBJCOPY) >/dev/null || { echo "missing objcopy: $(OBJCOPY)"; exit 1; }
	@command -v $(QEMU) >/dev/null || { echo "missing QEMU: $(QEMU)"; exit 1; }

run: $(KERNEL_ELF)
	$(QEMU) -machine virt -m 128M -smp 1 -nographic -bios none -kernel $(KERNEL_ELF)

debug: $(KERNEL_ELF)
	$(QEMU) -machine virt -m 128M -smp 1 -nographic -bios none -kernel $(KERNEL_ELF) -S -s

test: $(KERNEL_ELF)
	python3 tools/qemu_smoke_test.py $(QEMU) $(KERNEL_ELF)

boot-test: test

$(KERNEL_ELF): $(KERNEL_OBJS) linker.ld
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(KERNEL_OBJS)

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
