CROSS_COMPILE ?= aarch64-linux-gnu-
CC             := $(CROSS_COMPILE)gcc
LD             := $(CROSS_COMPILE)gcc

BUILD_DIR      := build
TARGET         := $(BUILD_DIR)/ucos_arm_demo.elf
MAP_FILE       := $(BUILD_DIR)/ucos_arm_demo.map

CFLAGS := -Wall -Wextra -O2 -std=c99 -ffreestanding -nostdlib -nostartfiles -fno-builtin \
          -fdata-sections -ffunction-sections -mcpu=cortex-a53 -MMD -MP \
          -Iinclude -Iport -Iucosii/include -Ibsp
ASFLAGS := $(CFLAGS)
LDFLAGS := -nostdlib -T linker.ld -Wl,-Map,$(MAP_FILE) -Wl,--gc-sections

C_SRCS := \
    src/main.c \
    port/os_cpu_c.c \
    ucosii/source/os_core.c \
    ucosii/source/os_task.c \
    ucosii/source/os_time.c \
    bsp/gic.c \
    bsp/uart.c \
    bsp/timer.c

ASM_SRCS := \
    start.S \
    port/os_cpu_a.S

OBJS := $(C_SRCS:%.c=$(BUILD_DIR)/%.o) $(ASM_SRCS:%.S=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

.PHONY: all clean run

all: $(TARGET)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(TARGET): $(OBJS) linker.ld
	$(LD) $(CFLAGS) $(OBJS) $(LDFLAGS) -lgcc -o $@

clean:
	rm -rf $(BUILD_DIR)

run: $(TARGET)
	@status=0; timeout --foreground 10s qemu-system-aarch64 -M virt,gic-version=2 -cpu cortex-a53 -nographic -kernel $(TARGET) 2>&1 || status=$$?; \
	 if [ $$status -eq 124 ]; then echo "[INFO] Demo stopped after 10s timeout"; fi; \
	 if [ $$status -ne 0 ] && [ $$status -ne 124 ]; then exit $$status; fi

-include $(DEPS)
