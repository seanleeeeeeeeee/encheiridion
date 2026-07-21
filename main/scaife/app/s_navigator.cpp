#include "s_navigator.h"
#include "page_state.h"
#include "scaife/scaife_client.h"
#include "scaife/xml_parser.h"
#include "ui/column.h"
#include "fonts/font_manager.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>


static const char *TAG = "s_navigator";

#define XML_BUF   (32 * 1024)
#define JSON_BUF  (24 * 1024)   // JSON is larger than old XML label response
#define TEXT_BUF  (16 * 1024)
#define LABEL_BUF  64
static TextLine *g_lines = NULL;

static int locate_author(TextLine *lines)
{
 	char path[32];
 	char search[32];
	strncpy(search, lines[0].text, 31);
	snprintf(path, 32, "/sdcard/scaife/catalog-%c.csv", *(search)|0x20);
	FILE *f = fopen(path, "r");
	if (f == NULL) return 0;
	ESP_LOGI(TAG, "opened %s", path);
	char line[256]; int n=0;
	while (fgets(line, sizeof(line), f)){
		if (strcasestr(line, search)){
			snprintf(lines[n].text, 256, "%s", line);
			ESP_LOGI(TAG, "%s", line);
			n++;
		}
	}
	fclose(f);
	lines[n].text[0] = '\0';
	return n;
}
static int locate_texts(TextLine *lines, int loc)
{
	int rawlen = strlen(lines[0].text); // "/library/urn[...]/json/"
 	char author_ref[128];
 	snprintf(author_ref, sizeof(author_ref), "%.*s", rawlen-9-6, lines[0].text+9);ESP_LOGI(TAG, "%s", author_ref);
	FILE *f = fopen("/sdcard/scaife/scaife.perseus.org.json", "r");
	if (f == NULL) return 0;
	char *json_buf = (char *)heap_caps_malloc(JSON_BUF, MALLOC_CAP_SPIRAM);
    if (!json_buf) return false;
	fseek(f, loc-200, SEEK_SET);
    char temp[128];
    char *tag1 = "\"urn\": \"";
    char *tag2 = "\"label\": \"";
    char *t = author_ref;
    char *p = nullptr;
    int bar =0;
    memcpy(json_buf, "          ", 10);
    while (fgets(json_buf + 10, JSON_BUF - 10, f)) {
    	p = json_buf;     ESP_LOGI(TAG, "%.20s", p);
    	while (p && (p = strstr(p, t)) != NULL) {
    		p += strlen(t);
			const char *q = p;
			while (*q && !(*q == '"' && *(q-1) != '\\')) q++;
			if (!*q) {
				int rem = (int)(json_buf+10 + strlen(json_buf+10) - (p - strlen(t)));
				memmove(json_buf + 10, p - strlen(t), rem);
				json_buf[10 + rem] = '\0';
				if (fgets(json_buf + 10 + rem, JSON_BUF - 10 - rem, f) == NULL) {
					free(json_buf);
					fclose(f);
					return 0;
				}
				p = json_buf;
				continue;
			}
			int len = (int)(q - p);
			if (t == tag1) {
				snprintf(temp, 128, "%.*s", len, p);
				ESP_LOGI(TAG, "u=%s", temp);
				if (strstr(temp, author_ref) == NULL) {
					ESP_LOGI(TAG, "ended at %s (offset %d)", temp, loc - JSON_BUF + p - json_buf);
					free(json_buf);
					fclose(f);
					return bar;
				}
				if (strchr(temp, '.') == strrchr(temp, '.')) {
					t = tag2;
				} else {
					strncpy(lines[bar].text, temp, 255);
					ESP_LOGI(TAG, "urn=%s", temp);
					bar++;
				}
			} else if (t == tag2) {
				snprintf(temp, 128, "%.*s", len, p);
				strncpy(lines[bar].text, temp, 255);
				ESP_LOGI(TAG, "label=%s", temp);
				t = tag1;
				bar++;
			} else {
				t = tag1;
			}
		}
		if (p == NULL) {
			memcpy(json_buf, json_buf + JSON_BUF - 11, 10);
		}
    }
    free(json_buf);
	ESP_LOGI(TAG, "end of file");
    fclose(f);
    return bar;
}

