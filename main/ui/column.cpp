#include "column.h"
#include "header.h"
#include "strings/greek_encode.h"   // provides utf8_next as static inline
#include "fonts/font_manager.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "column";

static const char* latin1_to_ascii[64] = { //starts at C3 80 or U+00C0
    "A","A","A","A","A","A","AE","C",   // ÀÁÂÃÄÅÆÇ
    "E\'","E\'","E^","E","I","I","I","I",     // ÈÉÊËÌÍÎÏ
    "D","N","O","O","O","O","O","x",     // ÐÑÒÓÔÕÖ×
    "O","U\'","U\'","U^","U","Y","Th","ss",   // ØÙÚÛÜÝÞß
    "a\'","a\'","a^","a","a","a","ae","c",     // àáâãäåæç
    "e\'","e\'","e^","e","i","i","i^","i",     // èéêëìíîï
    "d","n","o","o","o^","o","o","/",     // ðñòóôõö÷
    "o","u\'","u\'","u^","u","y","th","y"      // øùúûüýþÿ
};

// ── Font selector ─────────────────────────────────────────────────────────────
const uint8_t *font_for_cp(uint32_t cp,
                            const uint8_t *latin,
                            const uint8_t *gk_basic,
                            const uint8_t *gk_ext)
{
    if (cp >= 0x1F00 && cp <= 0x1FFF) return gk_ext;
    if (cp >= 0x0370 && cp <= 0x03FF) return gk_basic;
    return latin;
}

