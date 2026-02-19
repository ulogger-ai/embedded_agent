# uLogger Library Integration Guide

## Overview

The uLogger embedded agent works in combination with uLogger Cloud. After integration of the agent library, you must add a step to your build job to publish the AXF file to uLogger 
Cloud. For more information on that, follow the [Upload Example](https://github.com/ulogger-ai/example_upload)

uLogger is an embedded logging library with support for:
- **Runtime logging** with configurable debug modules and levels
- **Crash dump capture** for capturing watchdogs or hard faults for analysis
- **Non-volatile storage** for persistent log and crash dump data
- **Pretrigger buffering** to capture events leading up to crashes

This directory contains the public API headers. The only file you need to customize is **`ulogger_config.h`**, which defines your application's debug modules and memory configuration.

## Integration Steps

### 1. Copy and Customize `ulogger_config.h`

Copy the `ulogger_config.h` template to your application's include directory and customize:

#### Define Your Debug Modules
```c
#define ULOGGER_DEBUG_MODULE_LIST(X) \
  X(MAIN,    0) \
  X(NETWORK, 1) \
  X(STORAGE, 2) \
  X(SENSOR,  3)
```

Each module gets a unique bit position (0-31) and generates a corresponding `*_MODULE` enum value (e.g., `MAIN_MODULE`, `NETWORK_MODULE`).

#### Configure Memory Addresses
```c
// Debug log storage region
#define ULOGGER_LOG_NV_START_ADDRESS       0x08080000
#define ULOGGER_LOG_NV_END_ADDRESS         0x0808FFFF

// Crash dump storage region  
#define ULOGGER_EXCEPTION_NV_START_ADDRESS 0x08090000
#define ULOGGER_EXCEPTION_NV_END_ADDRESS   0x080AFFFF
```

Set these to match your hardware's available non-volatile memory (Flash, EEPROM, etc.).
Note: These should be aligned to page boundaries if your non-volatile memory requires that.

### 2. Link the Static Library

The uLogger static library includes a custom `HardFault_Handler` that intercepts hard faults before capturing the crash dump. For the linker to pick up this handler correctly, **the library must be added before other inputs during linking**.

#### Linker Flag

Add the following to your linker flags:

```
-Wl,-u,HardFault_Handler -lULogger_armv7m
```

- `-Wl,-u,HardFault_Handler` forces the linker to include the symbol even if it doesn't see an explicit reference to it yet, ensuring the library's hard fault handler is not discarded.
- `-lULogger_<arch>` links the appropriate library for your target architecture (see table below).

#### Simplicity Studio

In Simplicity Studio, configure the linker in the project properties under **GNU ARM C Linker**:

1. **Libraries** — Add the library search path pointing to the folder containing the uLogger `.a` files:
   - Under **Library search path (-L)**, add the path to the `lib/` directory, e.g. `"${workspace_loc:/embedded_agent/lib}"`
   - Under **Libraries (-l)**, add the library name without the `lib` prefix or `.a` extension, e.g. `ULogger_armv7m`

2. **Miscellaneous → Other flags** — Add the force-include flag:
   ```
   -Wl,-u,HardFault_Handler
   ```

#### Makefile

With GNU ld, static libraries are searched left-to-right and a strong symbol pulled in from an earlier library will win over a weak symbol encountered later. To guarantee the uLogger `HardFault_Handler` overrides any weak definition elsewhere (e.g. from a vendor device startup file), the library must appear **before** the other libraries and object files on the linker command line.

The cleanest way to achieve this in a Makefile is to prepend the uLogger library to `LDLIBS` and pass `-Wl,-u,HardFault_Handler` through `LDFLAGS`:

```makefile
LIB_DIR := path/to/ulogger/lib

LDFLAGS += -Wl,-u,HardFault_Handler

# uLogger must come first so its strong HardFault_Handler is resolved
# before the weak definition in the vendor startup object is seen.
LDLIBS  := -L$(LIB_DIR) -lULogger_armv7m $(LDLIBS)
```

Then your link rule uses that ordering:

```makefile
$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@
```

> **Why order matters:** GNU ld processes archive libraries on demand — it only extracts a member when it satisfies an unresolved symbol. `-Wl,-u,HardFault_Handler` creates an artificial undefined reference at the very start of linking, so the linker immediately pulls the strong definition from uLogger before any other object or library is processed. Without this flag, a weak `HardFault_Handler` already present in an object file would be silently kept and uLogger's version would never be extracted.

#### Architecture to Library Mapping

Select the library that matches your target's ARM architecture:

| ARM Architecture | Cortex-M Cores | Linker Flag |
|---|---|---|
| ARMv6-M | Cortex-M0, M0+, M1 | `-lULogger_armv6m` |
| ARMv7-M | Cortex-M3, M4, M4F, M7, M7F | `-lULogger_armv7m` |
| ARMv8-M | Cortex-M23, M33, M35P, M55 | `-lULogger_armv8m` |

> **Example:** For an EFR32MG21 (Cortex-M33, ARMv8-M), use `-Wl,-u,HardFault_Handler -lULogger_armv8m`.

### 3. Implement Memory Driver Functions

In your application, implement three memory driver functions that provide access to your non-volatile storage:

```c
bool ulogger_nv_mem_read(uint32_t address, uint8_t *data, uint32_t len);
bool ulogger_nv_mem_write(uint32_t address, const uint8_t *data, uint32_t len);
bool ulogger_nv_mem_erase(uint32_t address, uint32_t len);
```

These functions should:
- **Read/Write**: Transfer data between RAM and non-volatile memory
- **Erase**: Prepare a memory region for writing (e.g., Flash sector erase)
- **Return**: `true` on success, `false` on error

### 4. Create Memory Control Blocks

Define memory control blocks that map memory regions to data types:

```c
static const mem_drv_t ulogger_nv_mem_driver = {
    .read = ulogger_nv_mem_read,
    .write = ulogger_nv_mem_write,
    .erase = ulogger_nv_mem_erase,
};

static const mem_ctl_block_t ulogger_mem_ctl_block[] = {
    {
        .type = ULOGGER_MEM_TYPE_DEBUG_LOG,
        .start_addr = ULOGGER_LOG_NV_START_ADDRESS,
        .end_addr = ULOGGER_LOG_NV_END_ADDRESS,
        .mem_drv = &ulogger_nv_mem_driver,
    },
    {
        .type = ULOGGER_MEM_TYPE_STACK_TRACE,
        .start_addr = ULOGGER_EXCEPTION_NV_START_ADDRESS,
        .end_addr = ULOGGER_EXCEPTION_NV_END_ADDRESS,
        .mem_drv = &ulogger_nv_mem_driver,
    },
};
```

### 5. Create and Initialize Configuration

You must configure the ulogger library to customize your logging levels, groups,
and provide your non-volatile memory functions to it.

Create a `ulogger_config_t` structure and initialize the library:

```c
// Optional: Allocate pretrigger buffer
#define PRETRIGGER_LOG_COUNT 100
static uint8_t pretrigger_buffer[1024];

static ulogger_config_t g_ulogger_config = {
    .stack_top_address = 0x20008000,  // From your linker script
    .flags_level = {
        .flags = 0xFFFFFFFF,           // Enable all modules (or use specific bits)
        .level = ULOG_DEBUG,           // Minimum level to log
    },
    .mcb_param = ulogger_mem_ctl_block,
    .mcb_len = sizeof(ulogger_mem_ctl_block),
    .pretrigger_log_count = PRETRIGGER_LOG_COUNT,
    .pretrigger_buffer = pretrigger_buffer,
    .pretrigger_buffer_size = sizeof(pretrigger_buffer),
    
    // Crash dump metadata (This should be generated during your build process)
    .application_id = 0x12345678,
    .git_hash = "abc123def",
    .device_type = "MyDevice-v1",
    .device_serial = "SN-001",
    .version_string = "1.0.0",
};

void main(void) {
    ulogger_init(&g_ulogger_config);
    // Your application code...
}
```

### 6. Define Debug Module Table

The included `ulogger_modules_def.c` defines the debug module table. You
simply need to configure the modules in `ulogger_config.h` and those will
automatically be extracted during AXF upload process.

### 7. Use the Logging API

```c
#include "ulogger.h"

void example_function(void) {
    ulogger_log(MAIN_MODULE, ULOG_INFO, "System initialized\n");
    ulogger_log(SENSOR_MODULE, ULOG_DEBUG, "Temperature: %d.%d°C\n", temp/10, temp%10);
    ulogger_log(NETWORK_MODULE, ULOG_ERROR, "Connection failed: %d\n", error_code);
}
```

## Required Components Summary

### Must Customize
- ✅ `ulogger_config.h` - Debug modules and memory addresses

### Must Implement
- ✅ `ulogger_nv_mem_read()` - Read from non-volatile memory
- ✅ `ulogger_nv_mem_write()` - Write to non-volatile memory  
- ✅ `ulogger_nv_mem_erase()` - Erase non-volatile memory

### Must Create
- ✅ Memory control blocks (`mem_ctl_block_t[]`)
- ✅ Memory driver structure (`mem_drv_t`)
- ✅ Configuration structure (`ulogger_config_t`)

## Library Features

- **Hard Fault Handling**: The library provides `HardFault_Handler` for ARM Cortex-M (included in the static library)
- **Pretrigger Buffer**: Optional circular buffer that retains recent logs and flushes to NV memory on critical events
- **Format Support**: Standard printf-style formatting with `%d`, `%u`, `%x`, `%s`, etc.
- **Memory Types**: Separate storage regions for debug logs and crash dumps

## Notes

- The static library contains weak stub implementations that will be overridden by your implementations at link time
- Ensure `stack_top_address` matches your linker script's stack definition
- Memory addresses must not overlap between debug log and stack trace regions
- Pretrigger buffer is optional but strongly recommended for crash analysis
