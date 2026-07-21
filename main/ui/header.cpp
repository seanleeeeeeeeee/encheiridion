#include "header.h"
#include "fonts/font_manager.h"
#include "FastEPD.h"
#include "esp_log.h"


int screen_w = 960;
int screen_h = 540;
int col_w = ((screen_w - MARGIN*2 - COL_GAP) / 2);
int text_area_h = (screen_h - TEXT_Y_START - MARGIN);

void landscape(){
	epaper->setRotation(0);
	screen_w = 960;
	screen_h = 540;
	col_w = ((screen_w - MARGIN*2 - COL_GAP) / 2);
	text_area_h = (screen_h - TEXT_Y_START - MARGIN);
	ESP_LOGI("lndscp", "%d %d %d %d", screen_w, screen_h, col_w, text_area_h);
}

void portrait(){
	epaper->setRotation(90);
	screen_w = 540;
	screen_h = 960;
	col_w = (screen_w - MARGIN - HEADER_H);
	text_area_h = (screen_h - TEXT_Y_START - MARGIN);
		ESP_LOGI("prtrt","%d %d %d %d", screen_w, screen_h, col_w, text_area_h);
}
int max_visible_lines(bool portrait) { 
	return text_area_h / LINE_H;
}

void draw_header(const char *left_label, const char *right_label)
{
    epaper->setMode(BB_MODE_4BPP);
    epaper->clearWhite();
    epaper->fillRect(0, 0, screen_w, HEADER_H, 0x8);

    // Left: work label
    epaper->setFont(g_futura20_ram, false);
    epaper->setTextColor(0xf, 0x8);
    epaper->drawString(left_label, COL_LEFT_X, 40);

    // Right: ref..page
    epaper->setFont(g_futura20_ram, false);
    epaper->setTextColor(0xf, 0x8);
    epaper->drawString(right_label, COL_RIGHT_X, 40);

    // Column divider
    epaper->drawLine(COL_RIGHT_X - COL_GAP / 2, HEADER_H,
                    COL_RIGHT_X - COL_GAP / 2, screen_h,
                    BBEP_BLACK);
    epaper->setTextColor(BBEP_BLACK);
}

bool header_hit_left(int16_t x, int16_t y)
{
	ESP_LOGE(" ", "x=%hd y=%hd %d %d", x, y, y <= 480, x >= 450);
    return (y <= 480 && x >= 450);
}

bool header_hit_right(int16_t x, int16_t y)
{
    return (y > 480 && x >= 450);
}

bool page_hit_left(int16_t x, int16_t y)
{
    return ((y <= 300 && x < 450 && y > 1 && x > 1) || (x == 0 && y == -10));
}

bool page_hit_right(int16_t x, int16_t y)
{
    return ((y >= 700 && x < 450) || (x == -10 && y == 0));
}
