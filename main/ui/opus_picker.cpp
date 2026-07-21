#include "opus_picker.h"
#include "header.h"
#include "column.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "fonts/font_manager.h"
#include "reader/app/r_navigator.h"
#include "ui/ui_event.h"
#include "ui/epd_update.h"
#include "FastEPD.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "opus_picker";

#define PICKER_X    COL_LEFT_X
#define PICKER_Y    HEADER_H
#define PICKER_W    (screen_w - MARGIN * 2)
#define ROW_H       48
#define PICKER_ROWS OPUS_COUNT

int opus_picker_run(PageState *current)
{
	ESP_LOGI(TAG, "drawing shelf %d", current->shelf);
	char buf[64];
    // ── Draw ──────────────────────────────────────────────────────────────
    epaper->fillRect(PICKER_X, PICKER_Y,
                    PICKER_W, ROW_H * PICKER_ROWS,
                    BBEP_WHITE);
    epaper->drawRect(PICKER_X, PICKER_Y,
                    PICKER_W, ROW_H * PICKER_ROWS,
                    BBEP_BLACK);
    epaper->setFont(FONT_12x16);
	//epaper->drawString(("%d", current->shelf), COL_LEFT_X, 40);
    for (int i = 0; i < PICKER_ROWS; i++) {
        int row_y = PICKER_Y + i * ROW_H;

        if (i == current->shelf) {
            epaper->fillRect(PICKER_X, row_y, PICKER_W, ROW_H, BBEP_BLACK);
            epaper->setTextColor(BBEP_WHITE, BBEP_BLACK);
        } else {
            epaper->setTextColor(BBEP_BLACK, BBEP_WHITE);
        }
        if (nvs_array_get(current->shelf, i, 0, buf, sizeof(buf)) != ESP_OK){
        	sprintf(buf, "...");
        }

        epaper->drawString(buf, PICKER_X + 8, row_y + ROW_H - 20);

        epaper->drawLine(PICKER_X,           row_y + ROW_H,
                        PICKER_X + PICKER_W, row_y + ROW_H,
                        BBEP_BLACK);
    }

    epaper->setTextColor(BBEP_BLACK);
    epd_full_update();

    while (true) {
        int tx, ty;
        ui_wait_touch(&tx, &ty);
        ESP_LOGI(TAG, "x,y = %d,%d", tx, ty);
        if ((int16_t)tx == -10 && (int16_t)ty == 0) { //boot
        	current->shelf++; if (current->shelf >= 8) {current->shelf = current->shelf % 16;}
        	return opus_picker_run(current);
		}
		if ((int16_t)tx == 0 && (int16_t)ty == -10) { //IO48
			current->shelf--; if (current->shelf <= -1) return -7; //edit
        	return opus_picker_run(current);
		}
        // Outside picker → cancel
        // if (tx < PICKER_X || tx > PICKER_X + PICKER_W ||
//             ty < PICKER_Y || ty > PICKER_Y + ROW_H * PICKER_ROWS) {
//             return -1;
//         }
        int row = (459 - tx + (48.57/2))/48.57;
        
        if (row >= 0 && row < PICKER_ROWS) {
        	int id = current->shelf*10 + row;
        	
    		char id_ch[10]; snprintf(id_ch, sizeof(id_ch), "%d", id);
   			nvs_array_set(-1, -1, -1, id_ch);
   			
			if (nvs_array_get(current->shelf, row, 0, buf, sizeof(buf)) != ESP_OK) {
				sprintf(buf, "...");
				id *= -1;
			}
            ESP_LOGI(TAG, "Selected opus %d: %s", id, buf);
                
            return id;
        }
        else {
        	return -1;
        }
    }
}