static int scaife_search(PageState *s) {
return 0;}
static bool scaife_load(PageState *s, PassageData *pd)
{	
	if (*(s->urn_ref) == '*')
	{
		g_lines = (TextLine *)(s->page);
		s->page = locate_author(g_lines);
		return s->page;
	}
	if (*(s->urn_ref) == '?')
	{
		int loc = s->page;
		s->page = locate_texts(g_lines, loc);
		return s->page;
	}
    //memset(pd, 0, sizeof(*pd));
    char urn_grc[128], urn_eng[128];
    int urn_len = page_state_build_urn(s, true,  urn_grc, sizeof(urn_grc));
    page_state_build_urn(s, false, urn_eng, sizeof(urn_eng));
    char *xml_buf   = (char *)heap_caps_malloc(XML_BUF,   MALLOC_CAP_SPIRAM);
    char *json_buf  = (char *)heap_caps_malloc(JSON_BUF,  MALLOC_CAP_SPIRAM);
    char *text_grc  = (char *)heap_caps_malloc(TEXT_BUF,  MALLOC_CAP_SPIRAM);
    char *text_eng  = (char *)heap_caps_malloc(TEXT_BUF,  MALLOC_CAP_SPIRAM);
    char *label_buf = (char *)heap_caps_malloc(LABEL_BUF, MALLOC_CAP_SPIRAM);
	ESP_LOGI(TAG, "33");
    if (!xml_buf || !json_buf || !text_grc || !text_eng || !label_buf) {
        ESP_LOGE(TAG, "scratch alloc failed");
        free(xml_buf); free(json_buf);
        free(text_grc); free(text_eng); free(label_buf);
        return false;
    }

    bool ok = false;
	if (urn_grc[urn_len - 1] == ':') //empty urn ref, find first_passage
	{	urn_grc[urn_len - 1] = '\0';
		if (scaife_get_passage_json(urn_grc, json_buf, 2048) < 0) {
			ESP_LOGW(TAG, "JSON fetch failed");
			label_buf = "unknown";
		} else {
			ESP_LOGI(TAG, "buf=%.20s", json_buf);
			const char *p = strstr(json_buf, "\"urn\": \"") + 8;
			ESP_LOGI(TAG, "val=%.20s", p);
			const char *q = strchr(p, '\"');
			ESP_LOGI(TAG, "q=%.20s", q);
			p=q;
			while (*p && *p != ':') p--;
			int len = q - p;
			ESP_LOGI(TAG, "p=%.20s %d", p, len);
			memcpy(s->urn_ref, p + 1, len - 1);
			ESP_LOGI(TAG, "First passage: %s", s->urn_ref);
			p = strstr(p, "\"label\": \"") + 10;	//"label": "_"
			p = strstr(p, "\"label\": \"") + 10;	//_1234567890
			q = strchr(p, '\"');
			len = q - p;
			memcpy(label_buf, p, len);
			label_buf[len] = '\0';
			ESP_LOGI(TAG, "Label: %s", label_buf);
		}
		snprintf(pd->label, 128, "%s", label_buf); label_buf = NULL;
		page_state_build_urn(s, true,  urn_grc, sizeof(urn_grc));
    	page_state_build_urn(s, false, urn_eng, sizeof(urn_eng));
	}
    // ── Greek passage XML ────────────────────────────────────────────────
    if (scaife_get_passage_xml(urn_grc, xml_buf, XML_BUF) < 0 ||
        xml_find_passage_text(xml_buf, text_grc, TEXT_BUF) < 0) {
        ESP_LOGE(TAG, "Greek fetch failed: %s", urn_grc);
        goto done;
    }
    ESP_LOGI(TAG, "Greek %d chars: %.60s", (int)strlen(text_grc), text_grc);

    // ── English passage XML ──────────────────────────────────────────────
    if (scaife_get_passage_xml(urn_eng, xml_buf, XML_BUF) < 0 ||
        xml_find_passage_text(xml_buf, text_eng, TEXT_BUF) < 0) {
        ESP_LOGE(TAG, "English fetch failed: %s", urn_eng);
        text_eng = NULL;
        goto ld;
    }
    ESP_LOGI(TAG, "English %d chars: %.60s", (int)strlen(text_eng), text_eng);
    
    ok = true;
ld:
    pd->text_grc = text_grc;  text_grc  = NULL;
    pd->text_eng = text_eng;  text_eng  = NULL;

done:
    free(xml_buf);
    free(json_buf);
    free(text_grc);
    free(text_eng);
    free(label_buf);
    return ok;
}

