/******************************************************************************
 * File: gfx_task.h
 * Description: GFX subsystem initialization and LVGL task for CM55.
 *              Initializes GFXSS, GPU, display panel, VGLite, and LVGL.
 ******************************************************************************/

#ifndef GFX_TASK_H
#define GFX_TASK_H

#include "FreeRTOS.h"
#include "task.h"

/*******************************************************************************
 * Macros
 ******************************************************************************/
#define GFX_TASK_NAME       ("CM55 Gfx Task")
#define GFX_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 16)  /* in words */
#define GFX_TASK_PRIORITY   (configMAX_PRIORITIES - 1)

/*******************************************************************************
 * Globals
 ******************************************************************************/
extern TaskHandle_t rtos_cm55_gfx_task_handle;

/*******************************************************************************
 * Function Prototypes
 ******************************************************************************/

/**
 * @brief FreeRTOS task that initializes the graphics subsystem and runs
 *        the LVGL timer loop. Call retarget_lvgl_init() and application
 *        UI setup inside the "app hook" section of this task.
 */
void gfx_task(void *arg);

#endif /* GFX_TASK_H */
