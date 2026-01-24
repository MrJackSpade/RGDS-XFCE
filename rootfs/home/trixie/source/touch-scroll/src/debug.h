/**
 * @file debug.h
 * @brief Debug logging macros and globals.
 */

#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

/** Global flag: 1 if debug logging is enabled, 0 otherwise. */
extern int g_debug_mode;

/**
 * @brief Log a debug message to stderr if g_debug_mode is set.
 * Usage: DEBUG_LOG("Value: %d\n", val);
 */
#define DEBUG_LOG(fmt, ...) \
    do { \
        if (g_debug_mode) { \
            fprintf(stderr, "[DEBUG] " fmt, ##__VA_ARGS__); \
        } \
    } while (0)

#endif