static bool scaife_get_prevnext(const PageState *s, char *out_prev, int prev_size, char *out_next, int next_size)
{
    char urn_grc[128];
    page_state_build_urn(s, true, urn_grc, sizeof(urn_grc));

    char *json_buf = (char *)heap_caps_malloc(JSON_BUF, MALLOC_CAP_SPIRAM);
    if (!json_buf) return false;

    bool ok = false;

    if (scaife_get_passage_json(urn_grc, json_buf, 0, true) >= 0) {
        json_get_prevnext_refs(out_prev, prev_size, out_next, next_size);
        ok = true;
    } else {
        ESP_LOGE(TAG, "JSON fetch for prevnext failed");
        if (out_prev) out_prev[0] = '\0';
        if (out_next) out_next[0] = '\0';
    }

    free(json_buf);
    return ok;
}
static bool scaife_go_prev_page(PageState *s)
{
    ESP_LOGI(TAG, "Prev page");
    if (s->page > 0) {
        s->page--;
        page_state_save(s);
        return 0;
    }
    char prev_ref[URN_REF_MAX] = {0};
    char dummy[URN_REF_MAX]    = {0};
    if (!scaife_get_prevnext(s,
                                 prev_ref, sizeof(prev_ref),
                                 dummy,    sizeof(dummy))) {
        ESP_LOGW(TAG, "Could not fetch prev URN"); return 0;
    }
    if (strlen(prev_ref) == 0) { ESP_LOGI(TAG, "No previous"); return 0; }
    strncpy(s->urn_ref, prev_ref, URN_REF_MAX - 1);
    s->urn_ref[URN_REF_MAX - 1] = '\0';
    return 1;
    //s->page = max_page_for_passage();
    //page_state_save(s);
    vTaskDelay(pdMS_TO_TICKS(500));
}

static bool scaife_go_next_page(PageState *s, int max_page)
{
    ESP_LOGI(TAG, "Next page");
    if (s->page < max_page) {
        s->page++;
        page_state_save(s);
        return 0;
    }
    char next_ref[URN_REF_MAX] = {0};
    char dummy[URN_REF_MAX]    = {0};
    if (!scaife_get_prevnext(s,
                                 dummy,    sizeof(dummy),
                                 next_ref, sizeof(next_ref))) {
        ESP_LOGW(TAG, "Could not fetch next URN"); return 0;
    }
    if (strlen(next_ref) == 0) { ESP_LOGI(TAG, "No next"); return 0; }
    strncpy(s->urn_ref, next_ref, URN_REF_MAX - 1);
    s->urn_ref[URN_REF_MAX - 1] = '\0';
    s->page = 0;
    page_state_save(s);
    return 1;
    //vTaskDelay(pdMS_TO_TICKS(500));
}

static const NavigatorInterface scaife_nav = {
    .name          = "scaife",
    .load          = scaife_load,
    .search		   = scaife_search,
    .build_urn     = page_state_build_urn,
    .go_prev_page  = scaife_go_prev_page,
    .go_next_page  = scaife_go_next_page,
};

const NavigatorInterface *navigator_scaife(void) {
    return &scaife_nav;
}