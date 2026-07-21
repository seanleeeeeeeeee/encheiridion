#ifndef EPUB_PARSER_H
#define EPUB_PARSER_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "miniz.h"

/*
 * Text node types after parsing.
 * EPUB contains no images in the output – they are stripped entirely.
 */
typedef enum {
    TEXT_NORMAL  = 0,   /* regular paragraph / body text  */
    TEXT_HEADING = 1,   /* h1–h6, title, etc.             */
} text_type_t;

/*
 * A single "span" of text.  The caller owns the `text` pointer.
 */
typedef struct {
    text_type_t  type;
    char        *text;   /* null-terminated UTF-8 string   */
    size_t       len;    /* strlen(text)                   */
} text_span_t;

typedef struct {
    text_span_t *arr;
    size_t count;
    size_t cap;
} span_array_t;

/*
 * Result of parsing one page of an EPUB.
 * `spans`  – array of text_span_t, length `span_count`.
 * `flat`   – single contiguous char array, all spans concatenated with
 *            a single '\n' separator; ready to feed to a display driver.
 *            Heading spans are UPPER-CASED in the flat buffer so a renderer
 *            can distinguish them without extra metadata.
 */
typedef struct {
    text_span_t *spans;
    size_t       span_count;
    char        *flat;        /* caller must free()          */
    size_t       flat_len;
} epub_page_result_t;

/*
 * Configuration for page extraction.
 *
 * chars_per_page  – approximate number of characters per logical "page".
 *                   The parser will break at the nearest sentence / word
 *                   boundary.
 */
typedef struct {
    size_t chars_per_page;   /* e.g. 512 for a small e-ink screen */
} epub_page_cfg_t;

/* ─── Public API ─────────────────────────────────────────────────────────── */
esp_err_t read_file(const char *path, char **out, size_t *out_sz);
bool xml_attr(const char *haystack,
                     const char *attr_name,
                     char       *out,
                     size_t      out_sz);
esp_err_t html_to_spans(const char *html, size_t html_len,
                                span_array_t *sa);
char *flatten_spans(const span_array_t *sa, size_t *out_len);
/*
 * Parse a page from an EPUB file.
 *
 *  epub_path   – absolute path on VFS, e.g. "/sdcard/books/mybook.epub"
 *  page_number – 0-based logical page index
 *  cfg         – page size config
 *  result      – output; must be freed with epub_page_result_free()
 *
 * Returns ESP_OK on success.
 */
esp_err_t epub_get_page(const char      *epub_path,
                        uint32_t         page_number,
                        const epub_page_cfg_t *cfg,
                        epub_page_result_t    *result);

/* Free all memory owned by a result struct. */
void epub_page_result_free(epub_page_result_t *result);

/*
 * Returns the total number of logical pages for a given EPUB + config.
 * Useful for building a progress bar / TOC.
 */
esp_err_t epub_page_count(const char           *epub_path,
                          const epub_page_cfg_t *cfg,
                          uint32_t             *count_out);

#endif // EPUB_PARSER_H
