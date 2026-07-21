#include "page_state.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG      = "page_state";
static const char *NVS_NS   = "encheiridion";
static const char *NVS_OPUS = "opus_idx";
static const char *NVS_REF  = "urn_ref";
static const char *NVS_PAGE = "page";

const OpusEntry g_opus_catalogue[OPUS_COUNT] = {
    {
        "Lucian, Asinus",
        "tlg0061.tlg001",
        "urn:cts:greekLit:tlg0061.tlg001.1st1K-grc1",
        "urn:cts:greekLit:tlg0061.tlg001.perseus-eng1",
        "",
        "",
    },
    {
        "Xenophon, Memorabilia",
        "tlg0032.tlg002",
        "urn:cts:greekLit:tlg0032.tlg002.perseus-grc2",
        "urn:cts:greekLit:tlg0032.tlg002.perseus-eng2",
        "",
        "",
    },
    {
        "Lucian, Verae Historiae",
        "tlg0062.tlg012",
        "urn:cts:greekLit:tlg0062.tlg012.perseus-grc2",
        "urn:cts:greekLit:tlg0062.tlg012.perseus-eng2",
        "",
        "",
    },
    {
        "Lucian, Dearum judicium",
        "tlg0062.tlg032",
        "urn:cts:greekLit:tlg0062.tlg032.perseus-grc2",
        "urn:cts:greekLit:tlg0062.tlg032.perseus-eng2",
        "",
        "",
    },
    {
        "Herodotus, Histories",
        "tlg0016.tlg001",
        "urn:cts:greekLit:tlg0016.tlg001.perseus-grc2",
        "urn:cts:greekLit:tlg0016.tlg001.perseus-eng2",
        "",
        "",
    },
    {
        "Plutarch, Alcibiades",
        "tlg0007.tlg015",
        "urn:cts:greekLit:tlg0007.tlg015.perseus-grc2",
        "urn:cts:greekLit:tlg0007.tlg015.perseus-eng2",
        "",
        "",
    },
    {
        "Longus, Daphnis and Chloe",
        "tlg0561.tlg001",
        "urn:cts:greekLit:tlg0561.tlg001.perseus-grc2",
        "urn:cts:greekLit:tlg0561.tlg001.perseus-grc2",
        "",
        "",
    },
    {
        "Athenaeus, Deipnosophists",
        "tlg0008.tlg001",
        "urn:cts:greekLit:tlg0008.tlg001.perseus-grc4",
        "urn:cts:greekLit:tlg0008.tlg001.perseus-eng2",
        "",
        "",
    },
};


/* ─── NVS helpers for the 4×8×4 string array ─────────────────────────────── */

static void nvs_key_from_index(int page, int entry, int attr, char *key, size_t key_sz)
{
    snprintf(key, key_sz, "s_%d_%d_%d", page, entry, attr);
}

