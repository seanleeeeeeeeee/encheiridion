#include "r_navigator.h"
#include "scaife/app/page_state.h"
#include "reader/webserver.h"
#include "reader/epub_parser.h"
#include "ui/column.h"

#include "dirent.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
TaskHandle_t xSdTask = NULL;
SemaphoreHandle_t xSemaphore = NULL;
static const char *TAG = "r_nav";
spine_index_t *spine = NULL;
static TextLine *g_lines = NULL;

static esp_err_t build_spine_index(const char *book_dir, spine_index_t *si)
{
    memset(si, 0, sizeof(*si));
    strncpy(si->book_dir, book_dir, sizeof(si->book_dir) - 1);

    char *path = (char *)malloc(320);
    if (!path) return ESP_ERR_NO_MEM;

    snprintf(path, 320, "%s/META-INF/container.xml", book_dir);
    char *container = NULL;
    size_t csz = 0;
    esp_err_t ret = read_file(path, &container, &csz);
    if (ret != ESP_OK) { free(path); return ret; }

    char opf_rel[128] = {0};
    xml_attr(container, "full-path", opf_rel, sizeof(opf_rel));
    free(container);

    if (strlen(opf_rel) == 0) { free(path); return ESP_FAIL; }

    /* Derive opf_dir */
    const char *slash = strrchr(opf_rel, '/');
    if (slash) {
        size_t dlen = slash - opf_rel + 1;
        if (dlen >= sizeof(si->opf_dir)) dlen = sizeof(si->opf_dir) - 1;
        memcpy(si->opf_dir, opf_rel, dlen);
    }

    /* Read OPF */
    snprintf(path, 320, "%s/%s", book_dir, opf_rel);
    char *opf = NULL;
    size_t osz = 0;
    ret = read_file(path, &opf, &osz);
    free(path);
    if (ret != ESP_OK) return ret;
	
	const char *titletag = opf;
	titletag = strstr(titletag, "<dc:title");
	titletag = strstr(titletag, ">") + 1;
	const char *end = strstr(titletag, "</dc:title");
	snprintf(si->book_title, end - titletag + 1, "%s", titletag);
	ESP_LOGI(TAG, "title: %.10s ... '%s'", titletag - 10, si->book_title);
	  
    /* Parse manifest: build temp id→href map */
    typedef struct { char id[48]; char href[128]; } mitem_t;
    mitem_t *manifest = (mitem_t *)calloc(MAX_SPINE, sizeof(mitem_t));
    size_t mcount = 0;
    const char *p = opf;
    while ((p = strstr(p, "<item")) != NULL && mcount < MAX_SPINE) {
        const char *end = strchr(p, '>');
        if (!end) break;
        size_t tlen = end - p + 1;
        if (tlen >= 512) { p = end; continue; }

        char tag[512];
        memcpy(tag, p, tlen);
        tag[tlen] = '\0';

        char mt[48] = {0};
        xml_attr(tag, "media-type", mt, sizeof(mt));
        if (strstr(mt, "xhtml") || strstr(mt, "html")) {
            xml_attr(tag, "id",   manifest[mcount].id,   48);
            xml_attr(tag, "href", manifest[mcount].href, 128);
            mcount++;
        }
        p = end + 1;
    }
    /* Parse spine: ordered idrefs */
    p = opf;
    while ((p = strstr(p, "<itemref")) != NULL && si->count < MAX_SPINE) {
        const char *end = strchr(p, '>');
        if (!end) break;
        size_t tlen = end - p + 1;
        if (tlen >= 256) { p = end; continue; }

        char tag[256];
        memcpy(tag, p, tlen);
        tag[tlen] = '\0';

        char idref[48] = {0};
        xml_attr(tag, "idref", idref, sizeof(idref));

        /* Look up href */
        for (size_t m = 0; m < mcount; m++) {
            if (strcmp(manifest[m].id, idref) == 0) {
                strncpy(si->entries[si->count].idref, idref, 47);
                strncpy(si->entries[si->count].href, manifest[m].href, 127);
                si->count++;
                break;
            }
        }
        p = end + 1;
    }
    free(manifest);
    free(opf);

    ESP_LOGI(TAG, "Spine index: %zu chapters", si->count);
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Find spine index for an idref
 * ═══════════════════════════════════════════════════════════════════════════ */

static int spine_find(const spine_index_t *si, const char *idref)
{
    for (size_t i = 0; i < si->count; i++) {
        if (strcmp(si->entries[i].idref, idref) == 0)
            return (int)i;
    }
    return -1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Load one chapter's text
 * ═══════════════════════════════════════════════════════════════════════════ */

static esp_err_t load_chapter(const spine_index_t *si,
                               int                  spine_idx,
                               section_result_t    *out)
{
    memset(out, 0, sizeof(*out));

    if (spine_idx < 0 || spine_idx >= (int)si->count)
        return ESP_ERR_NOT_FOUND;

    out->has_prev = (spine_idx > 0);
    out->has_next = (spine_idx < (int)si->count - 1);

    /* Build file path */
    char *path = (char *)malloc(400);
    if (!path) return ESP_ERR_NO_MEM;

	snprintf(path, 400, "%s/%s%s",
             si->book_dir, si->opf_dir,
             si->entries[spine_idx].href);

    /* Strip fragment */
    char *hash = strchr(path, '#');
    if (hash) *hash = '\0';

    char *html = NULL;
    size_t html_len = 0;
    esp_err_t ret = read_file(path, &html, &html_len);
    free(path);
    if (ret != ESP_OK) return ret;

    /* Parse to spans */
    span_array_t sa = {0};
    ret = html_to_spans(html, html_len, &sa);
    free(html);
    if (ret != ESP_OK) return ret;

    /* Flatten */
    size_t flat_len = 0;
    char *flat = flatten_spans(&sa, &flat_len);
    out->text      = flat;
    out->text_len  = flat_len;
    out->spans     = sa.arr;
    out->span_count = sa.count;
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public: get section at position
 * ═══════════════════════════════════════════════════════════════════════════ */

esp_err_t epub_get_section(const char          *book_dir,
                           const book_position_t *pos,
                           section_result_t      *result)
{
    if (!spine) {
		spine = (spine_index_t *)calloc(1, sizeof(spine_index_t));
	}
    esp_err_t ret = build_spine_index(book_dir, spine);
    if (ret != ESP_OK) { free(spine); return ret; }
    int idx = spine_find(spine, pos->spine_idref);
    if (idx < 0) {
        ESP_LOGE(TAG, "idref '%s' not in spine", pos->spine_idref);
        return ESP_ERR_NOT_FOUND;
    }
	ESP_LOGI(TAG, "spine index %d: '%s'", idx, pos->spine_idref);
    ret = load_chapter(spine, idx, result);
    return ret;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public: navigate to next/prev section
 * ═══════════════════════════════════════════════════════════════════════════ */

esp_err_t epub_next_section(const char      *book_dir,
                            book_position_t *pos,
                            section_result_t *result)
{
    spine_index_t *si = (spine_index_t *)calloc(1, sizeof(spine_index_t));
    if (!si) return ESP_ERR_NO_MEM;

    esp_err_t ret = build_spine_index(book_dir, si);
    if (ret != ESP_OK) { free(si); return ret; }

    int idx = spine_find(si, pos->spine_idref);
    if (idx < 0 || idx >= (int)si->count - 1) {
        free(si);
        return ESP_ERR_NOT_FOUND;  /* already at last chapter */
    }

    /* Advance position */
    idx++;
    strncpy(pos->spine_idref, si->entries[idx].idref,
            sizeof(pos->spine_idref) - 1);
    pos->char_offset = 0;

    ret = load_chapter(si, idx, result);
    free(si);
    return ret;
}

esp_err_t epub_prev_section(const char      *book_dir,
                            book_position_t *pos,
                            section_result_t *result)
{
    spine_index_t *si = (spine_index_t *)calloc(1, sizeof(spine_index_t));
    if (!si) return ESP_ERR_NO_MEM;

    esp_err_t ret = build_spine_index(book_dir, si);
    if (ret != ESP_OK) { free(si); return ret; }

    int idx = spine_find(si, pos->spine_idref);
    if (idx <= 0) {
        free(si);
        return ESP_ERR_NOT_FOUND;  /* already at first chapter */
    }

    /* Go back */
    idx--;
    strncpy(pos->spine_idref, si->entries[idx].idref,
            sizeof(pos->spine_idref) - 1);
    pos->char_offset = 0;

    ret = load_chapter(si, idx, result);
    free(si);
    return ret;
}

esp_err_t epub_get_start_position(const char      *book_dir,
                                  book_position_t *pos)
{
	if (!spine) {
		spine = (spine_index_t *)calloc(1, sizeof(spine_index_t));
	}
    esp_err_t ret = build_spine_index(book_dir, spine);
    if (ret != ESP_OK) { free(spine); return ret; }

    if (spine->count == 0) { free(spine); return ESP_FAIL; }

    strncpy(pos->spine_idref, spine->entries[0].idref,
            sizeof(pos->spine_idref) - 1);
    pos->char_offset = 0;
    return ESP_OK;
}

void section_result_free(section_result_t *r)
{
    if (!r) return;
    if (r->spans) {
        for (size_t i = 0; i < r->span_count; i++)
            free(r->spans[i].text);
        free(r->spans);
    }
    free(r->text);
    memset(r, 0, sizeof(*r));
}

static book_position_t pos;
static char book_path[128]; static char alt_path[128];
static void sd_task(void *arg)
{
    ESP_LOGI(TAG, "sdtask started");
    PassageData *ret = (PassageData *)arg;
	section_result_t section;
	if (nvs_array_get(ret->n_lines_grc, ret->n_lines_eng, 4, pos.spine_idref, 128) != ESP_OK || pos.spine_idref[0] == '\0') {
		ESP_LOGI(TAG, "No bookmark");
    	epub_get_start_position(book_path, &pos);
    	snprintf(ret->label, 128, "%s", spine->book_title);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
	epub_get_section(book_path, &pos, &section);
	vTaskDelay(pdMS_TO_TICKS(10));
	if (!ret->text_grc) ESP_LOGI(TAG,"textgrc is null");
// 	if (!ret->text_grc) ret->text_grc = (char *)heap_caps_malloc(16384, MALLOC_CAP_SPIRAM);
// 	if (!ret->text_eng) ret->text_eng = (char *)heap_caps_malloc(16384, MALLOC_CAP_SPIRAM);
	ESP_LOGI(TAG, "got chapter %s: %.60s %d %d %d", pos.spine_idref, section.text, ret->text_grc, section.text_len, section.span_count);
// 	ret->text_grc = strdup(section.text);
// 	ret->text_eng = strdup(section.text);
	strncpy(ret->text_grc, section.text, 65536);
	strncpy(ret->text_eng, section.text, 65536);
	ESP_LOGI(TAG, "returning chapter %s: %.60s %d", pos.spine_idref, ret->text_grc);
	section_result_free(&section);
	ESP_LOGI(TAG,"sd hwm=%u", uxTaskGetStackHighWaterMark(NULL));
	xSemaphoreGive(xSemaphore);
    vTaskDelete(NULL);
}
static bool r_load(PageState *s, PassageData *pd) {
    ESP_LOGI(TAG, "Loading from local source...");
	nvs_array_get(s->shelf, s->opus_index, 2, book_path, 128);
	nvs_array_get(s->shelf, s->opus_index, 3, alt_path, 128);
    ESP_LOGI(TAG, "311");
    pd->n_lines_grc = s->shelf;
    pd->n_lines_eng = s->opus_index;
    xSemaphore = xSemaphoreCreateBinary();
    ESP_LOGI(TAG, "grc==%d", pd->text_grc);
    xTaskCreatePinnedToCore(sd_task,   "sd", 16384, (void *)pd, 4, &xSdTask, 1);
    while (1) {
		if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdPASS) {
			ESP_LOGI(TAG, "Loaded...");
			if (pd->text_grc == NULL){
				ESP_LOGI(TAG, "womp");
			}
			return 1;
		}
	}
}

static int r_search(PageState *s) {
	g_lines = (TextLine *)(s->page);
	int count = 0;
    ensure_dir(EPUB_STORAGE_PATH);
    DIR *dir = opendir(EPUB_STORAGE_PATH);

    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_DIR) {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
                snprintf(g_lines[count].text, 256, "%s", entry->d_name);
                count++;
            }
        }
        closedir(dir);
    }
    s->page = count;
    return count;
}

static bool r_go_prev_page(PageState *s)
{
    ESP_LOGI(TAG, "Prev page...");
    if (s->page > 0) {
        s->page--;
        page_state_save(s);
        return 0;
    }
	if (!spine) ESP_LOGI(TAG, "No spine");
    int idx = spine_find(spine, pos.spine_idref);
    if (idx <= 0) ESP_LOGI(TAG, "Already on first page!");
    idx--;
    strncpy(s->urn_ref, spine->entries[idx].idref, sizeof(s->urn_ref) - 1);
    strncpy(pos.spine_idref, spine->entries[idx].idref, sizeof(pos.spine_idref) - 1);
    return 1;
}

static bool r_go_next_page(PageState *s, int max_page)
{
    ESP_LOGI(TAG, "Next page...");
    if (s->page < max_page) {
        s->page++;
        page_state_save(s);
        return 0;
    }
    if (!spine) ESP_LOGI(TAG, "No spine");
    int idx = spine_find(spine, pos.spine_idref);
    if (idx < 0 || idx >= (int)spine->count - 1) {
        ESP_LOGI(TAG, "Already on last page!");
    }
    idx++;
    strncpy(s->urn_ref, spine->entries[idx].idref, sizeof(s->urn_ref) - 1);
    strncpy(pos.spine_idref, spine->entries[idx].idref, sizeof(pos.spine_idref) - 1);
    s->page = 0;
	page_state_save(s);
    return 1;
}

static int r_build_urn(const PageState *s, bool greek, char *out, int out_size) {
    return 0;
}

static const NavigatorInterface r_nav = {
    .name          = "epub",
    .load          = r_load,
    .search		   = r_search,
    .build_urn     = r_build_urn,
    .go_prev_page  = r_go_prev_page,
    .go_next_page  = r_go_next_page,
};

const NavigatorInterface *navigator_r(void) {
    return &r_nav;
}