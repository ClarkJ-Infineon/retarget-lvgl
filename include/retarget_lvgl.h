/******************************************************************************
 * @file retarget_lvgl.h
 * @brief Public interface for retarget-lvgl — printf to LVGL display terminal.
 *
 * @details
 * Routes printf output to an LVGL label widget via a thread-safe ring buffer
 * with ANSI escape sequence support. Supports two operating modes:
 *
 * 1. **Same-core mode**: `_write()` on CM55 → ring buffer → LVGL label
 * 2. **Cross-core mode**: retarget-ipc feed from CM33 → LVGL label
 *
 * @par Quick Start:
 * @code
 * #include "retarget_lvgl.h"
 * // After LVGL is initialized:
 * retarget_lvgl_init(lv_screen_active());
 * printf("Hello world!\n");  // appears on display
 * @endcode
 *
 * @par Thread Safety:
 * - _write() uses FreeRTOS critical sections (task and ISR safe)
 * - LVGL updates happen only in the LVGL timer context (thread-safe)
 * - retarget_lvgl_init() must be called from LVGL task context
 *
 * @copyright Copyright 2025-2026 Clark Jarvis / Infineon Technologies AG
 * @license SPDX-License-Identifier: Apache-2.0
 ******************************************************************************/

#ifndef RETARGET_LVGL_H
#define RETARGET_LVGL_H

#include "lvgl.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * @brief Initialize the retarget-lvgl terminal display.
 *
 * Creates a full-screen scrollable container with a monospace label and
 * registers an LVGL timer that drains the ring buffer every 50ms.
 * After this call, all printf() output renders on the display.
 *
 * If `RETARGET_LVGL_IPC_ENABLED` is defined, also initializes the
 * retarget-ipc device side and drains cross-core data.
 *
 * @param[in] parent  LVGL object to host the terminal (e.g., lv_screen_active()).
 *
 * @pre  LVGL initialized with display driver (lv_init() + lv_port_disp_init()).
 * @pre  FreeRTOS scheduler running or about to start.
 * @pre  Must be called from LVGL task context.
 *
 * @note Printf output before this call is buffered in the ring buffer and
 *       will appear once the drain timer starts (no data loss up to 4096 bytes).
 */
void retarget_lvgl_init(lv_obj_t *parent);

/**
 * @brief Clear the terminal display.
 *
 * Resets the internal text buffer and updates the label to empty.
 * Equivalent to receiving ESC[2J.
 *
 * @pre Must be called from LVGL task context.
 */
void retarget_lvgl_clear(void);

/**
 * @brief Get the terminal label widget for custom styling.
 *
 * Allows advanced users to modify font, color, or other label properties
 * directly via LVGL API.
 *
 * @return Pointer to the lv_label object, or NULL if not yet initialized.
 *
 * @note Any modifications must be done from LVGL task context.
 */
lv_obj_t *retarget_lvgl_get_label(void);

/**
 * @brief Get the current length of the terminal text buffer.
 *
 * Useful for testing and diagnostics — allows verification of buffer
 * state without needing to inspect the display visually.
 *
 * @return Number of characters currently in the text buffer.
 */
uint32_t retarget_lvgl_get_text_len(void);

/**
 * @brief Get the number of lines currently displayed.
 *
 * Counts newline characters in the text buffer. Useful for test verification.
 *
 * @return Number of newlines in the current text buffer (line count - 1).
 */
uint32_t retarget_lvgl_get_line_count(void);

/**
 * @brief Get a read-only pointer to the current text buffer.
 *
 * @warning The returned pointer is only valid until the next drain timer
 *          fires. Copy the data if you need to retain it.
 *
 * @return Pointer to the null-terminated text buffer, or NULL if not initialized.
 */
const char *retarget_lvgl_get_text(void);

#ifdef RETARGET_LVGL_UART_ENABLED
#include "mtb_hal.h"

/**
 * @brief Enable dual output: printf goes to both LVGL display and UART.
 *
 * When set, the `_write()` function sends data to both the ring buffer
 * (for display) and the UART (for serial terminal). This enables
 * verification of display behavior via serial monitoring.
 *
 * @param[in] uart  Pointer to an initialized and enabled mtb_hal_uart_t.
 *                  Must remain valid for the lifetime of the application.
 *
 * @pre UART must be initialized and enabled.
 */
void retarget_lvgl_set_uart(mtb_hal_uart_t *uart);
#endif

#if defined(__cplusplus)
}
#endif

#endif /* RETARGET_LVGL_H */