esp_err_t nvs_array_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t nvs_array_set(int page, int entry, int attr, const char *value)
{
	if (page < 0 || entry < 0 || attr < 0){
		nvs_handle_t h;
		if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
			ESP_LOGE(TAG, "nvs_open failed");
			return ESP_FAIL;
		}
		nvs_set_i32(h, "last_book", atoi(value));
		esp_err_t errr = nvs_commit(h);
		nvs_close(h);
		return errr;
	}
    if (page < 0 || page >= NVS_PAGES ||
        entry < 0 || entry >= NVS_ENTRIES ||
        attr < 0 || attr >= NVS_ATTRS) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (ret != ESP_OK) return ret;

    char key[20];
    nvs_key_from_index(page, entry, attr, key, sizeof(key));
    ret = nvs_set_str(h, key, value);
    if (ret == ESP_OK) ret = nvs_commit(h);
    ESP_LOGI(TAG, "nvsset %d %d %d<=%s", page, entry, attr, value);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_array_get(int page, int entry, int attr, char *buf, size_t buf_sz)
{
	ESP_LOGI(TAG, "nvsget %d %d %d ...", page, entry, attr);
	if (page == -1 && entry == -1 && attr == -1){
		nvs_handle_t h;
		int32_t v = 0;
		if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
			ESP_LOGE(TAG, "nvs_open failed");
			return ESP_FAIL;
		}
		if (nvs_get_i32(h, "last_book", &v) == ESP_OK) { 
    		snprintf(buf, 12, "%ld", v);
    		ESP_LOGI(TAG, "Loaded last book: %s", buf);
    	}
    	nvs_close(h);
    	return 0;
	}
    if (page < 0 || page >= NVS_PAGES ||
        entry < 0 || entry >= NVS_ENTRIES ||
        attr < 0 || attr >= NVS_ATTRS) {
        buf[0] = '\0';
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (ret != ESP_OK) {
        buf[0] = '\0';
        return ret;
    }

    char key[20];
    nvs_key_from_index(page, entry, attr, key, sizeof(key));
    size_t len = buf_sz;
    ret = nvs_get_str(h, key, buf, &len);
    if (ret != ESP_OK) buf[0] = '\0';
    ESP_LOGI(TAG, "nvsget %d %d %d=>%s", page, entry, attr, buf);
    nvs_close(h);
    return ret;
}

int page_state_init(PageState *s, int opus, bool load_last)
{ 	
	nvs_handle_t h;
	int32_t v = 0;
	strncpy(s->urn_ref, "23", URN_REF_MAX - 1);
    s->urn_ref[0] = '\0';
	s->page = 0;
	if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {ESP_LOGI(TAG, "No NVS state found"); return -1;}
    if (load_last && nvs_get_i32(h, "last_book", &v) == ESP_OK) { 
    	opus = (int)v; 
    	ESP_LOGI(TAG, "Loaded last book: %d", opus);
    }
    nvs_close(h);
	s->opus_index = abs(opus) % 10;
	s->shelf = abs(opus) / 10;
	size_t len = URN_REF_MAX;
	char buf[10];
	if (nvs_array_get(s->shelf, s->opus_index, 4, s->urn_ref, len) == ESP_ERR_NVS_NOT_FOUND) ESP_LOGI(TAG, "No bookmark1");
	if (nvs_array_get(s->shelf, s->opus_index, 5, buf, 10) == ESP_ERR_NVS_NOT_FOUND) ESP_LOGI(TAG, "No bookmark2");
	s->page = strtol(buf, NULL, 10);
	
    ESP_LOGI(TAG, "Loaded state: opus=%d %d ref=%s page=%d", s->shelf, s->opus_index, s->urn_ref, s->page);
    return 0;
}

void page_state_save(const PageState *s)
{
	char buf[10];
	snprintf(buf, 10, "%d", s->page);
	nvs_array_set(s->shelf, s->opus_index, 4, s->urn_ref);
	nvs_array_set(s->shelf, s->opus_index, 5, buf);
	snprintf(buf, 10, "%d", s->shelf*10 + s->opus_index);
    nvs_array_set(-1, -1, -1, buf); // book number
    ESP_LOGI(TAG, "Saved state: opus=%d ref=%s page=%d book=%s",
             s->opus_index, s->urn_ref, s->page, buf);
}

int page_state_build_urn(const PageState *s, bool greek, char *out, int out_size)
{
	ESP_LOGI(TAG, "Build urn %d %d", s->shelf, s->opus_index);
	char urn_grc[128]; char urn_eng[128];
	nvs_array_get(s->shelf, s->opus_index, 2, urn_grc, 128);
	nvs_array_get(s->shelf, s->opus_index, 3, urn_eng, 128);
	ESP_LOGI(TAG, "%s, %s, %s", urn_grc, urn_eng, s->urn_ref);
    const char *base   = greek ? urn_grc : urn_eng;
    if (!base) base    = urn_grc;
    return snprintf(out, out_size, "%s:%s", base, s->urn_ref);
}