// ── Measure ──────────────────────────────────────────────────────────────────
int measure_string(const char *utf8,
                   const uint8_t *latin,
                   const uint8_t *gk_basic,
                   const uint8_t *gk_ext)
{
    const char *p     = utf8;
    int         total = 0;

    while (*p) {
        const uint8_t *cur_font = NULL;
        char run[MAX_LINE_W];
        int  run_len = 0;

        while (*p && run_len < MAX_LINE_W - 4) {
            const char *prev = p;
            uint32_t cp = utf8_next(&p);
            if (cp == 0) break;

            const uint8_t *f = font_for_cp(cp, latin, gk_basic, gk_ext);
            if (!cur_font) cur_font = f;
            if (f != cur_font) { p = prev; break; }

            if (cp < 0x80) {
                run[run_len++] = (char)cp;
            } else if (cp < 0x800) {
                run[run_len++] = (char)(0xC0 | (cp >> 6));
                run[run_len++] = (char)(0x80 | (cp & 0x3F));
            } else {
                run[run_len++] = (char)(0xE0 | (cp >> 12));
                run[run_len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                run[run_len++] = (char)(0x80 | (cp & 0x3F));
            }
        }

        if (run_len == 0) break;
        run[run_len] = '\0';

        BB_RECT rect = {0, 0, 0, 0};
        epaper->setFont(cur_font, false);
        epaper->getStringBox(run, &rect);
        total += rect.w;
    }
    return total;
}
/*
int draw_mixed(const char *utf8, int x, int y,
               const uint8_t *latin,
               const uint8_t *gk_basic,
               const uint8_t *gk_ext)
{
    const char *p = utf8;

    while (*p) {
        const uint8_t *cur_font = NULL;
        char run[MAX_LINE_W];
        int  run_len = 0;

        while (*p && run_len < MAX_LINE_W - 4) {
            const char *prev = p;
            uint32_t cp = utf8_next(&p);
            if (cp == 0) break;

            const uint8_t *f = font_for_cp(cp, latin, gk_basic, gk_ext);
            //ESP_LOGI(TAG, "use font %u for %u", f, cp);
            if (!cur_font) cur_font = f;
            if (f != cur_font) { p = prev; break; }

            if (cp < 0x80) {
                run[run_len++] = (char)cp;
            } else if (cp < 0x800) {
                run[run_len++] = (char)(0xC0 | (cp >> 6));
                run[run_len++] = (char)(0x80 | (cp & 0x3F));
            } else {
                run[run_len++] = (char)(0xE0 | (cp >> 12));
                run[run_len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                run[run_len++] = (char)(0x80 | (cp & 0x3F));
            }
        }

        if (run_len == 0) break;
        run[run_len] = '\0';

        epaper->setFont(cur_font, true);
        BB_RECT rect = {0, 0, 0, 0};
        epaper->drawString(run, -1, -1);
        // epaper->getStringBox(run, &rect);
//         epaper->drawString(run, x, y);
        //ESP_LOGI(TAG, "drawing %s", run);
        x += rect.w;
    }
    return x;
}*/
int draw_mixed(const char *utf8, int x, int y,
               const uint8_t *latin,
               const uint8_t *gk_basic,
               const uint8_t *gk_ext)
{
    const char *p = utf8;

    while (*p) {
        const uint8_t *cur_font = NULL;
        char run[MAX_LINE_W];
        int  run_len = 0;

        while (*p && run_len < MAX_LINE_W - 4) {
            const char *prev = p;
            uint32_t cp = utf8_next(&p);
            if (cp == 0) break;

            const uint8_t *f = font_for_cp(cp, latin, gk_basic, gk_ext);
            if (!cur_font) cur_font = f;
            if (f != cur_font) { p = prev; break; }

            if (cp < 0x80) {
                run[run_len++] = (char)cp;
            } else if (cp < 0x800) {
                run[run_len++] = (char)(0xC0 | (cp >> 6));
                run[run_len++] = (char)(0x80 | (cp & 0x3F));
            } else {
                run[run_len++] = (char)(0xE0 | (cp >> 12));
                run[run_len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                run[run_len++] = (char)(0x80 | (cp & 0x3F));
            }
        }

        if (run_len == 0) break;
        run[run_len] = '\0';

        epaper->setFont(cur_font, true);
        epaper->drawString(run, x, y);


        x = epaper->_state.iCursorX;  // grab cursor directly
        y = epaper->_state.iCursorY;
    }
    return x;
}// ── Word wrap ────────────────────────────────────────────────────────────────
int wrap_text(const char *utf8, int max_pixel_width,
              TextLine *lines, int max_lines,
              const uint8_t *latin,
              const uint8_t *gk_basic,
              const uint8_t *gk_ext)
{
    int         line_count = 0;
	const char *p = utf8;
    char *line_buf = (char *)heap_caps_malloc(MAX_LINE_W, MALLOC_CAP_INTERNAL);
    char *word_buf = (char *)heap_caps_malloc(MAX_LINE_W, MALLOC_CAP_INTERNAL);
    if (!line_buf || !word_buf) {
        free(line_buf); free(word_buf);
        return 0;
    }

    while (*p && line_count < max_lines) {
        memset(line_buf, 0, MAX_LINE_W);
        int line_px  = 0;
        int line_len = 0;

        while (*p) {
            const char *q = p;
            while (*q == ' ' || *q == '\n') {
            	q++;
            }
            while (*q && *q != ' ' && *q != '\n') {
            	q++;
            }
			
            int wlen = (int)(q - p);
            if (wlen <= 0) { p = q; break; }
            if (wlen >= MAX_LINE_W) wlen = MAX_LINE_W - 1;

            memcpy(word_buf, p, wlen);
            word_buf[wlen] = '\0';
			{
				char *s = word_buf;
				while ((s = strstr(s, "\xe2\x80")) != NULL) {
					if (s[2] == '\x94'){
						s[0] = ' ';
						s[1] = '-';
						s[2] = ' ';
						s += 3;
					} else
					if (s[2] == '\x99') {
						s[0] = 0x01;
						s[1] = '\'';
						s[2] = 0x01;
						s += 3;
					}
				}
				wlen = strlen(word_buf);
			}
			{
				char *s = word_buf;
				while ((s = strstr(s, "\xc3")) != NULL) {
					memcpy(s, latin1_to_ascii[s[1] - 0x80], 2);
					if (s[1] == '\0') s[1] = 0x01;
					s += 2;
				}
			}
            int wpx = measure_string(word_buf, latin, gk_basic, gk_ext);

            if (line_len > 0 && line_px + wpx > max_pixel_width) break;

            if (line_len + wlen < MAX_LINE_W - 1) {
                memcpy(line_buf + line_len, word_buf, wlen);
                line_len += wlen;
                line_buf[line_len] = '\0';
                line_px += wpx;
            }
            p = q;
            if (*q == '\n') {
            	//ESP_LOGI(TAG, "--%s--", line_buf);
            	break;
            }
        }

        if (line_len == 0 && *p) {
            int i = 0;
            while (*p && *p != ' ' && i < MAX_LINE_W - 1)
                line_buf[i++] = *p++;
            line_buf[i] = '\0';
            line_len = i;
        }

        if (line_len > 0) {
            strncpy(lines[line_count].text, line_buf, MAX_LINE_W - 1);
            lines[line_count].text[MAX_LINE_W - 1] = '\0';
            //ESP_LOGI(TAG, "| -%c-%c-%c-", lines[line_count].text[0], lines[line_count].text[1], lines[line_count].text[2]);
            line_count++;
        }
    }

    free(line_buf);
    free(word_buf);
    return line_count;
}

// ── Draw column ──────────────────────────────────────────────────────────────
void draw_column(TextLine *lines, int line_count,
                 int x, int y_start,
                 int max_lines_visible, int page_number,
                 const uint8_t *latin,
                 const uint8_t *gk_basic,
                 const uint8_t *gk_ext)
{
    int y     = y_start;
    int start = max_lines_visible * page_number;
    int end   = start + max_lines_visible;

    for (int i = start; i < line_count && i < end; i++) {
        epaper->setTextColor(BBEP_BLACK);
        if (lines[i].text[0] == '\n' && lines[i].text[1] == '*' && lines[i].text[2] == '-') {
        	epaper->setItalic(true);
        	continue;
        }
        if (lines[i].text[0] == '\n' && lines[i].text[1] == '-' && lines[i].text[2] == '*') {
        	epaper->setItalic(false);
        	continue;
        }
        draw_mixed(lines[i].text, x, y, latin, gk_basic, gk_ext);
        y += LINE_H;
        	vTaskDelay(pdMS_TO_TICKS(10));
    }
}