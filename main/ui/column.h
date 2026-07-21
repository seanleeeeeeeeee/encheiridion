#pragma once
#include <stdint.h>
#include "FastEPD.h"
#include "header.h"

// utf8_next is already a static inline in strings/greek_encode.h
// Do NOT declare it here — including both would give a linkage conflict.

#ifdef __cplusplus
extern "C" {
#endif


typedef struct {
    char text[MAX_LINE_W];
} TextLine;

// Per-codepoint font selector
const uint8_t *font_for_cp(uint32_t cp,
                            const uint8_t *latin,
                            const uint8_t *gk_basic,
                            const uint8_t *gk_ext);

// Measure UTF-8 string width in pixels (mixed fonts)
int measure_string(const char *utf8,
                   const uint8_t *latin,
                   const uint8_t *gk_basic,
                   const uint8_t *gk_ext);

// Draw UTF-8 string with per-codepoint font switching; returns final x
int draw_mixed(const char *utf8, int x, int y,
               const uint8_t *latin,
               const uint8_t *gk_basic,
               const uint8_t *gk_ext);

// Word-wrap UTF-8 text; returns number of lines written
int wrap_text(const char *utf8, int max_pixel_width,
              TextLine *lines, int max_lines,
              const uint8_t *latin,
              const uint8_t *gk_basic,
              const uint8_t *gk_ext);

// Draw one page of wrapped lines
void draw_column(TextLine *lines, int line_count,
                 int x, int y_start,
                 int max_lines_visible, int page_number,
                 const uint8_t *latin,
                 const uint8_t *gk_basic,
                 const uint8_t *gk_ext);

#ifdef __cplusplus
}
#endif