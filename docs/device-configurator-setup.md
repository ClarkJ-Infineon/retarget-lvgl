# Device Configurator Setup — GFXSS for retarget-lvgl

The GFXSS (Graphics Subsystem) personality in Device Configurator generates critical hardware initialization structures. **Without it, the build fails with undefined symbols** (`GFXSS_HW`, `GFXSS_DC_IRQ`, `GFXSS_GPU_IRQ`, `GFXSS_config`, etc.).

## Recommended: Copy BSP from Reference Project

The fastest path is to copy the pre-configured BSP from a working LVGL project:

1. Copy `bsps/TARGET_APP_KIT_PSE84_EVAL_EPC2/` from the reference project (or `mtb-example-psoc-edge-lvgl-demo`) into your workspace root
2. Run `make getlibs` to regenerate linker scripts and config sources
3. Clean and rebuild: `make clean && make build`

This avoids manually configuring over a dozen coordinated GFXSS settings.

---

## Manual Configuration (from scratch)

Open `bsps/TARGET_*/config/design.modus` in Device Configurator.

### A. GFXSS Personality (CM55-only)

**Peripherals** tab → **Graphics Subsystem (GFXSS)**:

| Parameter | Value (4.3" Waveshare) | Notes |
|---|---|---|
| Display Type | `GFX_DISP_TYPE_DSI_DPI` | MIPI DSI interface |
| Display Width | `832` | Stride-aligned (actual 800, padded for GPU DMA) |
| Display Height | `480` | |
| FPS | `60` | |
| Format | `vivD24` | DSI pixel format |
| GPU Enabled | `true` | Required for VG-Lite acceleration |
| Max Per-Lane Mbps | `850` | DSI lane bandwidth |
| DSI Lanes | `1` | Waveshare 4.3" uses 1 lane |
| Mode | `VID_MODE_TYPE_BURST` | Video mode |
| LP Mode (cmd/video) | Both `true` | Low-power mode for commands/video |
| Layer 0 Enabled | `true` | Primary framebuffer layer |
| Layer 0 Format | `vivRGB565` | Matches `LV_COLOR_DEPTH 16` |
| Layer 0 Width/Height | `832` / `480` | Same as display |
| H-Timing | HBP=20, HFP=210, HSYNC=10 | Horizontal blanking |
| V-Timing | VBP=20, VFP=20, VSYNC=5 | Vertical blanking |

### B. I2C Controller (SCB0) — Display/Touch

**Peripherals** tab → **SCB0** → I2C:

| Parameter | Value | Notes |
|---|---|---|
| Mode | `CY_SCB_I2C_MASTER` | Master mode for touch controller |
| Data Rate | `100` kbps | Standard-mode I2C |
| Enable Rx/Tx FIFO | Both `true` | |
| Alias | `CYBSP_I2C_CONTROLLER` | Referenced by `display_i2c_config.h` |

Pin assignments (Pins tab):
- **P8.0** → `scb[0].i2c_scl[0]`
- **P8.1** → `scb[0].i2c_sda[0]`

### C. Debug UART (SCB2) — Optional

Only required if using `RETARGET_LVGL_UART_ENABLED`:

| Parameter | Value | Notes |
|---|---|---|
| Mode | UART Standard | |
| Baud Rate | `115200` | |
| Data Width | `8` | |
| Alias | `CYBSP_DEBUG_UART` | |

Pin assignments:
- **P6.4** → UART TX
- **P6.5** → UART RX

### D. Memory Configuration — SOCMEM Regions

Open the **Memory Configuration** section. The graphics subsystem requires a dedicated `gfx_mem` region in SOCMEM for framebuffers and VG-Lite GPU heap:

| Region | Memory | Offset | Size | Description |
|---|---|---|---|---|
| `m55_code_secondary` | SOCMEM_RAM | `0x00000000` | `0x00060000` (384 KB) | CM55 code |
| `m55_data_secondary` | SOCMEM_RAM | `0x00060000` | `0x00160000` (1.375 MB) | CM55 data/heap |
| `m33_m55_shared` | SOCMEM_RAM | `0x001C0000` | `0x00040000` (256 KB) | IPC shared memory |
| `gfx_mem` | SOCMEM_RAM | `0x00200000` | `0x00300000` (3 MB) | **Framebuffers + GPU heap** |

**Key constraints:**
- SOCMEM regions must be contiguous and sum to `0x500000` (5 MB total)
- `gfx_mem` sizing: `(832 × 480 × 2 bytes × 2 buffers) + 256KB VGLite heap + headroom ≈ 2 MB minimum`; 3 MB provides healthy headroom
- If your project needs more CM55 data space, shrink `gfx_mem` to 2 MB (`0x200000`) — but NOT smaller
- When rebalancing regions: shrink one region and save, then grow another and save (avoids overlap validation errors)

### E. Clock Configuration

The GFXSS requires two clock connections (Clocks tab):

| Clock Source | Destination | Purpose |
|---|---|---|
| `srss[0].clock[0].hfclk[12]` | `gfxss[0].clk_ref_mipidsi[0]` | MIPI DSI reference clock |
| HF clock (peri[1] domain) | `gfxss[0].clk_hf[0]` | GPU/DC core clock |

These are typically pre-configured in the standard BSP for KIT_PSE84_EVAL_EPC2. If starting from a bare BSP, verify these nets exist.

---

## Generated Output

After saving `design.modus`, Device Configurator generates:

| File | Key symbols |
|---|---|
| `cycfg_peripherals.h/.c` | `GFXSS_HW`, `GFXSS_GPU_IRQ`, `GFXSS_DC_IRQ`, `GFXSS_MIPIDSI_IRQ`, `GFXSS_config`, `GFXSS_graphics_layer`, `CYBSP_I2C_CONTROLLER_HW`, `CYBSP_DEBUG_UART_HW` |
| `cycfg_pins.h/.c` | Pin mux assignments for I2C and UART |
| Linker scripts | Memory region placement for `.cy_gpu_buf`, `.cy_socmem_data` |

**After any Device Configurator change:** Always run `make clean && make build` (clean build required for linker script changes).

---

## Waveshare 7.0" DSI (1024×600)

If using the 7.0" display instead, change:

| Parameter | 7.0" Value |
|---|---|
| Display Width | `1024` |
| Display Height | `600` |
| DSI Lanes | `2` |
| Layer 0 Width/Height | `1024` / `600` |
| `gfx_mem` size | `0x400000` (4 MB recommended) |
| Makefile define | `MTB_DISPLAY_WS7P0DSI` |

Also use `deps/display-dsi-waveshare-7-0-lcd.mtb` instead of the 4.3" variant.
