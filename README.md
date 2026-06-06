# retarget-lvgl — Printf to LVGL Display

Repurpose a DSI display as a serial terminal. This library overrides Newlib's `_write()` to route all `printf()` output to an LVGL label — no graphics programming required.

**Primary use case:** Non-graphics PSOC Edge applications that want printf output on the attached display instead of (or in addition to) a serial terminal.

## Features

- **Zero-config printf**: After initialization, all `printf()` calls render on the display
- **Thread-safe**: Ring buffer with FreeRTOS critical sections — callable from any task or ISR
- **Cross-core support**: Integrates with `retarget-ipc` to display CM33_NS printf on CM55 display
- **ANSI escape support**: `\x1b[2J` (clear), `\x1b[K` (erase line), `\r` (line overwrite)
- **Compile-time theming**: Colors, font, line spacing configurable via Makefile defines
- **Optional dual output**: Simultaneously output to both display and UART
- **Non-blocking**: Ring buffer decouples printf callers from LVGL rendering
- **Auto-scroll**: Container scrolls to show newest content
- **Overflow handling**: Text trims from front when buffer full (8192 char capacity)

## Target Hardware

| Board | Display | Status |
|---|---|---|
| KIT_PSE84_EVAL_EPC2 | Waveshare 4.3" DSI (800×480) | ✅ Tested |
| KIT_PSE84_EVAL_EPC2 | Waveshare 7.0" DSI (1024×600) | ✅ Supported |
| KIT_PSE84_AI | Waveshare 4.3" / 7.0" | ✅ Supported |

---

## Quick Start (Same-Core: CM55 Only)

For applications where printf and LVGL both run on CM55.

### 1. Add Library

**`proj_cm55/deps/retarget-lvgl.mtb`:**
```
https://github.com/ClarkJ-Infineon/retarget-lvgl#main#$$ASSET_REPO$$/retarget-lvgl/main
```

Remove `retarget-io.mtb` if present (conflicting `_write()`).

Run `make getlibs` from workspace root.

### 2. Configure Makefile

```makefile
# proj_cm55/Makefile
COMPONENTS += RETARGET_LVGL_DISPLAY FREERTOS RTOS_AWARE

# Include paths
INCLUDES += $(SEARCH_retarget-lvgl)/include
INCLUDES += $(SEARCH_retarget-lvgl)/COMPONENT_RETARGET_LVGL_DISPLAY
INCLUDES += $(SEARCH_retarget-lvgl)/COMPONENT_RETARGET_LVGL_DISPLAY/lvgl_port

# Display selection
DEFINES += MTB_DISPLAY_W4P3INCH_RPI MTB_CTP_FT5406

# Optional: UART dual output
DEFINES += RETARGET_LVGL_UART_ENABLED

# LVGL source management (see docs/lvgl-cy-ignore.md for full list)
CY_IGNORE += $(SEARCH_lvgl)/tests $(SEARCH_lvgl)/examples
CY_IGNORE += $(SEARCH_lvgl)/src/others/vg_lite_tvg $(SEARCH_lvgl)/src/libs/thorvg
CY_IGNORE += $(SEARCH_lvgl)/src/draw/vg_lite/lv_draw_vg_lite.c
CY_IGNORE += $(SEARCH_lvgl)/src/draw/vg_lite/lv_vg_lite_utils.c
CY_IGNORE += $(SEARCH_lvgl)/src/draw/vg_lite/lv_draw_vg_lite_img.c
CY_IGNORE += $(SEARCH_lvgl)/src/draw/sw/blend/helium
CY_IGNORE += $(SEARCH_lvgl)/src/draw/sw/blend/neon
CY_IGNORE += $(SEARCH_lvgl)/src/core/lv_refr.c
```

### 3. Copy lv_conf.h

Copy `COMPONENT_RETARGET_LVGL_DISPLAY/config/lv_conf.h` to your `proj_cm55/` root.

### 4. Add Required Dependencies

```
proj_cm55/deps/lvgl.mtb
proj_cm55/deps/display-dsi-waveshare-4-3-lcd.mtb
proj_cm55/deps/touch-ctp-ft5406.mtb
proj_cm55/deps/freertos.mtb
```

Also requires `COMPONENTS += GFXSS` in `common.mk` with GFXSS configured in Device Configurator (see docs/).

### 5. Use in main.c

```c
#include "retarget_lvgl.h"
#include "gfx_task.h"

int main(void)
{
    cybsp_init();
    __enable_irq();

    #ifdef RETARGET_LVGL_UART_ENABLED
    /* Optional: init UART for dual output */
    // ... UART init ...
    retarget_lvgl_set_uart(&debug_uart_obj);
    #endif

    /* Create GFX task — handles all display + LVGL + terminal init */
    xTaskCreate(gfx_task, GFX_TASK_NAME, GFX_TASK_STACK_SIZE,
                NULL, GFX_TASK_PRIORITY, &rtos_cm55_gfx_task_handle);

    vTaskStartScheduler();
}
```

After boot (~2.5s for DSI panel power-up), all `printf()` output appears on the display.

