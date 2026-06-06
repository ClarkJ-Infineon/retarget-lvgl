/******************************************************************************
 * File: gfx_task.c
 * Description: FreeRTOS task that initializes the PSOC Edge GFXSS (Display
 *              Controller, GPU, MIPI-DSI panel), VGLite, and LVGL.
 *              After initialization, runs the LVGL timer handler loop.
 ******************************************************************************/

#include "gfx_task.h"
#include "app_config.h"
#include "retarget_lvgl.h"

#include "vg_lite.h"
#include "vg_lite_platform.h"

#include "FreeRTOS.h"
#include "task.h"

#include "lvgl.h"

#if defined(MTB_DISPLAY_WS7P0DSI_RPI)
#include "mtb_disp_ws7p0dsi_drv.h"
#elif defined(MTB_DISPLAY_EK79007AD3)
#include "mtb_display_ek79007ad3.h"
#elif defined(MTB_DISPLAY_W4P3INCH_RPI)
#include "mtb_disp_dsi_waveshare_4p3.h"
#endif

#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "display_i2c_config.h"

#include <stdio.h>

/*******************************************************************************
 * Macros
 ******************************************************************************/
#define GPU_INT_PRIORITY            (3U)
#define DC_INT_PRIORITY             (3U)
#define APP_BUFFER_COUNT            (2U)
#define DEFAULT_GPU_CMD_BUFFER_SIZE ((64U) * (1024U))
#define GPU_TESSELLATION_BUFFER_SIZE ((MY_DISP_VER_RES) * 128U)
#define VGLITE_HEAP_SIZE            (((DEFAULT_GPU_CMD_BUFFER_SIZE) * \
                                      (APP_BUFFER_COUNT)) + \
                                     ((GPU_TESSELLATION_BUFFER_SIZE) * \
                                      (APP_BUFFER_COUNT)))
#define GPU_MEM_BASE                (0x0U)
#define I2C_CONTROLLER_IRQ_PRIORITY (2UL)
#define VG_PARAMS_POS               (0UL)

/*******************************************************************************
 * Global Variables
 ******************************************************************************/
/* Heap memory for VGLite */
CY_SECTION(".cy_gpu_buf") uint8_t contiguous_mem[VGLITE_HEAP_SIZE] = {0xFF};
volatile void *vglite_heap_base = &contiguous_mem;

TaskHandle_t rtos_cm55_gfx_task_handle = NULL;

/* DC IRQ Config */
cy_stc_sysint_t dc_irq_cfg =
{
    .intrSrc      = GFXSS_DC_IRQ,
    .intrPriority = DC_INT_PRIORITY
};

/* GPU IRQ Config */
cy_stc_sysint_t gpu_irq_cfg =
{
    .intrSrc      = GFXSS_GPU_IRQ,
    .intrPriority = GPU_INT_PRIORITY
};

cy_stc_scb_i2c_context_t disp_touch_i2c_controller_context;

cy_stc_sysint_t disp_touch_i2c_controller_irq_cfg =
{
    .intrSrc      = DISPLAY_I2C_CONTROLLER_IRQ,
    .intrPriority = I2C_CONTROLLER_IRQ_PRIORITY,
};

#if defined(MTB_DISPLAY_EK79007AD3)
mtb_display_ek79007ad3_pin_config_t ek79007ad3_pin_cfg =
{
    .reset_port = CYBSP_DISP_RST_PORT,
    .reset_pin  = CYBSP_DISP_RST_PIN,
};
#endif

/*******************************************************************************
 * Static Functions — Interrupt Handlers
 ******************************************************************************/
