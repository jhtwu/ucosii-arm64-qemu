# Project Structure

This document describes the organization of the uC/OS-II ARMv8-A port project.

## Directory Layout

```
/project/codex/ucos-arm/
├── boot/                    # Boot and low-level initialization
│   ├── start.S             # Assembly startup code (entry point)
│   └── linker.ld           # Linker script for memory layout
│
├── bsp/                     # Board Support Package
│   ├── bsp_int.c/h         # BSP interrupt management
│   ├── bsp_os.c/h          # BSP OS hooks and timer tick
│   ├── gic.c/h             # ARM GICv3 interrupt controller driver
│   ├── mmio.h              # Memory-mapped I/O utilities
│   ├── timer.c/h           # ARM Generic Timer driver
│   ├── uart.c/h            # PL011 UART driver
│   └── virtio_net.c/h      # VirtIO network driver
│
├── port/                    # uC/OS-II ARM port (architecture-specific)
│   ├── os_cpu.h            # CPU-specific type definitions
│   ├── os_cpu_c.c          # C portion of port (task creation, etc.)
│   └── os_cpu_a.S          # Assembly port (context switch, etc.)
│
├── ucosii/                  # uC/OS-II kernel source
│   ├── include/            # Kernel headers
│   │   ├── os_cfg.h        # OS configuration
│   │   ├── os_trace.h      # Trace/debug macros
│   │   └── ucos_ii.h       # Main kernel header
│   └── source/             # Kernel implementation
│       ├── os_core.c       # Core scheduler and initialization
│       ├── os_task.c       # Task management
│       └── os_time.c       # Time management
│
├── include/                 # Application headers
│   ├── app_cfg.h           # Application configuration
│   ├── cpu.h               # CPU-specific definitions
│   ├── lib.h               # Utility library header
│   └── net_demo.h          # Network demo header
│
├── src/                     # Application source code
│   ├── main.c              # Main application entry
│   ├── net_demo.c          # Network demonstration code
│   ├── lib.c               # Utility functions (memcpy, etc.)
│   └── irq.c               # IRQ handler dispatcher
│
├── test/                    # Test cases
│   ├── README.md           # Test documentation
│   ├── test_context_timer.c    # Test 1: Context switch & timer
│   └── test_network_ping.c     # Test 2: Network TAP ping test
│
├── doc/                     # Documentation
│   ├── 01-TIMER_INTERRUPT_FIX.md
│   ├── 02-CONTEXT_SWITCH_FIX.md
│   ├── VIRTIO_PORTING.md
│   └── PROJECT_STRUCTURE.md     # This file
│
├── archive/                 # Reference and old files (not compiled)
│   └── port_reference/     # Official uC/OS-II port reference files
│
├── Makefile                 # Build system
└── README.md                # Project overview
```

## Key Components

### Boot Sequence (`boot/`)

1. **start.S**: Entry point after QEMU loads the kernel
   - Sets up initial stack pointer
   - Configures exception vector table
   - Initializes BSS section
   - Jumps to C `main()` function

2. **linker.ld**: Memory layout definition
   - Defines code, data, and BSS sections
   - Sets up stack locations
   - Configures memory addresses for ARMv8 virt machine

### Board Support Package (`bsp/`)

Hardware abstraction layer for ARM Virt platform:

- **GIC (Generic Interrupt Controller)**: Interrupt management
- **UART**: Serial console for debugging output
- **Timer**: System tick generation for OS scheduler
- **VirtIO-net**: Network device driver for TAP/user networking

### uC/OS-II Port (`port/`)

ARM-specific implementation of uC/OS-II primitives:

- **Context switching**: Save/restore task state
- **Task stack initialization**: Set up new task stacks
- **Critical sections**: Interrupt disable/enable
- **Timer tick hook**: Called on each timer interrupt

### Application (`src/`)

Demo applications showing OS capabilities:

- **main.c**: Dual-task context switching demo
- **net_demo.c**: Network stack demo (ARP, ICMP)
- **lib.c**: Basic C library functions
- **irq.c**: Interrupt routing and handling

### Test Suite (`test/`)

Automated validation tests:

- **test_context_timer.c**: Validates task scheduling and timer interrupts
- **test_network_ping.c**: Tests network stack with performance measurements

## Build Targets

### Main Targets
- `make all` - Build main demo application
- `make clean` - Remove all build artifacts
- `make run` - Run demo in QEMU (user networking)
- `make run-tap` - Run demo in QEMU (TAP networking)

### Test Targets
- `make test-timer` - Run context switch and timer test
- `make test-ping` - Run network ping test (requires TAP setup)
- `make test-all` - Run all automated tests

## File Organization Rationale

### Why `boot/` directory?
- **start.S** and **linker.ld** are architecture-specific boot files
- Separating them from root improves organization
- Makes it clear these are low-level initialization files
- Follows convention of larger embedded projects

### Why `archive/` directory?
- Keeps reference implementation for comparison
- Not compiled into final binary
- Useful for understanding official port structure
- Can be deleted if disk space is a concern

### Why separate `test/` directory?
- Tests are separate from production code
- Each test is a standalone application
- Easy to add new tests without modifying main code
- Clear separation of concerns

### Why keep `bsp/` separate from `port/`?
- **port/**: OS-specific, portable across ARM platforms
- **bsp/**: Hardware-specific, tied to QEMU virt machine
- Clear boundary between OS port and hardware drivers

## Removed Files

The following files were removed during reorganization:

- `bsp/gic.c.backup` - Backup file (redundant)
- `bsp/gic_old.c` - Old implementation (superseded)
- `bsp/virtio_net.c.rej` - Rejected patch file (temporary)
- `port/os_cpu_official.*` - Moved to `archive/port_reference/`
- `port/os_cpu_c_template.c` - Moved to `archive/port_reference/`
- `port/os_cpu_c_ref.c` - Moved to `archive/port_reference/`

## Header Include Paths

The Makefile configures these include paths:

```makefile
-Iinclude          # Application headers
-Iport             # Port-specific headers
-Iucosii/include   # Kernel headers
-Ibsp              # BSP headers
```

This allows including files like:
```c
#include <ucos_ii.h>    // From ucosii/include/
#include "uart.h"        // From bsp/
#include "app_cfg.h"     // From include/
```

## Version Control Notes

Key git operations during reorganization:
- Used `git mv` to preserve file history
- Created new directories: `boot/`, `test/`, `archive/`
- Removed backup and temporary files
- Updated Makefile to reflect new paths

## Future Extensions

Suggested structure for future additions:

```
drivers/               # Additional device drivers
  ├── spi/
  ├── i2c/
  └── gpio/

apps/                  # Multiple application demos
  ├── demo_basic/
  ├── demo_network/
  └── demo_filesystem/

libs/                  # Shared libraries
  ├── lwip/           # Lightweight IP stack
  └── fatfs/          # FAT filesystem
```

---

Last updated: 2025-10-10