int chapter_picker_run(int page)
{
	char buf[64];
    epaper->fillRect(PICKER_X, PICKER_Y,
                    PICKER_W, ROW_H * PICKER_ROWS,
                    BBEP_WHITE);
    epaper->drawRect(PICKER_X, PICKER_Y,
                    PICKER_W, ROW_H * PICKER_ROWS,
                    BBEP_BLACK);

    for (int i = 0; i < PICKER_ROWS && i + page * PICKER_ROWS < (int)spine->count; i++) {
        int row_y = PICKER_Y + i * ROW_H;

        epaper->setTextColor(BBEP_BLACK, BBEP_WHITE);

        epaper->setFont(g_futura16_ram, false);
		snprintf(buf, sizeof(buf), spine->entries[i + page * PICKER_ROWS].idref);

        epaper->drawString(buf, PICKER_X + 8, row_y + ROW_H - 10);

        epaper->drawLine(PICKER_X,           row_y + ROW_H,
                        PICKER_X + PICKER_W, row_y + ROW_H,
                        BBEP_BLACK);
    }

    epaper->setTextColor(BBEP_BLACK);
    epd_full_update();

    while (true) {
        int tx, ty;
        ui_wait_touch(&tx, &ty);
        ESP_LOGI(TAG, "touch %d,%d", tx, ty);
        if ((int16_t)tx == -10 && (int16_t)ty == 0) {
        	page++;
        	return chapter_picker_run(page);
		}
		if ((int16_t)tx == 0 && (int16_t)ty == -10) {
			return -10;
		}
        // Outside picker → cancel
        if (tx < PICKER_X || tx > PICKER_X + PICKER_W ||
            ty < PICKER_Y || ty > PICKER_Y + ROW_H * PICKER_ROWS) {
            return -1;
        }

        int row = (459 - tx + (48.57/2))/48.57;
        if (row >= 0 && row < PICKER_ROWS) {
            return page*8 + row;
        }
    }
}

int urn_picker(bool csv, const TextLine *lines, int count, int page)
{
	char buf[256];
	char urn_1[128];
	nvs_array_get(0, 7, 2, urn_1, 128);
	portrait();
    epaper->fillRect(PICKER_X, PICKER_Y,
                	 PICKER_W, ROW_H * PICKER_ROWS, BBEP_WHITE);
    epaper->drawRect(PICKER_X, PICKER_Y,
                	 PICKER_W, ROW_H * PICKER_ROWS, BBEP_BLACK);
	int line_i = PICKER_ROWS*page;
    for (int i = 0; i < PICKER_ROWS && line_i < count; i++) {
        int row_y = PICKER_Y + i * ROW_H;
        epaper->setTextColor(BBEP_BLACK, BBEP_WHITE);
        epaper->setFont(FONT_12x16);
        char *name;
        strncpy(buf, lines[line_i].text, 255);
        if (csv) {
        	name = strtok(buf, ",");
        } else {
        	name = strrchr(buf, ':') ? strrchr(buf, ':') : buf;
        }
        ESP_LOGI(TAG, "%d-%s", line_i, name);
        epaper->drawString(name, PICKER_X + 8, row_y + ROW_H - 20);
        epaper->drawLine(PICKER_X, row_y + ROW_H, PICKER_X + PICKER_W, row_y + ROW_H, BBEP_BLACK);
        if (strcmp(buf,urn_1) == 0){
        	epaper->drawLine(PICKER_X, row_y + ROW_H +2, PICKER_X + PICKER_W, row_y + ROW_H+2, BBEP_BLACK);
        }
        line_i++;
    }

    epaper->setTextColor(BBEP_BLACK);
    epd_full_update();

    while (true) {
        int tx, ty;
        ui_wait_touch(&tx, &ty);
        
        if ((int16_t)tx == -10 && (int16_t)ty == 0) {
        	return urn_picker(csv, lines, count, ++page);
		}
		if ((int16_t)tx == 0 && (int16_t)ty == -10) {
			return -10;
		}
        // Outside picker → cancel
        if (tx < PICKER_X || tx > PICKER_X + PICKER_W ||
            ty < PICKER_Y || ty > PICKER_Y + ROW_H * PICKER_ROWS) {
            return 0;
        }

        int row = (ty - 75) / 48;
        ESP_LOGI(TAG, "touch %d,%d=%d/%d", tx, ty, page*8 + row, count);
        if (row >= 0 && row < PICKER_ROWS) {
            return page*8 + row;
        }
    }
}