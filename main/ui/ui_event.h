#pragma once

// FreeRTOS.h must come before any other FreeRTOS header
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_EVT_TOUCH,
//     UI_EVT_RFRSH,
//     UI_EVT_MENUS,
} UiEventType;

typedef struct {
    UiEventType type;
    int16_t     x;
    int16_t     y;
} UiEvent;

// Defined in main.cpp — not static, so visible to all translation units
extern QueueHandle_t g_ui_queue;

// Block until the next touch event arrives.
// Never calls gt911_read_touch directly.
void ui_wait_touch(int *out_x, int *out_y);

#ifdef __cplusplus
}
#endif