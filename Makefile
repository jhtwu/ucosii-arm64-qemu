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
LDFLAGS := -nostdlib -T boot/linker.ld -Wl,-Map,$(MAP_FILE) -Wl,--gc-sections

C_SRCS := \
    src/main.c \
    src/net_demo.c \
    src/lib.c \
    src/irq.c \
    port/os_cpu_c.c \
    ucosii/source/os_core.c \
    ucosii/source/os_task.c \
    ucosii/source/os_time.c \
    bsp/gic.c \
    bsp/uart.c \
    bsp/timer.c \
    bsp/bsp_int.c \
    bsp/bsp_os.c \
    bsp/virtio_net.c \
    bsp/nat.c

ASM_SRCS := \
    boot/start.S \
    port/os_cpu_a.S

OBJS := $(C_SRCS:%.c=$(BUILD_DIR)/%.o) $(ASM_SRCS:%.S=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

# Test targets
TEST1_TARGET := $(BUILD_DIR)/test_context_timer.elf
TEST2_TARGET := $(BUILD_DIR)/test_network_ping.elf
TEST3_TARGET := $(BUILD_DIR)/test_dual_network.elf

TEST_COMMON_SRCS := \
    port/os_cpu_c.c \
    port/os_cpu_a.S \
    ucosii/source/os_core.c \
    ucosii/source/os_task.c \
    ucosii/source/os_time.c \
    bsp/gic.c \
    bsp/uart.c \
    bsp/timer.c \
    bsp/bsp_int.c \
    bsp/bsp_os.c \
    bsp/nat.c \
    src/lib.c \
    src/irq.c \
    boot/start.S

TEST1_SRCS := test/test_context_timer.c
TEST2_SRCS := test/test_network_ping.c bsp/virtio_net.c
TEST3_SRCS := test/test_dual_network.c bsp/virtio_net.c

TEST1_OBJS := $(TEST_COMMON_SRCS:%.c=$(BUILD_DIR)/%.o) $(TEST_COMMON_SRCS:%.S=$(BUILD_DIR)/%.o)
TEST1_OBJS := $(filter %.o,$(TEST1_OBJS))
TEST1_OBJS += $(TEST1_SRCS:%.c=$(BUILD_DIR)/%.o)

TEST2_OBJS := $(TEST_COMMON_SRCS:%.c=$(BUILD_DIR)/%.o) $(TEST_COMMON_SRCS:%.S=$(BUILD_DIR)/%.o)
TEST2_OBJS := $(filter %.o,$(TEST2_OBJS))
TEST2_OBJS += $(TEST2_SRCS:%.c=$(BUILD_DIR)/%.o)

TEST3_OBJS := $(TEST_COMMON_SRCS:%.c=$(BUILD_DIR)/%.o) $(TEST_COMMON_SRCS:%.S=$(BUILD_DIR)/%.o)
TEST3_OBJS := $(filter %.o,$(TEST3_OBJS))
TEST3_OBJS += $(TEST3_SRCS:%.c=$(BUILD_DIR)/%.o)

.PHONY: all clean run test-timer test-ping test-dual test-all

all: $(TARGET)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(TARGET): $(OBJS) boot/linker.ld
	$(LD) $(CFLAGS) $(OBJS) $(LDFLAGS) -lgcc -o $@

clean:
	rm -rf $(BUILD_DIR)

run: $(TARGET)
	@status=0; timeout --foreground 60s qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a57 -nographic \
		-global virtio-mmio.force-legacy=off \
		-netdev tap,id=net0,ifname=qemu-lan,script=no,downscript=no \
		-device virtio-net-device,netdev=net0,bus=virtio-mmio-bus.0,mac=52:54:00:12:34:56 \
		-netdev tap,id=net1,ifname=qemu-wan,script=no,downscript=no \
		-device virtio-net-device,netdev=net1,bus=virtio-mmio-bus.1,mac=52:54:00:65:43:21 \
		-kernel $(TARGET) 2>&1 || status=$$?; \
	 if [ $$status -eq 124 ]; then echo "[INFO] Demo stopped after 60s timeout"; fi; \
	 if [ $$status -ne 0 ] && [ $$status -ne 124 ]; then exit $$status; fi

# Test case 1: Context switch and timer validation
$(TEST1_TARGET): $(TEST1_OBJS) boot/linker.ld
	$(LD) $(CFLAGS) $(TEST1_OBJS) $(LDFLAGS) -lgcc -o $@

test-timer: $(TEST1_TARGET)
	@echo "========================================="
	@echo "Running Test Case 1: Context Switch & Timer"
	@echo "========================================="
	@output=$$(timeout --foreground 5s qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a57 -nographic \
		-kernel $(TEST1_TARGET) 2>&1); \
	echo "$$output"; \
	if echo "$$output" | grep -q "\[PASS\]"; then \
		echo ""; echo "✓ TEST PASSED"; exit 0; \
	elif echo "$$output" | grep -q "\[FAIL\]"; then \
		echo ""; echo "✗ TEST FAILED"; exit 1; \
	else \
		echo ""; echo "⚠ TEST INCOMPLETE"; exit 1; \
	fi

# Test case 2: Network TAP ping test
$(TEST2_TARGET): $(TEST2_OBJS) boot/linker.ld
	$(LD) $(CFLAGS) $(TEST2_OBJS) $(LDFLAGS) -lgcc -o $@

test-ping: $(TEST2_TARGET)
	@echo "========================================="
	@echo "Running Test Case 2: Network TAP Ping Test"
	@echo "========================================="
	@echo "Prerequisites: TAP interface 'qemu-lan' must be configured"
	@echo "              with IP 192.168.1.103"
	@echo ""
	@output=$$(timeout --foreground 5s qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a57 -nographic \
		-global virtio-mmio.force-legacy=off \
		-netdev tap,id=net0,ifname=qemu-lan,script=no,downscript=no \
		-device virtio-net-device,netdev=net0,bus=virtio-mmio-bus.0 \
		-kernel $(TEST2_TARGET) 2>&1); \
	echo "$$output"; \
	if echo "$$output" | grep -q "\[PASS\]"; then \
		echo ""; echo "✓ TEST PASSED"; exit 0; \
	elif echo "$$output" | grep -q "\[FAIL\]"; then \
		echo ""; echo "✗ TEST FAILED"; exit 1; \
	else \
		echo ""; echo "⚠ TEST INCOMPLETE"; exit 1; \
	fi

# Test case 3: Dual network interface test
$(TEST3_TARGET): $(TEST3_OBJS) boot/linker.ld
	$(LD) $(CFLAGS) $(TEST3_OBJS) $(LDFLAGS) -lgcc -o $@

test-dual: $(TEST3_TARGET)
	@echo "========================================="
	@echo "Running Test Case 3: Dual Network Test"
	@echo "========================================="
	@echo "Prerequisites: TAP interfaces 'qemu-lan' and 'qemu-wan'"
	@echo "              qemu-lan: 192.168.1.103"
	@echo "              qemu-wan: 10.3.5.103"
	@echo ""
	@output=$$(timeout --foreground 5s qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a57 -nographic \
		-global virtio-mmio.force-legacy=off \
		-netdev tap,id=net0,ifname=qemu-lan,script=no,downscript=no \
		-device virtio-net-device,netdev=net0,bus=virtio-mmio-bus.0,mac=52:54:00:12:34:56 \
		-netdev tap,id=net1,ifname=qemu-wan,script=no,downscript=no \
		-device virtio-net-device,netdev=net1,bus=virtio-mmio-bus.1,mac=52:54:00:65:43:21 \
		-kernel $(TEST3_TARGET) 2>&1); \
	echo "$$output"; \
	if echo "$$output" | grep -q "\[PASS\]"; then \
		echo ""; echo "✓ TEST PASSED"; exit 0; \
	elif echo "$$output" | grep -q "\[FAIL\]"; then \
		echo ""; echo "✗ TEST FAILED"; exit 1; \
	else \
		echo ""; echo "⚠ TEST INCOMPLETE"; exit 1; \
	fi

# Run all tests
test-all: test-timer test-ping test-dual
	@echo ""
	@echo "========================================="
	@echo "All tests completed"
	@echo "========================================="

-include $(DEPS)
