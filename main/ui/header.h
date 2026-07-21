#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "FastEPD.h"

#ifdef __cplusplus
extern "C" {
#endif

// Layout constants
#define MARGIN        18
#define COL_GAP       20
#define COL_LEFT_X   MARGIN
#define HEADER_H      50
#define TEXT_Y_START  (HEADER_H + MARGIN + 4)
#define MAX_LINES   2000
#define MAX_LINE_W  256
#define LINE_H       28

extern FASTEPD* epaper;

extern int screen_w;
extern int screen_h;
extern int col_w;
extern int text_area_h;

#define COL_RIGHT_X  (MARGIN + col_w + COL_GAP)
#define TEXT_AREA_H   (screen_h - TEXT_Y_START - MARGIN)

void landscape();
void portrait();
int max_visible_lines(bool);
// Draw the top header bar.
//   left_label  : work label (left half of header, touch → opus picker)
//   right_label : "ref..page" string (right half, touch → numpad)
void draw_header(const char *left_label, const char *right_label);

// Hit-test helpers — return true if point is in that zone
bool header_hit_left (int16_t x, int16_t y);
bool header_hit_right(int16_t x, int16_t y);
bool page_hit_left   (int16_t x, int16_t y);
bool page_hit_right  (int16_t x, int16_t y);

#ifdef __cplusplus
}
#endif
