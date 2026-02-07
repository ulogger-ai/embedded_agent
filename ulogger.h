#ifndef ULOGGER_H
#define ULOGGER_H

/**
 * @file ulogger.h
 * @brief Public API for uLogger library
 *
 * This header contains the public API, types, and function prototypes for uLogger.
 * Include this file in your application code. Do not modify this file.
 * 
 * For configuration settings, see ulogger_config.h
 */

#include <stdint.h>
#include <stdbool.h>
#include "ulogger_mem.h"
#include "ulogger_config.h"
#include "ulogger_debug_modules.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Debug Level Definitions
// ============================================================================

/**
 * Standard debug levels (ascending severity)
 */
enum ULOGGER_DEBUG_LEVEL {
    ULOG_DEBUG = 0,      // Detailed debug information
    ULOG_INFO,           // General information
    ULOG_WARNING,        // Warning conditions
    ULOG_ERROR,          // Error conditions
    ULOG_CRITICAL,       // Critical failures
    ULOG_INVALID = 5     // Invalid/unused level
};

// ============================================================================
// Public API Types
// ============================================================================

/**
 * @brief Configuration structure for flags and level
 */
typedef struct {
    uint32_t flags;   // Debug module flags (bitfield)
    uint8_t level;    // Minimum log level threshold
} ulogger_flags_level_t;

/**
 * @brief Configuration structure for ulogger system (for crash handler)
 */
typedef struct {
    uint32_t stack_top_address;         // Top of stack for crash dumps
    ulogger_flags_level_t flags_level;  // Flags and level configuration
    const mem_ctl_block_t *mcb_param;   // Memory control block array
    uint32_t mcb_len;                   // Length of memory control block array
    uint16_t pretrigger_log_count;      // Number of pretrigger logs to keep in buffer
    uint8_t *pretrigger_buffer;         // Pointer to pretrigger buffer (user-allocated)
    uint16_t pretrigger_buffer_size;    // Size of pretrigger buffer in bytes
    
    // Crash dump header metadata
    uint32_t application_id;        // Application identifier
    const char *git_hash;           // Git commit hash (max 40 chars)
    const char *device_type;        // Device type string (max 64 chars)
    const char *device_serial;      // Device serial number (max 64 chars)
    const char *version_string;     // Firmware version string (max 64 chars)
} ulogger_config_t;

// ============================================================================
// Public API Functions
// ============================================================================

/**
 * @brief Initialize the uLogger system
 * @param config Pointer to uLogger configuration structure
 * 
 * This function must be called once during application initialization before
 * using any logging functions. It initializes the memory subsystem, pretrigger
 * buffer, and crash dump infrastructure.
 */
void ulogger_init(ulogger_config_t *config);

/**
 * @brief Main logging function
 * @param debug_module Bit mask indicating the module (0x01-0x80000000)
 * @param debug_level Log level (use ULOGGER_DEBUG_LEVEL enum values)
 * @param fmt Printf-style format string
 * @param ... Variable arguments matching the format string
 */
void ulogger_log(uint32_t debug_module, uint8_t debug_level, const char *fmt, ...);

/**
 * @brief Set the flags and level configuration
 * @param flags_level Pointer to flags/level configuration structure
 * 
 * @note Before calling this function, you must initialize the memory subsystem
 *       by calling Mem_init() with your memory control block configuration.
 *       Example:
 *       @code
 *       Mem_init(ulogger_mem_ctl_block, sizeof(ulogger_mem_ctl_block));
 *       @endcode
 */
void ulogger_set_flags_level(ulogger_flags_level_t *flags_level);

/**
 * @brief Clear all logs in non-volatile memory
 */
void ulogger_clear_nv_logs(void);

/**
 * @brief Get current non-volatile log buffer usage in bytes
 * @return Total bytes needed for transmission (13-byte header + log data), or 0 if no logs
 * 
 * @note This function returns the total size required for buffer allocation when using
 *       ulogger_read_nv_logs_with_header(). The returned size includes both the 13-byte
 *       header and the actual log data.
 */
uint32_t ulogger_get_nv_log_usage(void);

/**
 * @brief Get the size of the core dump stored in non-volatile memory
 * @return Size of the core dump in bytes, or 0 if no valid dump exists
 */
uint32_t ulogger_get_core_dump_size(void);

/**
 * @brief Read NV logs with 13-byte header prepended
 * 
 * This function simplifies log transmission by automatically prepending a 13-byte
 * header containing metadata (version, session token, sequence number, size, checksum)
 * before the log data. The header format matches the protocol expected by ulogger_extract.py.
 * 
 * @param dest Destination buffer (must be at least 13 + log data size bytes)
 * @param max_bytes Maximum bytes to write to dest
 * @param session_token Session identifier to include in header (typically from cloud/server)
 * @return Total bytes written (header + log data), or 0 if no logs available
 * 
 * @note The returned size includes both the 13-byte header and log data.
 *       Call ulogger_get_nv_log_usage() first to determine the required buffer size.
 */
uint32_t ulogger_read_nv_logs_with_header(void *dest, uint32_t max_bytes, uint32_t session_token);

/**
 * @brief Flush all pretrigger logs to NV memory
 * 
 * Call this function to ensure all buffered logs in the pretrigger buffer (RAM) are
 * written to non-volatile memory before reading or transmitting logs. This is typically
 * called before ulogger_get_nv_log_usage() and ulogger_read_nv_logs_with_header().
 * 
 * @note This function should be called before attempting to read logs for transmission
 *       to ensure all recent logs are persisted to NV memory.
 */
void ulogger_flush_pretrigger_to_nv(void);

// ============================================================================
// Application-Provided Functions
// ============================================================================

/**
 * @brief Read data from non-volatile memory
 * 
 * This function must be implemented by the user application to read data from
 * non-volatile memory (e.g., Flash, EEPROM, or persistent storage).
 * 
 * @param address Starting address in non-volatile memory to read from
 * @param data Pointer to buffer where read data will be stored
 * @param size Number of bytes to read
 * @return true on success, false on error
 */
bool ulogger_nv_mem_read(uint32_t address, uint8_t *data, uint32_t size);

/**
 * @brief Write data to non-volatile memory
 * 
 * This function must be implemented by the user application to write data to
 * non-volatile memory (e.g., Flash, EEPROM, or persistent storage).
 * 
 * @param address Starting address in non-volatile memory to write to
 * @param data Pointer to buffer containing data to write
 * @param size Number of bytes to write
 * @return true on success, false on error
 */
bool ulogger_nv_mem_write(uint32_t address, const uint8_t *data, uint32_t size);

/**
 * @brief Erase a region of non-volatile memory
 * 
 * This function must be implemented by the user application to erase a region
 * of non-volatile memory (e.g., Flash sector erase). The implementation should
 * erase all data in the specified address range, preparing it for new writes.
 * 
 * @param address Starting address of the region to erase
 * @param size Size of the region to erase in bytes
 * @return true on success, false on error
 */
bool ulogger_nv_mem_erase(uint32_t address, uint32_t size);

#ifdef __cplusplus
}
#endif

// Check definitions
/**
 * Calculated memory length based on address range
 */
#define ULOGGER_LOG_MEM_LEN (ULOGGER_LOG_NV_END_ADDRESS - ULOGGER_LOG_NV_START_ADDRESS + 1)

// Generate build error if NV memory size is insufficient
// Validate minimum log data length
#if ULOGGER_LOG_MEM_LEN < 200
#error "ULOGGER_LOG_MEM_LEN must be at least 200 bytes"
#endif

#endif // ULOGGER_H
