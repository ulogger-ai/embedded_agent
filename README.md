# uLogger Library Integration Guide

Agent version: **v1.2.4** (see `ulogger_version.h`)

## Overview

The uLogger embedded agent works in combination with uLogger Cloud. After integration of the agent library, you must add a step to your build job to publish the AXF file to uLogger
Cloud. For more information on that, follow the [Upload Example](https://github.com/ulogger-ai/example_upload)

uLogger is an embedded logging library with support for:
- **Runtime logging** with configurable debug modules and levels
- **Metrics and heartbeats** for device health and activity reporting
- **Crash dump capture** for capturing watchdogs, asserts or hard faults for analysis
- **Non-volatile storage** for persistent log and crash dump data
- **Pretrigger buffering** to capture events leading up to crashes

### What is in this directory

| File | Role |
|---|---|
| `ulogger.h` | Public API. Do not modify. |
| `ulogger_mem.h` | Memory abstraction API and control block types. Do not modify. |
| `ulogger_debug_modules.h` | Generates the debug module enum and flash-resident module table. Do not modify. |
| `ulogger_modules_def.c` | Instantiates the module table. Compile this into your application exactly once. |
| `ulogger_version.h` | Agent version string. |
| `lib/*.a` | Prebuilt static libraries, one per ARM architecture. |

**`ulogger_config.h` is not shipped in this repository.** It is your file: you create it, you own it, and it is listed in `.gitignore` so that agent updates never overwrite your settings. `ulogger.h` includes it, so the build will not compile until you provide it (see step 1).

## Integration Steps

### 1. Create `ulogger_config.h`

Create `ulogger_config.h` somewhere on your application's include path. It must define your debug modules, and may optionally define your memory addresses.

#### Define Your Debug Modules
```c
#ifndef ULOGGER_CONFIG_H
#define ULOGGER_CONFIG_H

#define ULOGGER_DEBUG_MODULE_LIST(X) \
  X(MAIN,    0) \
  X(NETWORK, 1) \
  X(STORAGE, 2) \
  X(SENSOR,  3)

#endif // ULOGGER_CONFIG_H
```

Each entry takes a name and a unique bit position (0-31). The list generates a `*_MODULE` enum value per entry whose value is the **bit mask**, not the bit index — `X(NETWORK, 1)` yields `NETWORK_MODULE == 0x02`. Pass those values straight to `ulogger_log()` and OR them together to form `flags_level.flags`.

Bit 31 is reserved: `ULOG_ALWAYS` (`0x80000000`) is a sentinel meaning *log regardless of the configured module flags*. Do not assign a module to bit 31.

#### Configure Memory Addresses
```c
// Debug log storage region
#define ULOGGER_LOG_NV_START_ADDRESS       0x08080000
#define ULOGGER_LOG_NV_END_ADDRESS         0x0808FFFF

// Crash dump storage region
#define ULOGGER_EXCEPTION_NV_START_ADDRESS 0x08090000
#define ULOGGER_EXCEPTION_NV_END_ADDRESS   0x080AFFFF
```

These particular macros are a convention, not a library requirement — the library only ever sees the addresses you put in the memory control blocks in step 4. Set them to match your hardware's available non-volatile memory (Flash, EEPROM, etc.).
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
static const ulogger_mem_drv_t ulogger_nv_mem_driver = {
    .read = ulogger_nv_mem_read,
    .write = ulogger_nv_mem_write,
    .erase = ulogger_nv_mem_erase,
};

static const ulogger_mem_ctl_block_t ulogger_mem_ctl_block[] = {
    {
        .type = ULOGGER_MEM_TYPE_DEBUG_LOG,
        .start_addr = ULOGGER_LOG_NV_START_ADDRESS,
        .end_addr = ULOGGER_LOG_NV_END_ADDRESS,
        .mem_drv = &ulogger_nv_mem_driver,
        .erase_granularity = FLASH_PAGE_SIZE,   // optional, recommended
    },
    {
        .type = ULOGGER_MEM_TYPE_STACK_TRACE,
        .start_addr = ULOGGER_EXCEPTION_NV_START_ADDRESS,
        .end_addr = ULOGGER_EXCEPTION_NV_END_ADDRESS,
        .mem_drv = &ulogger_nv_mem_driver,
        .erase_granularity = FLASH_PAGE_SIZE,
    },
};
```

Available region types are `ULOGGER_MEM_TYPE_DEBUG_LOG`, `ULOGGER_MEM_TYPE_STACK_TRACE` and `ULOGGER_MEM_TYPE_OTA_PATCH`.

#### `erase_granularity`

`erase_granularity` is the smallest independently erasable unit of the region in bytes — the same constant your driver's `erase()` already steps by, typically the flash page or sector size. It is declared per region because regions may live on different devices (internal flash vs. an external QSPI part).

- **Leave it 0** (the default for a partial initialiser) and the library only ever erases the region as a whole. This is the long-standing behaviour.
- **Supply it** and the library reclaims space a page at a time instead, so a log transfer no longer has to discard entries that were written while it was in flight.

The library uses the value only if `start_addr` and the region length are both multiples of it; otherwise a partial erase could take a neighbouring page with it, so the whole-region path is used instead. Call `ulogger_mem_get_erase_granularity()` to see what the library actually accepted — it returns 0 when the value was rejected.

### 5. Create and Initialize Configuration

You must configure the ulogger library to customize your logging levels, groups,
and provide your non-volatile memory functions to it.

Create a `ulogger_config_t` structure and initialize the library:

```c
// Optional: Allocate pretrigger buffer
#define PRETRIGGER_LOG_COUNT 100
static uint8_t pretrigger_buffer[1024];

extern uint32_t __StackTop;   // From your linker script

static const void *stack_top_cb(ulogger_stack_type_t stack_type) {
    // Return the top of the requested stack. If your application does not use
    // the process stack, return the same address for both.
    (void)stack_type;
    return (const void *)&__StackTop;
}

static void fault_reboot(void) {
    NVIC_SystemReset();   // Called once the crash dump has been captured.
}

static ulogger_config_t g_ulogger_config = {
    .fault_reboot_cb = fault_reboot,
    .stack_top_address_cb = stack_top_cb,
    .flags_level = {
        .flags = 0xFFFFFFFF,           // Enable all modules (or OR together *_MODULE values)
        .level = ULOG_DEBUG,           // Minimum level to log
    },
    .mcb_param = ulogger_mem_ctl_block,
    .mcb_len = sizeof(ulogger_mem_ctl_block),   // BYTES, not element count
    .pretrigger_log_count = PRETRIGGER_LOG_COUNT,
    .pretrigger_buffer = pretrigger_buffer,
    .pretrigger_buffer_size = sizeof(pretrigger_buffer),

    // Timestamps
    .get_tick = app_get_tick,          // Raw hardware tick counter
    .tick_rate_hz = 32768,             // e.g. 32.768 kHz RTC
    .get_epoch_us = app_get_epoch_us,  // Optional, may be NULL

    // Crash dump metadata (This should be generated during your build process)
    .application_id = 0x12345678,
    .git_hash = "abc123def",
    .device_type = "MyDevice-v1",
    .device_serial = "SN-001",
    .version_string = "1.0.0",
};

void main(void) {
    if (!ulogger_init(&g_ulogger_config)) {
        // RAM logging is up, but the NV store is not. See below.
    }
    // Your application code...
}
```

#### Callbacks

- **`stack_top_address_cb`** replaces the old fixed `stack_top_address` field. It is called with `ULOGGER_STACK_TYPE_MSP` or `ULOGGER_STACK_TYPE_PSP` and returns the top of that stack, so a dump can be taken correctly whichever stack was active at the fault.
- **`fault_reboot_cb`** runs after a crash has been captured — use it to reset the device.
- **`get_tick`** returns your raw hardware tick counter and **`tick_rate_hz`** tells the library how to convert it to real time. **`get_epoch_us`** is optional: it returns Unix epoch microseconds (UTC) sampled at the same instant as `get_tick()` at report-generation time. Leaving it `NULL` means cloud ingestion falls back to its own receive-time approximation for absolute timestamps.

#### `mcb_len` is a byte count

`mcb_len` is the **size of the control block array in bytes** — pass `sizeof(the_array)`, not the number of elements. If the value is not a whole multiple of `sizeof(ulogger_mem_ctl_block_t)` — an element count was passed, or the caller was built against a different definition of the control block — the entire array is refused and every memory region stays unconfigured.

`ulogger_init()` now detects that and returns `false` rather than running on with NV logging silently switched off. It also returns `false` if `config` is `NULL`. When the memory layer was the reason, RAM logging is still initialised and usable and only the non-volatile store is unavailable, so a caller may choose to continue. Use `ulogger_mem_is_configured()` to tell the two cases apart.

### 6. Define Debug Module Table

The included `ulogger_modules_def.c` defines the debug module table. You
simply need to configure the modules in `ulogger_config.h` and those will
automatically be extracted during AXF upload process.

Compile `ulogger_modules_def.c` into your application exactly once. It expands `ULLOGGER_DEFINE_DEBUG_MODULE_TABLE()`, which emits the table into the `.ulogger.debug_modules` section for extraction from the ELF. If you would rather place the table in your own translation unit, drop `ulogger_modules_def.c` and invoke the macro yourself in exactly one `.c` file:

```c
#include "ulogger_config.h"
#include "ulogger_debug_modules.h"

ULLOGGER_DEFINE_DEBUG_MODULE_TABLE();
```

### 7. Use the Logging API

```c
#include "ulogger.h"

void example_function(void) {
    ulogger_log(MAIN_MODULE, ULOG_INFO, "System initialized\n");
    ulogger_log(SENSOR_MODULE, ULOG_DEBUG, "Temperature: %d.%d°C\n", temp/10, temp%10);
    ulogger_log(NETWORK_MODULE, ULOG_ERROR, "Connection failed: %d\n", error_code);

    // Bypass the module filter entirely for a message that must always be kept
    ulogger_log(ULOG_ALWAYS, ULOG_CRITICAL, "Entering safe mode\n");
}
```

Levels, in ascending severity: `ULOG_DEBUG`, `ULOG_INFO`, `ULOG_WARNING`, `ULOG_ERROR`, `ULOG_CRITICAL`, `ULOG_METRIC`. Anything below `flags_level.level`, or whose module bit is clear, is discarded at the call site. `ULOG_METRIC` frames are always persisted to NV.

Change the filter at runtime with `ulogger_set_flags_level()`.

To also see logs locally (console, RTT, custom sink) register a callback:

```c
static void local_sink(uint32_t module, uint8_t level, const char *fmt, va_list args) {
    vprintf(fmt, args);
}

register_local_log_callback(local_sink);
```

### 8. Record Metrics and Heartbeats

Metrics are named values recorded at `ULOG_METRIC` level and always persisted:

```c
ULOGGER_METRIC("battery_mv", f, voltage);    // f = float/double
ULOGGER_METRIC("rssi",       i, rssi_dbm);   // i = signed integer
ULOGGER_METRIC("packets_rx", u, rx_count);   // u = unsigned integer
```

Metric names must contain no spaces and no `=`.

**Heartbeats are required.** The platform uses heartbeat presence to determine which devices are currently active, so call it **at least once per day**; more often is fine:

```c
ulogger_heartbeat();
```

It expands to a single `ulogger_log()` call with a literal format string and no runtime arguments, producing a 6-byte payload plus frame header.

### 9. Assertions

```c
ULOGGER_ASSERT(ptr != NULL);
ULOGGER_ASSERT(index < MAX_SIZE);
```

On failure the macro logs the file and line at `ULOG_CRITICAL` and captures a full core dump via `ulogger_assert_fail()`, which does not return.

### 10. Retrieve and Transfer Logs

Reading the NV log store for transmission follows a **seal → read → consume** cycle. Sealing freezes a snapshot so the reported size, the buffer header and the trailing gap all describe the same batch, and frames logged during the transfer are excluded from it rather than lost.

```c
// 1. Make sure everything buffered in RAM has reached NV memory.
ulogger_flush_pretrigger_to_nv();

// 2. Freeze the payload for this transfer.
uint32_t total = ulogger_seal_nv_logs_for_transfer();
if (total == 0) {
    return;  // Nothing to send.
}

// 3. Read it out — in one shot, or in chunks via read_offset.
uint8_t buf[CHUNK];
uint32_t offset = 0;
while (offset < total) {
    uint32_t n = ulogger_read_nv_logs_with_header(buf, sizeof(buf), session_token, offset);
    if (n == 0) {
        break;
    }
    transmit(buf, n);
    offset += n;
}

// 4. Only once the data is safely delivered.
ulogger_consume_nv_logs();
```

- `ulogger_get_nv_log_usage()` reports the same total (header + log data) and is what you size a buffer from. `ULOGGER_BUFFER_HEADER_SIZE` is the number of leading header bytes — use the macro rather than hardcoding a count, so it tracks wire-format version bumps.
- **Prefer `ulogger_consume_nv_logs()` over `ulogger_clear_nv_logs()`** on the transfer-complete path. Clearing erases the whole region and destroys entries written while the transfer was in flight; consuming discards only the sealed snapshot, and flash is erased only when the region drains or runs low on free space.
- `ulogger_read_nv_logs()` is a legacy path that returns raw log data with no header or trailer. It is bounded by the same snapshot, and does not advance the read position, but only the `_with_header()` variant emits the trailing-gap trailer the consumer needs to place the batch in time.

> **Duplicates after a reset.** The consume position lives in RAM, so a reset re-offers everything still physically present in the region — which now includes data already delivered but not yet erased, since the FIFO no longer wipes on every transfer. Expect duplicate delivery of up to a region's worth of entries after a reset. That is the deliberate trade for not losing entries written during the transfer window. A consumer that cannot tolerate duplicates must de-duplicate on its own side, and **cannot use tick values to do it** because the tick counter restarts at 0 on reset.

#### Log store diagnostics

```c
uint32_t pending, write_offset, dropped;
ulogger_get_nv_stats(&pending, &write_offset, &dropped);
```

All three pointers are optional. `dropped` is the cumulative count of bytes that passed the level filter but never reached the consumer — either space was reclaimed with no room to relocate the retained tail (typically a region with no usable `erase_granularity`, so only a whole-region erase is available), or the region had no room for a frame, or the device refused to program it. Frames discarded by the level/module filter are deliberate and are **not** counted. Non-zero means log data was lost. It resets to 0 on `ulogger_init()`.

### 11. Crash Dumps

Hard faults are captured automatically by the library's `HardFault_Handler`. `ulogger_get_core_dump_size()` returns the size of a stored dump, or 0 if there is none.

If you branch into the fault handler from a **non-fault** context — a watchdog early-warning ISR, for instance — record why first:

```c
void WDOG_EarlyWarning_IRQHandler(void) {
    ulogger_crash_set_cause(ULOGGER_CRASH_CAUSE_WATCHDOG);
    HardFault_Handler();
}
```

Without this, such a capture is indistinguishable from a genuine CPU fault: the fault status registers are clear, which is only weak evidence of what happened. Recording the cause says so outright.

| Cause | Meaning |
|---|---|
| `ULOGGER_CRASH_CAUSE_FAULT` | CPU took a fault (default) |
| `ULOGGER_CRASH_CAUSE_WATCHDOG` | Hardware watchdog early-warning interrupt |
| `ULOGGER_CRASH_CAUSE_ASSERT` | Failed assertion |
| `ULOGGER_CRASH_CAUSE_APP_WDG` | Application-level liveness check |

The value is written into the next crash dump and then reset to `ULOGGER_CRASH_CAUSE_FAULT`, so it can never be applied to a later genuine fault. It also starts at `ULOGGER_CRASH_CAUSE_FAULT` after every reset.

## Required Components Summary

### Must Create
- ✅ `ulogger_config.h` - Debug modules (and, by convention, memory addresses). Not shipped; you write it.
- ✅ Memory driver structure (`ulogger_mem_drv_t`)
- ✅ Memory control blocks (`ulogger_mem_ctl_block_t[]`)
- ✅ Configuration structure (`ulogger_config_t`)

### Must Implement
- ✅ `ulogger_nv_mem_read()` - Read from non-volatile memory
- ✅ `ulogger_nv_mem_write()` - Write to non-volatile memory
- ✅ `ulogger_nv_mem_erase()` - Erase non-volatile memory
- ✅ `stack_top_address_cb` - Top of MSP/PSP for crash dumps
- ✅ `get_tick` + `tick_rate_hz` - Timestamp source

### Must Call
- ✅ `ulogger_init()` - Once at startup, and check the return value
- ✅ `ulogger_heartbeat()` - At least once per day

## Library Features

- **Hard Fault Handling**: The library provides `HardFault_Handler` for ARM Cortex-M (included in the static library), with an explicit crash cause recorded in every dump
- **Pretrigger Buffer**: Optional circular buffer that retains recent logs and flushes to NV memory on critical events
- **Format Support**: Standard printf-style formatting with `%d`, `%u`, `%x`, `%s`, etc.
- **Metrics and Heartbeat**: Named float/signed/unsigned metrics, always persisted
- **Memory Types**: Separate storage regions for debug logs, crash dumps and OTA patches
- **Page-granular reclaim**: Optional `erase_granularity` lets the log store reclaim space a page at a time instead of wiping the whole region

## Notes

- `ulogger_config.h` is gitignored and not shipped — agent updates will not clobber it, but a fresh clone will not compile until you add it
- The static library contains weak stub implementations that will be overridden by your implementations at link time
- `mcb_len` is a size in bytes (`sizeof(array)`), not an element count — passing an element count causes `ulogger_init()` to return `false` with NV storage unconfigured
- Memory addresses must not overlap between debug log and stack trace regions
- Pretrigger buffer is optional but strongly recommended for crash analysis
- Use `ulogger_consume_nv_logs()`, not `ulogger_clear_nv_logs()`, to complete a log transfer
