/******************************************************************************
 * File: retarget_lvgl.h
 *
 * Description: Public interface for retarget-lvgl library.
 *              Routes printf output to an LVGL label on screen via a
 *              thread-safe ring buffer with ANSI escape sequence support.
 *
 *              Supports two modes:
 *              1. Same-core: _write() on CM55 → display (standalone)
 *              2. Cross-core: retarget-ipc feed → display (with retarget-ipc)
 *
 * Usage:
 *   #include "retarget_lvgl.h"
 *   // After LVGL is initialized:
 *   retarget_lvgl_init(lv_screen_active());
 *   printf("Hello world!\n");  // appears on display
 *
 * Copyright 2026 Infineon Technologies AG
 * SPDX-License-Identifier: Apache-2.0
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
 * Creates a full-screen scrollable label on the given parent and registers
 * an LVGL timer that drains the ring buffer into the widget.
 * After this call, all printf() output goes to the display.
 *
 * If retarget-ipc is also present (RETARGET_LVGL_IPC_ENABLED defined),
 * the drain timer will also consume data from the IPC shared buffer.
 *
 * @param parent  LVGL object to place the terminal on (e.g., lv_screen_active())
 *
 * @pre LVGL initialized (lv_init() + display driver)
 * @pre FreeRTOS scheduler running or about to start
 */
void retarget_lvgl_init(lv_obj_t *parent);

/**
 * @brief Clear the terminal display.
 *
 * @note Must be called from LVGL task context.
 */
void retarget_lvgl_clear(void);

/**
 * @brief Get the terminal label widget for custom styling.
 *
 * @note Any modifications must be done from LVGL task context.
 * @return Pointer to the lv_label object, or NULL if not initialized.
 */
lv_obj_t *retarget_lvgl_get_label(void);

#ifdef RETARGET_LVGL_UART_ENABLED
#include "mtb_hal.h"

/**
 * @brief Enable dual output: printf goes to both LVGL display and UART.
 *
 * @param uart  Pointer to an initialized and enabled mtb_hal_uart_t object.
 */
void retarget_lvgl_set_uart(mtb_hal_uart_t *uart);
#endif

#if defined(__cplusplus)
}
#endif

#endif /* RETARGET_LVGL_H */
