/******************************************************************************
 * File: retarget_lvgl_display.c
 *
 * @brief Retarget-lvgl display module — routes printf() output to an LVGL
 *        terminal label with ANSI escape sequence support.
 *
 * @details
 * Architecture:
 *   1. _write() places characters into a thread-safe ring buffer
 *      (callable from any FreeRTOS task or ISR context on this core).
 *   2. An LVGL timer callback (runs in LVGL task context) drains:
 *      - The local ring buffer (CM55 printf)
 *      - The IPC shared buffer (CM33_NS printf, if retarget-ipc present)
 *   3. Drain processes ANSI escape sequences and updates the label.
 *   4. Optionally, _write() also sends characters to a UART for dual output.
 *
 * Supported ANSI sequences:
 *   - ESC[2J        — Clear screen
 *   - ESC[H / ESC[;H — Cursor home (consumed after clear)
 *   - ESC[K / ESC[0K — Erase to end of line
 *   - ESC[2K        — Erase entire line
 *   - \r (bare)     — Carriage return (overwrite current line)
 *   - All other ESC[...X sequences are silently stripped.
 *
 * Copyright 2026 Infineon Technologies AG
 * SPDX-License-Identifier: Apache-2.0
 ******************************************************************************/

#include "retarget_lvgl.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdio.h>

/* Optional IPC integration */
#ifdef RETARGET_LVGL_IPC_ENABLED
#include "retarget_ipc.h"
#endif

/*******************************************************************************
 * Macros
 ******************************************************************************/
#define RING_BUFFER_SIZE    (4096U)
#define DRAIN_PERIOD_MS     (50U)
#define TEXTAREA_MAX_LEN    (8192U)

/* Theme defaults — override via DEFINES in your Makefile */
#ifndef RETARGET_LVGL_BG_COLOR
#define RETARGET_LVGL_BG_COLOR       lv_color_black()
#endif
#ifndef RETARGET_LVGL_TEXT_COLOR
#define RETARGET_LVGL_TEXT_COLOR      lv_color_make(0x6C, 0xB4, 0xAD)
#endif
#ifndef RETARGET_LVGL_FONT
#define RETARGET_LVGL_FONT           &lv_font_unscii_16
#endif
#ifndef RETARGET_LVGL_LINE_SPACE
#define RETARGET_LVGL_LINE_SPACE      4
#endif

/*******************************************************************************
 * Ring Buffer (local — for CM55 printf)
 ******************************************************************************/
static volatile uint8_t ring_buf[RING_BUFFER_SIZE];
static volatile uint32_t ring_head = 0;
static volatile uint32_t ring_tail = 0;

static void ring_buf_write(const uint8_t *data, uint32_t len)
{
    UBaseType_t saved_interrupt_status = taskENTER_CRITICAL_FROM_ISR();

    for (uint32_t i = 0; i < len; i++)
    {
        uint32_t next_head = (ring_head + 1U) % RING_BUFFER_SIZE;
        if (next_head == ring_tail)
        {
            break;  /* Buffer full — drop */
        }
        ring_buf[ring_head] = data[i];
        ring_head = next_head;
    }

    taskEXIT_CRITICAL_FROM_ISR(saved_interrupt_status);
}

static uint32_t ring_buf_read(uint8_t *out, uint32_t max_len)
{
    UBaseType_t saved_interrupt_status = taskENTER_CRITICAL_FROM_ISR();

    uint32_t count = 0;
    while ((ring_tail != ring_head) && (count < max_len))
    {
        out[count] = ring_buf[ring_tail];
        ring_tail = (ring_tail + 1U) % RING_BUFFER_SIZE;
        count++;
    }

    taskEXIT_CRITICAL_FROM_ISR(saved_interrupt_status);
    return count;
}

/*******************************************************************************
 * Text Buffer + LVGL Widgets
 ******************************************************************************/
#define TEXT_BUF_SIZE       (TEXTAREA_MAX_LEN + 1U)
static char text_buf[TEXT_BUF_SIZE];
static uint32_t text_len = 0;

static lv_obj_t *terminal_container = NULL;
static lv_obj_t *terminal_label = NULL;

/*******************************************************************************
 * ANSI Escape Processing
 ******************************************************************************/
