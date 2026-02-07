#ifndef ULOGGER_CONFIG_H
#define ULOGGER_CONFIG_H

/**
 * @file ulogger_config.h
 * @brief User configuration settings for uLogger
 *
 * This file contains all user-configurable settings for the uLogger library.
 * Modify these definitions to match your application requirements and hardware.
 */

// ============================================================================
// Debug Module Definitions (USER-DEFINED)
// ============================================================================

/**
 * Define your debug modules using the X-macro pattern.
 * Each entry: X(NAME, bit_position)
 * - NAME: Module identifier (will generate NAME_MODULE enum value)
 * - bit_position: Bit position (0-31) for the module flag
 *
 * Example customization:
 * #define ULOGGER_DEBUG_MODULE_LIST(X) \
 *   X(MAIN,    0) \
 *   X(NETWORK, 1) \
 *   X(STORAGE, 2)
 */
#define ULOGGER_DEBUG_MODULE_LIST(X) \
  X(MAIN,   0) \
  X(SYSTEM, 1) \
  X(COMM,   2) \
  X(SENSOR, 3) \
  X(POWER,  4)


// ============================================================================
// Non-Volatile Memory Configuration (USER-DEFINED)
// ============================================================================

/**
 * Non-volatile memory addresses for persistent logging
 * Customize these for your target hardware
 * 
 * If using internal flash, ensure these addresses do not overlap with your
 * application code or other critical data.
 */
// #define ULOGGER_LOG_NV_START_ADDRESS       0
// #define ULOGGER_LOG_NV_END_ADDRESS         0x3FFF
// #define ULOGGER_EXCEPTION_NV_START_ADDRESS 0x4000
// #define ULOGGER_EXCEPTION_NV_END_ADDRESS   0x6FFF

#endif // ULOGGER_CONFIG_H
