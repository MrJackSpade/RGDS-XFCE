/**
 * @file gesture_utils.c
 * @brief Utility implementation.
 */

#include "gesture_utils.h"
#include <stddef.h>

/**
 * @brief Get the current time in milliseconds.
 * @return long long Current time in ms.
 */
long long current_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}