static uint32_t strip_ansi_clear(uint8_t *buf, uint32_t len)
{
    /* Look for the last occurrence of ESC[2J */
    uint32_t last_clear = UINT32_MAX;
    for (uint32_t i = 0; i + 3 < len; i++)
    {
        if (buf[i] == 0x1B && buf[i+1] == '[' && buf[i+2] == '2' && buf[i+3] == 'J')
        {
            last_clear = i + 4U;
        }
    }

    if (last_clear != UINT32_MAX)
    {
        text_buf[0] = '\0';
        text_len = 0;

        /* Skip ESC[;H or ESC[H if immediately follows */
        if (last_clear + 3 <= len &&
            buf[last_clear] == 0x1B && buf[last_clear+1] == '[')
        {
            if (buf[last_clear+2] == 'H')
            {
                last_clear += 3U;
            }
            else if (last_clear + 4 <= len &&
                     buf[last_clear+2] == ';' && buf[last_clear+3] == 'H')
            {
                last_clear += 4U;
            }
        }

        uint32_t remaining = len - last_clear;
        if (remaining > 0)
        {
            memmove(buf, buf + last_clear, remaining);
        }
        return remaining;
    }

    /* No clear — strip other ESC[ sequences, convert erase-line to \r */
    uint32_t out = 0;
    for (uint32_t i = 0; i < len; i++)
    {
        if (buf[i] == 0x1B && (i + 1) < len && buf[i+1] == '[')
        {
            uint32_t seq_start = i;
            i += 2;
            while (i < len && (buf[i] < 0x40 || buf[i] > 0x7E))
            {
                i++;
            }
            if (i >= len)
            {
                break;
            }

            if (buf[i] == 'K')
            {
                uint8_t param = '0';
                if (i == seq_start + 3)
                {
                    param = buf[seq_start + 2];
                }
                if (param == '0' || param == '2' || i == seq_start + 2)
                {
                    buf[out++] = '\r';
                }
            }
            continue;
        }
        buf[out++] = buf[i];
    }
    return out;
}

/*******************************************************************************
 * Drain Timer — processes buffer data and updates display
 ******************************************************************************/
static uint8_t drain_buf[RING_BUFFER_SIZE];

static void process_drain_data(uint8_t *buf, uint32_t n)
{
    if (n == 0)
    {
        return;
    }

    /* Process ANSI escape sequences */
    n = strip_ansi_clear(buf, n);
    if (n == 0)
    {
        lv_label_set_text_static(terminal_label, text_buf);
        lv_obj_scroll_to_y(terminal_container, LV_COORD_MAX, LV_ANIM_OFF);
        return;
    }

    /* Append to text buffer with \r handling (line overwrite) */
    for (uint32_t i = 0; i < n; i++)
    {
        if (buf[i] == '\r')
        {
            if ((i + 1) < n && buf[i + 1] == '\n')
            {
                continue;  /* \r\n → just \n */
            }
            /* Bare \r: truncate back to start of current line */
            while (text_len > 0 && text_buf[text_len - 1] != '\n')
            {
                text_len--;
            }
            text_buf[text_len] = '\0';
            continue;
        }

        /* Trim from front if buffer is about to overflow */
        if (text_len + 1 >= TEXT_BUF_SIZE)
        {
            uint32_t keep = TEXT_BUF_SIZE / 2U;
            uint32_t discard = text_len - keep;
            memmove(text_buf, text_buf + discard, keep);
            text_len = keep;
        }

        text_buf[text_len++] = (char)buf[i];
    }
    text_buf[text_len] = '\0';
}

static void lvgl_drain_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (terminal_label == NULL)
    {
        return;
    }

    bool updated = false;

    /* 1. Drain local ring buffer (CM55 printf) */
    uint32_t n = ring_buf_read(drain_buf, sizeof(drain_buf) - 1U);
    if (n > 0)
    {
        process_drain_data(drain_buf, n);
        updated = true;
    }

    /* 2. Drain IPC buffer (CM33_NS printf, if retarget-ipc present) */