---

## Cross-Core Mode (CM33_NS → CM55 Display)

For code examples where the application runs on CM33_NS but you want printf on the display.

### Additional Requirements

- `retarget-ipc` library added to **both** core projects
- `retarget-lvgl` added to CM55 project only

### CM33_NS Setup

```makefile
# proj_cm33_ns/Makefile
COMPONENTS += RETARGET_IPC_HOST
INCLUDES += $(SEARCH_retarget-ipc)/include
# Remove retarget-io.mtb from deps/
```

```c
// proj_cm33_ns/main.c
#include "retarget_ipc.h"

int main(void) {
    cybsp_init();
    retarget_ipc_host_init();
    // printf now goes to shared memory → CM55 display
    printf("Hello from CM33!\n");
    vTaskStartScheduler();
}
```

### CM55 Setup

```makefile
# proj_cm55/Makefile
COMPONENTS += RETARGET_LVGL_DISPLAY RETARGET_IPC_DEVICE
DEFINES += RETARGET_LVGL_IPC_ENABLED
INCLUDES += $(SEARCH_retarget-ipc)/include
INCLUDES += $(SEARCH_retarget-lvgl)/include
# ... (same display config as same-core mode)
```

The `retarget-lvgl` drain timer automatically polls `retarget_ipc_read()` when `RETARGET_LVGL_IPC_ENABLED` is defined.

---

## Compile-Time Configuration

| Define | Default | Description |
|---|---|---|
| `RETARGET_LVGL_BG_COLOR` | `lv_color_black()` | Terminal background color |
| `RETARGET_LVGL_TEXT_COLOR` | `lv_color_make(0x6C, 0xB4, 0xAD)` | Text color |
| `RETARGET_LVGL_FONT` | `&lv_font_unscii_16` | Font (must be monospace) |
| `RETARGET_LVGL_LINE_SPACE` | `4` | Pixels between lines |
| `RETARGET_LVGL_UART_ENABLED` | *(not defined)* | Enable dual output |
| `RETARGET_LVGL_IPC_ENABLED` | *(not defined)* | Enable retarget-ipc integration |

### Theme Examples

```makefile
# Classic green terminal:
DEFINES+='RETARGET_LVGL_TEXT_COLOR=lv_color_make(0x00,0xFF,0x00)'

# Amber on dark blue:
DEFINES+='RETARGET_LVGL_TEXT_COLOR=lv_color_make(0xFF,0xB0,0x00)'
DEFINES+='RETARGET_LVGL_BG_COLOR=lv_color_make(0x1A,0x1A,0x2E)'
```

---

## Architecture

```
CM33_NS printf("hello")                      CM55 printf("world")
    │                                              │
    ▼                                              ▼
  _write() [retarget-ipc HOST]               _write() [retarget-lvgl]
    │                                              │
    ▼                                              ▼
  shared memory ring buffer ──────────►  LVGL drain timer (50ms)
                                               │
                                               ▼
                                         process ANSI sequences
                                               │
                                               ▼
                                         lv_label → display
```

---

## Memory Usage

| Resource | Size | Notes |
|----------|------|-------|
| Local ring buffer | 4,096 bytes | BSS (CM55 static) |
| Text buffer | 8,193 bytes | BSS (CM55 static) |
| Drain buffer | 4,096 bytes | BSS (CM55 static) |
| LVGL + VGLite heap | ~1.8 MB | SOCMEM `.cy_gpu_buf` section |
| GFX task stack | 4,096 words | FreeRTOS heap |

---

## File Structure

```
retarget-lvgl/
├── include/
│   └── retarget_lvgl.h              ← Public API (both cores include)
├── COMPONENT_RETARGET_LVGL_DISPLAY/ ← CM55: compiled when component active
│   ├── retarget_lvgl_display.c      ← Core: ring buffer + ANSI + LVGL label
│   ├── gfx_task.c                   ← GFXSS + LVGL + terminal init task
│   ├── gfx_task.h
│   ├── app_config.h                 ← Error handler macro
│   ├── lvgl_port/                   ← Board-specific display drivers
│   │   ├── lv_port_disp.c/h
│   │   ├── lv_port_indev.c/h
│   │   ├── display_i2c_config.h
│   │   └── lv_draw_vg_lite*.c      ← VGLite GPU overrides
│   └── config/
│       └── lv_conf.h                ← Template (copy to proj_cm55/)
└── docs/
    └── device-configurator-setup.md
```

---

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| Linker: multiple `_write` | `retarget-io` library present | Remove `retarget-io.mtb`, run `make getlibs` |
| No display output | GFX task not initialized yet | Normal — takes ~2.5s for DSI panel boot |
| CM33 printf not showing | IPC not configured | Add `RETARGET_LVGL_IPC_ENABLED` define |
| Build error: GFXSS symbols | BSP missing GFXSS config | See docs/device-configurator-setup.md |
| Garbled display | MPU not configured for gfx_mem | Ensure SOCMEM gfx_mem is non-cacheable |

## License

Apache-2.0
