/******************************************************************************
 * File: app_config.h
 * Description: Application-wide constants, macros, and error handler.
 ******************************************************************************/

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "cybsp.h"

/*******************************************************************************
 * Error Handler
 ******************************************************************************/
__STATIC_INLINE void handle_app_error(void)
{
    __disable_irq();
    CY_ASSERT(0);
    while (true);
}

#endif /* APP_CONFIG_H */