#ifdef RETARGET_LVGL_IPC_ENABLED
    if (retarget_ipc_is_active())
    {
        n = retarget_ipc_read(drain_buf, sizeof(drain_buf) - 1U);
        if (n > 0)
        {
            process_drain_data(drain_buf, n);
            updated = true;
        }
    }
#endif

    /* Update label and scroll if anything was processed */
    if (updated)
    {
        lv_label_set_text_static(terminal_label, text_buf);
        lv_obj_scroll_to_y(terminal_container, LV_COORD_MAX, LV_ANIM_OFF);
    }
}

/*******************************************************************************
 * Public API
 ******************************************************************************/

void retarget_lvgl_init(lv_obj_t *parent)
{
    /* Ensure stdout is unbuffered — without retarget-io, newlib defaults to
     * line-buffered stdout which prevents no-newline printf from reaching
     * _write() immediately. Required for ANSI escape sequences to work. */
    setvbuf(stdout, NULL, _IONBF, 0);

    text_buf[0] = '\0';
    text_len = 0;

    /* Create scrollable container */
    terminal_container = lv_obj_create(parent);
    lv_obj_set_size(terminal_container, LV_PCT(100), LV_PCT(100));
    lv_obj_align(terminal_container, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_scroll_dir(terminal_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(terminal_container, LV_SCROLLBAR_MODE_AUTO);

    /* Terminal style */
    lv_obj_set_style_bg_color(terminal_container, RETARGET_LVGL_BG_COLOR, 0);
    lv_obj_set_style_border_width(terminal_container, 0, 0);
    lv_obj_set_style_pad_all(terminal_container, 4, 0);
    lv_obj_set_style_radius(terminal_container, 0, 0);

    /* Create label */
    terminal_label = lv_label_create(terminal_container);
    lv_label_set_long_mode(terminal_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(terminal_label, LV_PCT(100));
    lv_label_set_text_static(terminal_label, "");

    /* Terminal font and color */
    lv_obj_set_style_text_font(terminal_label, RETARGET_LVGL_FONT, 0);
    lv_obj_set_style_text_color(terminal_label, RETARGET_LVGL_TEXT_COLOR, 0);
    lv_obj_set_style_text_line_space(terminal_label, RETARGET_LVGL_LINE_SPACE, 0);

    /* Initialize IPC device side if available */
#ifdef RETARGET_LVGL_IPC_ENABLED
    retarget_ipc_device_init();
#endif

    /* Register drain timer */
    lv_timer_create(lvgl_drain_timer_cb, DRAIN_PERIOD_MS, NULL);
}

void retarget_lvgl_clear(void)
{
    if (terminal_label != NULL)
    {
        text_buf[0] = '\0';
        text_len = 0;
        lv_label_set_text_static(terminal_label, "");
    }
}

lv_obj_t *retarget_lvgl_get_label(void)
{
    return terminal_label;
}

uint32_t retarget_lvgl_get_text_len(void)
{
    return text_len;
}

uint32_t retarget_lvgl_get_line_count(void)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < text_len; i++)
    {
        if (text_buf[i] == '\n')
        {
            count++;
        }
    }
    return count;
}

const char *retarget_lvgl_get_text(void)
{
    if (terminal_label == NULL)
    {
        return NULL;
    }
    return (const char *)text_buf;
}

/*******************************************************************************
 * _write() Override — captures CM55 printf (same-core mode)
 ******************************************************************************/

#ifdef RETARGET_LVGL_UART_ENABLED
static mtb_hal_uart_t *uart_obj = NULL;

void retarget_lvgl_set_uart(mtb_hal_uart_t *uart)
{
    uart_obj = uart;
}
#endif

int _write(int fd, const char *ptr, int len)
{
    (void)fd;

    if (ptr == NULL || len <= 0)
    {
        return 0;
    }

    ring_buf_write((const uint8_t *)ptr, (uint32_t)len);

#ifdef RETARGET_LVGL_UART_ENABLED
    if (uart_obj != NULL)
    {
        size_t remaining = (size_t)len;
        const uint8_t *p = (const uint8_t *)ptr;
        while (remaining > 0)
        {
            size_t uart_len = remaining;
            mtb_hal_uart_write(uart_obj, (void *)p, &uart_len);
            p += uart_len;
            remaining -= uart_len;
        }
    }
#endif

    return len;
}
