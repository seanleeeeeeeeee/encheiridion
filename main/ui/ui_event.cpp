#include "ui_event.h"          // brings in extern QueueHandle_t g_ui_queue
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

void ui_wait_touch(int *out_x, int *out_y)
{
    UiEvent evt;
    while (xQueueReceive(g_ui_queue, &evt, portMAX_DELAY) != pdTRUE) {
        // portMAX_DELAY means this should never spin, but be safe
    }
    if (out_x) *out_x = evt.x;
    if (out_y) *out_y = evt.y;
}