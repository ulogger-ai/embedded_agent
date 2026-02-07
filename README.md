# uLogger Library Integration Guide

## Overview

uLogger is an embedded logging library with support for:
- **Runtime logging** with configurable debug modules and levels
- **Crash dump capture** using ARM CrashCatcher for hard fault analysis
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

### 2. Implement Memory Driver Functions

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

### 3. Create Memory Control Blocks

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

### 4. Create and Initialize Configuration

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
    .mcb_len = sizeof(ulogger_mem_ctl_block) / sizeof(mem_ctl_block_t),
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

### 5. Define Debug Module Table

The included `ulogger_modules_def.c` defines the debug module table. You
simply need to configure the modules in `ulogger_config.h` and those will
automatically be extracted during AXF upload process.

### 6. Use the Logging API

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
