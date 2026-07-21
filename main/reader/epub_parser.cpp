#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "epub_parser.h"

static const char *TAG = "EPUB_PARSER";

/* ═══════════════════════════════════════════════════════════════════════════
 * File reader — heap allocated
 * ═══════════════════════════════════════════════════════════════════════════ */

esp_err_t read_file(const char *path, char **out, size_t *out_sz)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0 || sz > 512 * 1024) {
        ESP_LOGE(TAG, "Bad file size %ld: %s", sz, path);
        fclose(f);
        return ESP_FAIL;
    }

    char *buf = (char *)heap_caps_malloc(sz + 1,
                                          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = (char *)malloc(sz + 1);
    }
    if (!buf) {
        ESP_LOGE(TAG, "OOM reading %s (%ld bytes)", path, sz);
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    size_t rd = fread(buf, 1, sz, f);
    fclose(f);

    if ((long)rd != sz) {
        ESP_LOGE(TAG, "Short read %zu/%ld: %s", rd, sz, path);
        free(buf);
        return ESP_FAIL;
    }

    buf[sz] = '\0';
    *out    = buf;
    *out_sz = (size_t)sz;
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Minimal XML attribute extractor
 * ═══════════════════════════════════════════════════════════════════════════ */

bool xml_attr(const char *haystack,
                     const char *attr_name,
                     char       *out,
                     size_t      out_sz)
{
    char search1[64], search2[64];
    snprintf(search1, sizeof(search1), "%s=\"", attr_name);
    snprintf(search2, sizeof(search2), "%s='", attr_name);

    const char *p = strstr(haystack, search1);
    char delim = '"';
    if (!p) {
        p = strstr(haystack, search2);
        delim = '\'';
    }
    if (!p) return false;

    p += strlen(attr_name) + 2;
    const char *end = strchr(p, delim);
    if (!end) return false;

    size_t len = end - p;
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * OPF parsing — same logic, reads flat files from SD
 * ═══════════════════════════════════════════════════════════════════════════ */

#define MAX_SPINE_ITEMS   64
#define MAX_PATH_LEN     128
#define MAX_ID_LEN        48
#define MAX_MEDIATYPE_LEN 48

typedef struct {
    char id[MAX_ID_LEN];
    char href[MAX_PATH_LEN];
    char media_type[MAX_MEDIATYPE_LEN];
} opf_manifest_item_t;

typedef struct {
    opf_manifest_item_t *items;
    size_t               item_count;
    char               (*spine)[MAX_ID_LEN];
    size_t               spine_count;
    char                 opf_dir[MAX_PATH_LEN];
} opf_t;

static esp_err_t opf_alloc(opf_t *opf)
{
    memset(opf, 0, sizeof(*opf));
    opf->items = (opf_manifest_item_t *)calloc(MAX_SPINE_ITEMS,
                                                sizeof(opf_manifest_item_t));
    if (!opf->items) return ESP_ERR_NO_MEM;

    opf->spine = (char (*)[MAX_ID_LEN])calloc(MAX_SPINE_ITEMS, MAX_ID_LEN);
    if (!opf->spine) {
        free(opf->items);
        opf->items = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void opf_free(opf_t *opf)
{
    if (opf->items) { free(opf->items); opf->items = NULL; }
    if (opf->spine) { free(opf->spine); opf->spine = NULL; }
}

static const char *opf_href_for_idref(const opf_t *opf, const char *idref)
{
    for (size_t i = 0; i < opf->item_count; i++) {
        if (strcmp(opf->items[i].id, idref) == 0) {
            return opf->items[i].href;
        }
    }
    return NULL;
}

static esp_err_t parse_opf(const char *opf_content,
                            const char *opf_rel_path,
                            opf_t      *opf)
{
    /* Derive OPF directory */
    const char *slash = strrchr(opf_rel_path, '/');
    if (slash) {
        size_t dir_len = slash - opf_rel_path + 1;
        if (dir_len >= MAX_PATH_LEN) dir_len = MAX_PATH_LEN - 1;
        memcpy(opf->opf_dir, opf_rel_path, dir_len);
        opf->opf_dir[dir_len] = '\0';
    } else {
        opf->opf_dir[0] = '\0';
    }

    opf->item_count  = 0;
    opf->spine_count = 0;

    /* Parse <item> elements */
    const char *p = opf_content;
    while ((p = strstr(p, "<item")) != NULL) {
        const char *end = strchr(p, '>');
        if (!end) break;
        size_t tag_len = end - p + 1;
        if (tag_len >= 512) { p = end; continue; }

        char tag[512];
        memcpy(tag, p, tag_len);
        tag[tag_len] = '\0';

        if (opf->item_count < MAX_SPINE_ITEMS) {
            opf_manifest_item_t *it = &opf->items[opf->item_count];
            xml_attr(tag, "id",         it->id,         MAX_ID_LEN);
            xml_attr(tag, "href",       it->href,       MAX_PATH_LEN);
            xml_attr(tag, "media-type", it->media_type, MAX_MEDIATYPE_LEN);

            if (strstr(it->media_type, "xhtml") ||
                strstr(it->media_type, "html")) {
                opf->item_count++;
            }
        }
        p = end + 1;
    }

    /* Parse <itemref> spine */
    p = opf_content;
    while ((p = strstr(p, "<itemref")) != NULL) {
        const char *end = strchr(p, '>');
        if (!end) break;
        size_t tag_len = end - p + 1;
        if (tag_len >= 256) { p = end; continue; }

        char tag[256];
        memcpy(tag, p, tag_len);
        tag[tag_len] = '\0';

        if (opf->spine_count < MAX_SPINE_ITEMS) {
            xml_attr(tag, "idref",
                     opf->spine[opf->spine_count],
                     MAX_ID_LEN);
            opf->spine_count++;
        }
        p = end + 1;
    }

    ESP_LOGI(TAG, "OPF: %zu manifest items, %zu spine items",
             opf->item_count, opf->spine_count);
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * HTML → plain text (same as before)
 * ═══════════════════════════════════════════════════════════════════════════ */

static const char *BLOCK_TAGS[] = {
    "p","div","br","li","tr","blockquote","section","article","aside","nav",NULL
};
static const char *DROP_TAGS[] = {
    "script","style","img","image","svg","figure","figcaption",
    "audio","video","head","meta","link",NULL
};
static const char *HEADING_TAGS[] = {
    "h1","h2","h3","h4","h5","h6","title",NULL
};

static bool tag_matches(const char *name, const char * const *list)
{
    for (size_t i = 0; list[i]; i++) {
        if (strcasecmp(name, list[i]) == 0) return true;
    }
    return false;
}

static size_t decode_entities(char *s, size_t len)
{
    static const struct { const char *ent; char rep; } E[] = {
        {"&amp;",'&'},{"&lt;",'<'},{"&gt;",'>'},
        {"&quot;",'"'},{"&apos;",'\''},{"&nbsp;",' '},{"&#160;",' '},{NULL,0}
    };
    char *r = s, *w = s, *end = s + len;
    while (r < end) {
        if (*r == '&') {
            bool replaced = false;
            for (size_t i = 0; E[i].ent; i++) {
                size_t el = strlen(E[i].ent);
                if ((size_t)(end - r) >= el && memcmp(r, E[i].ent, el) == 0) {
                    *w++ = E[i].rep; r += el; replaced = true; break;
                }
            }
            if (!replaced) {
                char *sc = (char *)memchr(r, ';',
                    (size_t)(end - r) < 8 ? (size_t)(end - r) : 8);
                if (sc && *(r+1) == '#') { *w++ = '?'; r = sc + 1; }
                else { *w++ = *r++; }
            }
        } else { *w++ = *r++; }
    }
    *w = '\0';
    return (size_t)(w - s);
}


static esp_err_t span_append(span_array_t *sa, text_type_t type,
                              const char *text, size_t len)
{
    if (len == 0) return ESP_OK;
    if (sa->count >= sa->cap) {
        size_t new_cap = sa->cap ? sa->cap * 2 : 16;
        text_span_t *tmp = (text_span_t *)realloc(sa->arr,
                                                    new_cap * sizeof(text_span_t));
        if (!tmp) return ESP_ERR_NO_MEM;
        sa->arr = tmp;
        sa->cap = new_cap;
    }
    char *dup = strndup(text, len);
    if (!dup) return ESP_ERR_NO_MEM;
    sa->arr[sa->count].type = type;
    sa->arr[sa->count].text = dup;
    sa->arr[sa->count].len  = len;
    sa->count++;
    return ESP_OK;
}

#define TBUF_MAX 4096

static void flush_span(span_array_t *sa, char *tbuf, size_t *tbuf_len,
                        text_type_t type)
{
    if (*tbuf_len == 0) return;
    while (*tbuf_len > 0 &&
           (tbuf[*tbuf_len-1]==' ' || tbuf[*tbuf_len-1]=='\n'))
        (*tbuf_len)--;
    if (*tbuf_len > 0) {
        tbuf[*tbuf_len] = '\0';
        span_append(sa, type, tbuf, *tbuf_len);
    }
    *tbuf_len = 0;
}

esp_err_t html_to_spans(const char *html, size_t html_len,
                                span_array_t *sa)
{
    char tbuf[TBUF_MAX];
    size_t tbuf_len = 0;
    text_type_t cur_type = TEXT_NORMAL;
    bool in_drop = false;
    int drop_depth = 0;
    bool last_was_ws = true;

    const char *p = html;
    const char *end = html + html_len;

    while (p < end) {
        if (*p == '<') {
            p++;
            bool is_close = (*p == '/');
            if (is_close) p++;

            char tag_name[64] = {0};
            size_t tn = 0;
            while (p < end && tn < sizeof(tag_name)-1 &&
                   (isalnum((unsigned char)*p) || *p == ':')) {
                tag_name[tn++] = tolower((unsigned char)*p++);
            }
            tag_name[tn] = '\0';
            while (p < end && *p != '>') p++;
            if (p < end) p++;

            if (strlen(tag_name) == 0) continue;

            if (tag_matches(tag_name, DROP_TAGS)) {
                if (!is_close) {
                    flush_span(sa, tbuf, &tbuf_len, cur_type);
                    in_drop = true; drop_depth = 1;
                }
                continue;
            }
            if (in_drop) {
                if (!is_close && tag_matches(tag_name, DROP_TAGS)) drop_depth++;
                else if (is_close) drop_depth--;
                if (drop_depth <= 0) in_drop = false;
                continue;
            }
            if (tag_matches(tag_name, HEADING_TAGS)) {
                if (!is_close) {
                    flush_span(sa, tbuf, &tbuf_len, cur_type);
                    cur_type = TEXT_HEADING;
                } else {
                    flush_span(sa, tbuf, &tbuf_len, TEXT_HEADING);
                    cur_type = TEXT_NORMAL;
                    if (tbuf_len < TBUF_MAX-1) tbuf[tbuf_len++] = '\n';
                }
                continue;
            }
            if (tag_matches(tag_name, BLOCK_TAGS)) {
                if (tbuf_len > 0 && tbuf[tbuf_len-1] != '\n') {
                    if (tbuf_len < TBUF_MAX-1) tbuf[tbuf_len++] = '\n';
                }
                last_was_ws = true;
            }
            continue;
        }

        if (in_drop) { p++; continue; }

        unsigned char c = (unsigned char)*p++;
        if (c == '\r' || c == '\t' || c == '\n') c = ' ';
        if (c == ' ') {
            if (!last_was_ws && tbuf_len < TBUF_MAX-1) tbuf[tbuf_len++] = ' ';
            last_was_ws = true;
            continue;
        }
        last_was_ws = false;
        if (tbuf_len < TBUF_MAX-1) {
            tbuf[tbuf_len++] = (char)c;
        } else {
            flush_span(sa, tbuf, &tbuf_len, cur_type);
        }
    }

    flush_span(sa, tbuf, &tbuf_len, cur_type);

    for (size_t i = 0; i < sa->count; i++) {
        text_span_t *sp = &sa->arr[i];
        sp->len = decode_entities(sp->text, sp->len);
    }
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Flatten + paginate
 * ═══════════════════════════════════════════════════════════════════════════ */

char *flatten_spans(const span_array_t *sa, size_t *out_len)
{
    size_t total = 0;
    for (size_t i = 0; i < sa->count; i++)
        total += sa->arr[i].len + 1;
    total++;
	ESP_LOGI(TAG, "spans=%d", total);
    char *buf = (char *)heap_caps_malloc(total+256, MALLOC_CAP_SPIRAM);
    if (!buf) return NULL;

    size_t pos = 0;
    for (size_t i = 0; i < sa->count; i++) {
        const text_span_t *sp = &sa->arr[i];
        if (sp->type == TEXT_HEADING) {
        	buf[pos++] = '\n';
        	buf[pos++] = '*';
        	buf[pos++] = '-';
        	buf[pos++] = '\n';
            for (size_t j = 0; j < sp->len; j++)
                buf[pos++] = (char)toupper((unsigned char)sp->text[j]);
            buf[pos++] = '\n';
        	buf[pos++] = '-';
        	buf[pos++] = '*';
        	buf[pos++] = '\n';
        } else {
            memcpy(buf + pos, sp->text, sp->len);
            pos += sp->len;
        }
        if (pos > 0 && buf[pos-1] != '\n') buf[pos++] = '\n';
        ESP_LOGI(TAG, "span %d: %.20s '%c'", i, buf, buf[pos-1]);
    }
    buf[pos] = '\0';
    *out_len = pos;
    return buf;
}

static char *extract_page(const char *flat, size_t flat_len,
                           size_t *offset, size_t page_chars)
{
    if (*offset >= flat_len) return NULL;
    size_t start = *offset;
    size_t avail = flat_len - start;
    size_t take  = avail < page_chars ? avail : page_chars;

    if (start + take < flat_len) {
        size_t snap = start + take;
        while (snap > start && flat[snap] != ' ' && flat[snap] != '\n') snap--;
        if (snap > start) take = snap - start;
    }

    char *page = (char *)malloc(take + 1);
    if (!page) return NULL;
    memcpy(page, flat + start, take);
    page[take] = '\0';

    *offset = start + take;
    while (*offset < flat_len &&
           (flat[*offset] == ' ' || flat[*offset] == '\n'))
        (*offset)++;

    return page;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main extraction — reads flat files from SD
 * ═══════════════════════════════════════════════════════════════════════════ */

static esp_err_t epub_extract_all_text(const char   *book_dir,
                                       span_array_t *sa,
                                       char        **flat_out,
                                       size_t       *flat_len_out)
{
    ESP_LOGI(TAG, "Parsing book: %s", book_dir);
    ESP_LOGI(TAG, "Free heap: %lu", (unsigned long)esp_get_free_heap_size());

    /* Read container.xml */
    char *path = (char *)malloc(320);
    if (!path) return ESP_ERR_NO_MEM;

    snprintf(path, 320, "%s/META-INF/container.xml", book_dir);

    char *container = NULL;
    size_t container_sz = 0;
    esp_err_t ret = read_file(path, &container, &container_sz);
    if (ret != ESP_OK) { free(path); return ret; }

    /* Find OPF relative path */
    char opf_rel[MAX_PATH_LEN] = {0};
    if (!xml_attr(container, "full-path", opf_rel, sizeof(opf_rel))) {
        /* Fallback: search for .opf */
        const char *opf_ptr = strcasestr(container, ".opf");
        if (opf_ptr) {
            const char *start = opf_ptr;
            while (start > container && *start != '"' && *start != '\'') start--;
            start++;
            size_t len = opf_ptr + 4 - start;
            if (len < sizeof(opf_rel)) {
                memcpy(opf_rel, start, len);
                opf_rel[len] = '\0';
            }
        }
    }
    free(container);

    if (strlen(opf_rel) == 0) {
        ESP_LOGE(TAG, "No OPF path found");
        free(path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OPF: %s", opf_rel);

    /* Read OPF */
    snprintf(path, 320, "%s/%s", book_dir, opf_rel);

    char *opf_content = NULL;
    size_t opf_sz = 0;
    ret = read_file(path, &opf_content, &opf_sz);
    if (ret != ESP_OK) { free(path); return ret; }

    /* Parse OPF */
    opf_t *opf = (opf_t *)calloc(1, sizeof(opf_t));
    if (!opf) { free(opf_content); free(path); return ESP_ERR_NO_MEM; }

    ret = opf_alloc(opf);
    if (ret != ESP_OK) { free(opf); free(opf_content); free(path); return ret; }

    ret = parse_opf(opf_content, opf_rel, opf);
    free(opf_content);
    if (ret != ESP_OK) { opf_free(opf); free(opf); free(path); return ret; }

    /* Process spine chapters */
    for (size_t s = 0; s < opf->spine_count; s++) {
        const char *idref = opf->spine[s];
        const char *href  = opf_href_for_idref(opf, idref);
        if (!href) {
            ESP_LOGW(TAG, "No manifest item for idref: %s", idref);
            continue;
        }

        snprintf(path, 320, "%s/%s%s", book_dir, opf->opf_dir, href);

        /* Strip fragment */
        char *hash = strchr(path, '#');
        if (hash) *hash = '\0';

        ESP_LOGI(TAG, "Chapter %zu: %s  heap=%lu",
                 s, path, (unsigned long)esp_get_free_heap_size());

        char *html = NULL;
        size_t html_len = 0;
        ret = read_file(path, &html, &html_len);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Skipping: %s", path);
            continue;
        }

        ret = html_to_spans(html, html_len, sa);
        free(html);
        if (ret != ESP_OK) {
            opf_free(opf); free(opf); free(path);
            return ret;
        }
    }

    opf_free(opf);
    free(opf);
    free(path);

    *flat_out = flatten_spans(sa, flat_len_out);
    if (!*flat_out) return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "Total text: %zu bytes, %zu spans, heap=%lu",
             *flat_len_out, sa->count,
             (unsigned long)esp_get_free_heap_size());
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════════════════════════════════════════ */

esp_err_t epub_get_page(const char            *book_dir,
                        uint32_t               page_number,
                        const epub_page_cfg_t *cfg,
                        epub_page_result_t    *result)
{
    memset(result, 0, sizeof(*result));

    span_array_t sa = {0};
    char  *flat     = NULL;
    size_t flat_len = 0;

    esp_err_t ret = epub_extract_all_text(book_dir, &sa, &flat, &flat_len);
    if (ret != ESP_OK) {
        if (sa.arr) {
            for (size_t i = 0; i < sa.count; i++) free(sa.arr[i].text);
            free(sa.arr);
        }
        return ret;
    }

    /* Paginate */
    size_t   offset    = 0;
    uint32_t cur_page  = 0;
    char    *page_text = NULL;

    while (offset < flat_len) {
        char *pg = extract_page(flat, flat_len, &offset, cfg->chars_per_page);
        if (!pg) break;
        if (cur_page == page_number) { page_text = pg; break; }
        free(pg);
        cur_page++;
    }

    free(flat);

    if (!page_text) {
        for (size_t i = 0; i < sa.count; i++) free(sa.arr[i].text);
        free(sa.arr);
        return ESP_ERR_NOT_FOUND;
    }

    result->flat     = page_text;
    result->flat_len = strlen(page_text);

    /* Rebuild spans for this page */
    span_array_t page_sa = {0};
    size_t start = 0;
    for (size_t i = 0; i <= result->flat_len; i++) {
        bool is_end = (i == result->flat_len);
        bool is_nl  = (!is_end && page_text[i] == '\n');
        if (is_end || is_nl) {
            if (i > start) {
                bool all_upper = true;
                for (size_t k = start; k < i; k++) {
                    char c = page_text[k];
                    if (c == ' ') continue;
                    if (!isupper((unsigned char)c)) { all_upper = false; break; }
                }
                text_type_t t = (all_upper && (i - start) > 1)
                                ? TEXT_HEADING : TEXT_NORMAL;
                span_append(&page_sa, t, page_text + start, i - start);
            }
            start = i + 1;
        }
    }

    result->spans      = page_sa.arr;
    result->span_count = page_sa.count;

    for (size_t k = 0; k < sa.count; k++) free(sa.arr[k].text);
    free(sa.arr);

    return ESP_OK;
}

esp_err_t epub_page_count(const char            *book_dir,
                          const epub_page_cfg_t *cfg,
                          uint32_t              *count_out)
{
    span_array_t sa = {0};
    char  *flat     = NULL;
    size_t flat_len = 0;

    esp_err_t ret = epub_extract_all_text(book_dir, &sa, &flat, &flat_len);
    if (ret != ESP_OK) {
        if (sa.arr) {
            for (size_t i = 0; i < sa.count; i++) free(sa.arr[i].text);
            free(sa.arr);
        }
        return ret;
    }

    uint32_t count  = 0;
    size_t   offset = 0;
    while (offset < flat_len) {
        char *pg = extract_page(flat, flat_len, &offset, cfg->chars_per_page);
        if (!pg) break;
        free(pg);
        count++;
    }

    free(flat);
    for (size_t i = 0; i < sa.count; i++) free(sa.arr[i].text);
    free(sa.arr);

    *count_out = count;
    return ESP_OK;
}

void epub_page_result_free(epub_page_result_t *result)
{
    if (!result) return;
    if (result->spans) {
        for (size_t i = 0; i < result->span_count; i++)
            free(result->spans[i].text);
        free(result->spans);
    }
    free(result->flat);
    memset(result, 0, sizeof(*result));
}