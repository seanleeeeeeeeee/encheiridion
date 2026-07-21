#include "numpad.h"
#include "header.h"
#include "fonts/font_manager.h"
#include "ui/ui_event.h"
#include "ui/epd_update.h"
#include "FastEPD.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>


static const char *TAG = "numpad";

#define PAD_COLS  3
#define PAD_ROWS  4
#define NUM_W     120
#define NUM_H      80
#define PAD_X     ((screen_w - PAD_COLS * NUM_W) / 2)
#define PAD_Y     160
#define INPUT_Y   (PAD_Y - 60)
#define INPUT_X   PAD_X
#define INPUT_W   (PAD_COLS * NUM_W)
#define INPUT_H    50
static const char *NUM_LABELS[PAD_ROWS][PAD_COLS] = {
    { "1", "2", "3" },
    { "4", "5", "6" },
    { "7", "8", "9" },
    { ".", "0", "\xe2\x86\x92" },   // U+2192 RIGHT ARROW (confirm)
};
static const char *KB_LABELS[4][10] = {
//     0    1    2    3    4    5    6    7    8    9
    { "q", "w", "e", "r", "t", "y", "u", "i", "o", "p" },
    { "a", "s", "d", "f", "g", "h", "j", "k", "l" },
    { "^", "z", "x", "c", "v", "b", "n", "m", "<" },
    { "|", "*", "|", "",  "",  " ", ".", "|", "#"},
};

static int KB_COORDS[4][10][2] = {0};
//     0    1    2    3    4    5    6    7    8    9
//     {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
//     {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
//     { "^", "z", "x", "c", "v", "b", "n", "m", "<" },
//     { " |","*|"," |"," ", " ", " ", " |"," |","#|"},
// };

static char key_action(int row, int col, bool kb = false)
{
	if (kb)
	{	if (row == 3 && col == 0) return '*';
		if (row == 3 && col == 0) return '#';
		return KB_LABELS[row][col][0];
	}
    if (row == 3 && col == 0) return '.';
    if (row == 3 && col == 1) return '0';
    if (row == 3 && col == 2) return 'O'; // confirm
    return (char)('1' + row * 3 + col);
}

static void draw_numpad_full(const char *current_input)
{
	landscape();
    epaper->fillRect(PAD_X - 10, INPUT_Y - 10,
                    INPUT_W + 20,
                    INPUT_H + PAD_ROWS * NUM_H + 20,
                    BBEP_WHITE);

    epaper->drawRect(INPUT_X, INPUT_Y, INPUT_W, INPUT_H, BBEP_BLACK);
    epaper->setFont(g_futura20_ram, false);
    epaper->setTextColor(BBEP_BLACK);
    epaper->drawString(current_input, INPUT_X + 8, INPUT_Y + INPUT_H - 10);

    for (int r = 0; r < PAD_ROWS; r++) {
        for (int c = 0; c < PAD_COLS; c++) {
            int kx = PAD_X + c * NUM_W;
            int ky = PAD_Y + r * NUM_H;

            bool is_special = (r == 3 && (c == 0 || c == 2));
            if (is_special) {
                epaper->fillRect(kx, ky, NUM_W, NUM_H, BBEP_BLACK);
                epaper->setTextColor(BBEP_WHITE, BBEP_BLACK);
            } else {
                epaper->drawRect(kx, ky, NUM_W, NUM_H, BBEP_BLACK);
                epaper->setTextColor(BBEP_BLACK);
            }

            epaper->setFont(g_futura20_ram, false);
            BB_RECT br = {0, 0, 0, 0};
            epaper->getStringBox(NUM_LABELS[r][c], &br);
            int lx = kx + (NUM_W - br.w) / 2;
            int ly = ky + (NUM_H + br.h) / 2;
            epaper->drawString(NUM_LABELS[r][c], lx, ly);
        }
    }

    epaper->setTextColor(BBEP_BLACK);
    epd_full_update();
}

#define KB_COLS   10
#define KB_ROWS   4
#define KEY_W      52
#define KEY_H      80
#define KB_X     12
#define KB_Y     622
#define KBINPUT_Y   (KB_Y - 60)
#define KBINPUT_X   KB_X
#define KBINPUT_W   (KB_COLS * KEY_W)
#define KBINPUT_H    50
static BB_RECT srch = {KBINPUT_X, KBINPUT_Y, KBINPUT_W, KBINPUT_H};

static void draw_keyboard(const char *current_input)
{
	portrait();
    epaper->fillRect(KB_X, KBINPUT_Y - 10,
                    KBINPUT_W + 1,
                    KBINPUT_H + KB_ROWS * KEY_H + 20,
                    BBEP_WHITE);

    epaper->drawRect(KBINPUT_X, KBINPUT_Y, KBINPUT_W, KBINPUT_H, BBEP_BLACK);
    epaper->setFont(g_futura20_ram, false);
    epaper->setTextColor(BBEP_BLACK);
    epaper->drawString(current_input, KBINPUT_X + 8, KBINPUT_Y + KBINPUT_H - 10);
	epaper->setTextColor(BBEP_BLACK);
	epaper->setFont(g_futura20_ram, false);
	bool wide=0; int kx=0; int ky=0; int w=0; int off = 0;
    for (int r = 0; r < KB_ROWS; r++) {
        for (int c = 0; c < KB_COLS; c++) {
        	if (!(KB_LABELS[r][c])) continue;
        	if (r == 0) off = 0;
			if (r != 0) off = KEY_W * 0.5;
			if (*KB_LABELS[r][c] && *KB_LABELS[r][c] == '|'){
				wide = 1;
				kx = KB_X + off+ c * KEY_W;
				ky = KB_Y + r * KEY_H;
				goto coords;
			} else if (*KB_LABELS[r][c] && wide) {
				wide = 0;
				w =  KB_X + off + (c+1) * KEY_W - kx;
			} else {
				w = KEY_W;
				kx = KB_X + off + c * KEY_W;
				ky = KB_Y + r * KEY_H;
			}
			if (1){
				epaper->drawRect(kx, ky, w, KEY_H, BBEP_BLACK);
				BB_RECT br = {0, 0, 0, 0};
				epaper->getStringBox(KB_LABELS[r][c], &br);
				int lx = kx + (w - br.w) / 2;
				int ly = ky + (KEY_H + br.h) / 2;
				epaper->drawString(KB_LABELS[r][c], lx, ly);
			}
coords:		if (true) {
				KB_COORDS[r][c][0] = kx + w;
            	KB_COORDS[r][c][1] = ky + KEY_H;
            }
			ESP_LOGI(TAG, "keyboard %s   %d %d", KB_LABELS[r][c], KB_COORDS[r][c][0], KB_COORDS[r][c][1]);
        }
    }
    epaper->setTextColor(BBEP_BLACK);
    epd_full_update();
}