static void dc_irq_handler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    Cy_GFXSS_Clear_DC_Interrupt(GFXSS, &gfx_context);
    xTaskNotifyFromISR(rtos_cm55_gfx_task_handle, 1, eSetValueWithOverwrite,
                       &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void gpu_irq_handler(void)
{
    Cy_GFXSS_Clear_GPU_Interrupt(GFXSS, &gfx_context);
    vg_lite_IRQHandler();
}

static void disp_touch_i2c_controller_interrupt(void)
{
    Cy_SCB_I2C_Interrupt(DISPLAY_I2C_CONTROLLER_HW,
                         &disp_touch_i2c_controller_context);
}

/*******************************************************************************
 * Function Name: gfx_task
 ******************************************************************************/
void gfx_task(void *arg)
{
    CY_UNUSED_PARAMETER(arg);

    uint32_t time_till_next = 0;
    cy_en_sysint_status_t sysint_status = CY_SYSINT_SUCCESS;
    cy_en_gfx_status_t gfx_status = CY_GFX_SUCCESS;
    vg_lite_error_t vglite_status = VG_LITE_SUCCESS;

#if defined(MTB_DISPLAY_WS7P0DSI_RPI)
    cy_rslt_t status = CY_RSLT_SUCCESS;
#elif defined(MTB_DISPLAY_EK79007AD3)
    cy_en_mipidsi_status_t mipi_status = CY_MIPIDSI_SUCCESS;
#endif

    cy_en_scb_i2c_status_t i2c_result = CY_SCB_I2C_SUCCESS;

    /* ---- GFXSS init ---- */
#if defined(MTB_DISPLAY_WS7P0DSI_RPI)
    GFXSS_config.mipi_dsi_cfg = &mtb_disp_ws7p0dsi_dsi_config;
#elif defined(MTB_DISPLAY_EK79007AD3)
    GFXSS_config.mipi_dsi_cfg = &mtb_display_ek79007ad3_mipidsi_config;
#elif defined(MTB_DISPLAY_W4P3INCH_RPI)
    GFXSS_config.mipi_dsi_cfg = &mtb_disp_waveshare_4p3_dsi_config;
#endif

    GFXSS_config.dc_cfg->gfx_layer_config->width  = MY_DISP_HOR_RES;
    GFXSS_config.dc_cfg->gfx_layer_config->height = MY_DISP_VER_RES;
    GFXSS_config.dc_cfg->display_width            = MY_DISP_HOR_RES;
    GFXSS_config.dc_cfg->display_height           = MY_DISP_VER_RES;

    GFXSS_config.dc_cfg->gfx_layer_config->buffer_address    = frame_buffer1;
    GFXSS_config.dc_cfg->gfx_layer_config->uv_buffer_address = frame_buffer1;

    gfx_status = Cy_GFXSS_Init(GFXSS, &GFXSS_config, &gfx_context);

    if (CY_GFX_SUCCESS == gfx_status)
    {
        /* DC interrupt */
        sysint_status = Cy_SysInt_Init(&dc_irq_cfg, dc_irq_handler);
        if (CY_SYSINT_SUCCESS != sysint_status)
        {
            handle_app_error();
        }
        NVIC_EnableIRQ(GFXSS_DC_IRQ);

        /* GPU interrupt */
        sysint_status = Cy_SysInt_Init(&gpu_irq_cfg, gpu_irq_handler);
        if (CY_SYSINT_SUCCESS != sysint_status)
        {
            handle_app_error();
        }
        Cy_GFXSS_Enable_GPU_Interrupt(GFXSS);
        NVIC_EnableIRQ(GFXSS_GPU_IRQ);

        /* I2C for display/touch */
        i2c_result = Cy_SCB_I2C_Init(DISPLAY_I2C_CONTROLLER_HW,
                                     &DISPLAY_I2C_CONTROLLER_config,
                                     &disp_touch_i2c_controller_context);
        if (CY_SCB_I2C_SUCCESS != i2c_result)
        {
            handle_app_error();
        }
        sysint_status = Cy_SysInt_Init(&disp_touch_i2c_controller_irq_cfg,
                                       &disp_touch_i2c_controller_interrupt);
        if (CY_SYSINT_SUCCESS != sysint_status)
        {
            handle_app_error();
        }
        NVIC_EnableIRQ(disp_touch_i2c_controller_irq_cfg.intrSrc);
        Cy_SCB_I2C_Enable(DISPLAY_I2C_CONTROLLER_HW);

        vTaskDelay(pdMS_TO_TICKS(500));

        /* ---- Display panel init ---- */
#if defined(MTB_DISPLAY_WS7P0DSI_RPI)
        status = mtb_disp_ws7p0dsi_panel_init(DISPLAY_I2C_CONTROLLER_HW,
                                              &disp_touch_i2c_controller_context);
        if (CY_RSLT_SUCCESS != status)
        {
            handle_app_error();
        }

#elif defined(MTB_DISPLAY_EK79007AD3)
        mipi_status = mtb_display_ek79007ad3_init(GFXSS_GFXSS_MIPIDSI,
                                                  &ek79007ad3_pin_cfg);
        if (CY_MIPIDSI_SUCCESS != mipi_status)
        {
            handle_app_error();
        }

#elif defined(MTB_DISPLAY_W4P3INCH_RPI)
        /* Re-init I2C for 4.3-inch display (required by driver) */
        i2c_result = Cy_SCB_I2C_Init(DISPLAY_I2C_CONTROLLER_HW,
                                     &DISPLAY_I2C_CONTROLLER_config,
                                     &disp_touch_i2c_controller_context);
        if (CY_SCB_I2C_SUCCESS != i2c_result)
        {
            handle_app_error();
        }
        sysint_status = Cy_SysInt_Init(&disp_touch_i2c_controller_irq_cfg,
                                       &disp_touch_i2c_controller_interrupt);
        if (CY_SYSINT_SUCCESS != sysint_status)
        {
            handle_app_error();
        }
        NVIC_EnableIRQ(disp_touch_i2c_controller_irq_cfg.intrSrc);
        Cy_SCB_I2C_Enable(DISPLAY_I2C_CONTROLLER_HW);

        i2c_result = mtb_disp_waveshare_4p3_init(DISPLAY_I2C_CONTROLLER_HW,
                                             &disp_touch_i2c_controller_context);
        if (CY_SCB_I2C_SUCCESS != i2c_result)
        {
            handle_app_error();
        }
#endif

        /* ---- VGLite init ---- */
        vg_module_parameters_t vg_params;
        vg_params.register_mem_base = (uint32_t)GFXSS_GFXSS_GPU_GCNANO;
        vg_params.gpu_mem_base[VG_PARAMS_POS] = GPU_MEM_BASE;
        vg_params.contiguous_mem_base[VG_PARAMS_POS] = vglite_heap_base;
        vg_params.contiguous_mem_size[VG_PARAMS_POS] = VGLITE_HEAP_SIZE;

        vg_lite_init_mem(&vg_params);

        vglite_status = vg_lite_init((MY_DISP_HOR_RES), (MY_DISP_VER_RES));

        if (VG_LITE_SUCCESS == vglite_status)
        {
            /* ---- LVGL init ---- */
            lv_init();
            lv_port_disp_init();
            lv_port_indev_init();

            /* ---- Application hook: retarget-lvgl + user UI ---- */
            retarget_lvgl_init(lv_screen_active());

            /* Print boot info to terminal display */
            printf("=== PSOC Edge LVGL Terminal ===\n");
            printf("System booted successfully\n");
            printf("FreeRTOS version: %s\n", tskKERNEL_VERSION_NUMBER);
            printf("Display: 800x480 @ 60Hz\n");
            printf("Font: UNSCII-16 monospace\n");
            printf("Ring buffer: 4096 bytes\n");
            printf("\n--- Terminal Ready ---\n");
        }
        else
        {
            vg_lite_close();
            handle_app_error();
        }
    }
    else
    {
        handle_app_error();
    }

    /* ---- LVGL timer loop ---- */
    for (;;)
    {
        time_till_next = lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(time_till_next));
    }
}
