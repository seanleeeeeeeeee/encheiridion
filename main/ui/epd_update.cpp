#include "epd_update.h"
#include "ui/touch_bus.h"
#include "FastEPD.h"
#include "header.h"

void epd_full_update(void)
{
	flag_of_pausing = true;
	//while (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE);
    touch_bus_pre_update();
    epaper->fullUpdate(true);
    touch_bus_post_update();
    //xSemaphoreGive(s_mutex);
    flag_of_pausing = false;
}

void epd_part_update(BB_RECT *rect)
{
	flag_of_pausing = true;
    touch_bus_pre_update();
    epaper->fullUpdate(true, false, rect);
    touch_bus_post_update();
    //xSemaphoreGive(s_mutex);
    flag_of_pausing = false;
}