static void redraw_input_field(const char *current_input)
{
    epaper->fillRect(INPUT_X + 1, INPUT_Y + 1,
                    INPUT_W - 2, INPUT_H - 2,
                    BBEP_WHITE);
    epaper->setFont(g_futura20_ram, false);
    epaper->setTextColor(BBEP_BLACK);
    epaper->drawString(current_input, INPUT_X + 8, INPUT_Y + INPUT_H - 10);
    epd_full_update();
}

int numpad_run(const char *initial_ref, char *out_ref, int out_size)
{
    char buf[48];
    strncpy(buf, "", sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    int len = (int)strlen(buf);
    strncpy(out_ref, "", out_size - 1);

    draw_numpad_full(buf);

    while (true) {
        int tx, ty;
        ui_wait_touch(&tx, &ty);
        // ESP_LOGI(TAG, "numpad touch %d,%d", tx, ty);
		if ((int16_t)tx == 0 && (int16_t)ty == -10) { //io48
			return -10;
		}
		if ((int16_t)tx == -10 && (int16_t)ty == 0) { //boot
			out_ref[0] = '\0';
			return 255;
		}
        // Outside numpad grid
        if (ty > 720) {
            return 0;
        }
		
        int col = ((ty - 355) + (123.5/2))/123.5;
        int row = ((324 - tx) + (76.33/2))/76.33;
        if (screen_w < screen_h) {
        	col = (tx-PAD_X)/NUM_W - 0.5;
        	row = (ty-PAD_Y)/NUM_H - 0.5;
        }
        if (col < 0 || col >= PAD_COLS || row < 0 || row >= PAD_ROWS)
            continue;
		ESP_LOGI(TAG, "numpad touch %d,%d   %d,%d", tx, ty, col, row);
        char action = key_action(row, col);

        if (action == 'O') {
            strncpy(out_ref, buf, out_size - 1);
            out_ref[out_size - 1] = '\0';
            ESP_LOGI(TAG, "numpad confirm: %s", out_ref);
            return 1;
        }

        if (action == '.') {
            if (len > 0 && len < (int)sizeof(buf) - 1) {
            	if (buf[len - 1] == '.') {
            		buf[len - 1] = '-';
            	} else {
					buf[len++] = '.';
					buf[len]   = '\0';
				}
                redraw_input_field(buf);
            }
        } else {
            // digit
            if (len < (int)sizeof(buf) - 1) {
                buf[len++] = action;
                buf[len]   = '\0';
                redraw_input_field(buf);
            }
        }
    }
}

int keyboard(const char *initial_ref, char *out_ref, int out_size)
{
    char buf[48];
    strncpy(buf, "", sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    int len = (int)strlen(buf);

    draw_keyboard(buf);
	bool caps=0;
    while (true) {
        int tx, ty;
        ui_wait_touch(&tx, &ty);
        ESP_LOGI(TAG, "kb %d,%d", tx, ty);
        if (ty < 400) return 0;
        if (ty > 702) tx -= KEY_W * 0.5;
		if ((int16_t)tx == 0 && (int16_t)ty == -10) { //io48
			return -10;
		}
		int r=0;int c=0;
		for (r = (int)(ty > 702); KB_COORDS[r][c][1] < ty; r++);
        for (c = 0; KB_COORDS[r][c][0] < tx; c++);
        char *map = "**    .##";
        char press = (r == 3) ? *(map+c) : *KB_LABELS[r][c];
        ESP_LOGI(TAG, "kb        %d,%d %c", r, c, press);
        if (press == '#') {
            strncpy(out_ref, buf, out_size - 1);
            out_ref[out_size - 1] = '\0';
            ESP_LOGI(TAG, "enter %s", out_ref);
            return 1;
        }
		epaper->fillRect(KBINPUT_X, KBINPUT_Y, KBINPUT_W, KBINPUT_H, BBEP_WHITE);
        if (press == '^'){
        	caps = !caps;
        	epaper->drawRect(KBINPUT_X, KBINPUT_Y, KBINPUT_W, KBINPUT_H, BBEP_WHITE);
        } else {
			if (press == '<'){
				buf[--len] = '\0';
			} else if (len < (int)sizeof(buf) - 1) {
				if (caps) press = press & '_';
				buf[len++] = press;
				buf[len]   = '\0';
				caps = 0;
			}
			epaper->drawRect(KBINPUT_X, KBINPUT_Y, KBINPUT_W, KBINPUT_H, BBEP_BLACK);
		}
		epaper->drawString(buf, KBINPUT_X + 8, KBINPUT_Y + KBINPUT_H - 10);
		epd_part_update(&srch);
    }
}