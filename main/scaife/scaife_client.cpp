#include "scaife_client.h"
#include "xml_parser.h"          // for json_store_link_header
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdio.h>
#include "esp_task_wdt.h"

static const char *TAG = "scaife";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_MAX_RETRY     10

static EventGroupHandle_t s_wifi_eg   = NULL;
static int                s_retry_cnt = 0;
esp_event_handler_instance_t inst_any = NULL;
esp_event_handler_instance_t inst_got_ip = NULL;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_cnt < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_cnt++;
        } else {
            xEventGroupSetBits(s_wifi_eg, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_retry_cnt = 0;
        xEventGroupSetBits(s_wifi_eg, WIFI_CONNECTED_BIT);
    }
}

int scaife_wifi_init(const char *ssid, const char *pass)
{
    s_wifi_eg = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, &inst_any));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, &inst_got_ip));

    wifi_config_t wifi_cfg = {};
    strncpy((char *)wifi_cfg.sta.ssid,     ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, pass, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_eg,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
        pdMS_TO_TICKS(30000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected");
        return 0;
    }
    ESP_LOGE(TAG, "WiFi failed");
    return -1;
}

typedef struct {
    char *buf;
    int   buf_size;
    int   written;
    bool  overflow;
    bool  capture_link;
    bool  link_stored;
} fetch_ctx_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    fetch_ctx_t *ctx = (fetch_ctx_t *)evt->user_data;
    if (!ctx) return ESP_OK;

    if (evt->event_id == HTTP_EVENT_ON_HEADER &&
        ctx->capture_link &&
        strcasecmp(evt->header_key, "link") == 0) {
        json_store_link_header(evt->header_value);
        ctx->link_stored = true;
    }
    return ESP_OK;
}

#define MAX_RETRIES 5

static int scaife_fetch(const char *url, char *buf, int buf_size,
                        bool capture_link)
{
	esp_log_level_set("HTTP_CLIENT",     ESP_LOG_VERBOSE);
esp_log_level_set("esp-tls",         ESP_LOG_VERBOSE);
esp_log_level_set("esp-tls-mbedtls", ESP_LOG_VERBOSE);
esp_log_level_set("transport_base",  ESP_LOG_VERBOSE);
esp_log_level_set("HTTP_STREAM",     ESP_LOG_VERBOSE);
    ESP_LOGI(TAG, "http : %d bytes, link=%d", buf_size, capture_link);
    fetch_ctx_t ctx = { buf, buf_size, 0, false, capture_link, false };
    buf[0] = '\0';

    esp_http_client_config_t cfg = {};
    cfg.url               = url;
    cfg.event_handler     = http_event_handler;
    cfg.user_data         = &ctx;
    cfg.transport_type    = HTTP_TRANSPORT_OVER_SSL;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms        = 15000;
    cfg.buffer_size       = 4096;
    cfg.user_agent        = "Mozilla/5.0 (compatible; scaife-esp32/1.0)";

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        ctx.written     = 0;
        ctx.overflow    = false;
        ctx.link_stored = false;
        buf[0]          = '\0';

        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) { ESP_LOGE(TAG, "http_client_init failed"); return -1; }

        esp_http_client_set_header(client, "Accept", "application/json");
        esp_http_client_set_header(client, "Accept-Encoding", "identity");
        esp_http_client_set_header(client, "Connection", "close");

        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "open failed: %d", err);
            esp_http_client_cleanup(client);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        esp_http_client_fetch_headers(client);     /* fires ON_HEADER events */
        int status = esp_http_client_get_status_code(client);

        /* If we only wanted the Link header, stop right after headers. */
        if (capture_link && ctx.link_stored) {
            ESP_LOGI(TAG, "attempt %d: status=%d (link captured, body skipped)",
                     attempt, status);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            if (status == 200) return 0;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        /* Stream the body into buf; stop cleanly on overflow. */
        char tmp[512];
        while (!ctx.overflow) {
            int remaining = ctx.buf_size - ctx.written - 1;
            if (remaining <= 0) { ctx.overflow = true; break; }
            int chunk = remaining < (int)sizeof(tmp) ? remaining
                                                    : (int)sizeof(tmp);
            int r = esp_http_client_read(client, tmp, chunk);
            if (r <= 0) break;                     /* 0 = EOF, <0 = error */
            memcpy(ctx.buf + ctx.written, tmp, r);
            ctx.written += r;
            ctx.buf[ctx.written] = '\0';
        }

        ESP_LOGI(TAG, "attempt %d: err=%d status=%d bytes=%d ovf=%d",
                 attempt, err, status, ctx.written, ctx.overflow);

        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        if (status == 200 && ctx.written > 0) {
            if (ctx.overflow)
                ESP_LOGW(TAG, "response truncated (buf=%d)", buf_size);
            return ctx.written;
        }
        vTaskDelay(pdMS_TO_TICKS(2000*attempt));
    }
    return -1;
}
typedef struct {
    FILE       *fp;             // open file handle on SD card
    size_t      total_written;  // running total of bytes written
    bool        write_error;    // flag if any fwrite failed
    char       *link_buf;       // optional: buffer to capture Link header
    bool        capture_link;
} stream_to_sd_ctx_t;

static esp_err_t http_event_handler_sd(esp_http_client_event_t *evt)
{
    stream_to_sd_ctx_t *ctx = (stream_to_sd_ctx_t *)evt->user_data;

    switch (evt->event_id) {

    case HTTP_EVENT_ON_DATA:
        if (ctx->write_error) {
            break;  // stop trying if a previous write already failed
        }
        if (evt->data_len > 0 && ctx->fp != NULL) {
            size_t written = fwrite(evt->data, 1, evt->data_len, ctx->fp);
            if (written != (size_t)evt->data_len) {
                ESP_LOGE(TAG, "fwrite failed: wanted %d, wrote %zu",
                         evt->data_len, written);
                ctx->write_error = true;
            } else {
                ctx->total_written += written;
            }
        }
        break;

    case HTTP_EVENT_ON_HEADER:
        /* Optionally capture the "Link" header for pagination */
        if (ctx->capture_link && ctx->link_buf &&
            strcasecmp(evt->header_key, "Link") == 0) {
            strncpy(ctx->link_buf, evt->header_value, 511);
            ctx->link_buf[511] = '\0';
        }
        break;

    case HTTP_EVENT_ERROR:
        ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
        break;

    default:
        break;
    }
    return ESP_OK;
}

int scaife_fetch_to_sd(const char *url, const char *file_path, char *link_buf, bool capture_link)
{
    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        FILE *fp = fopen(file_path, "w");
        if (!fp) {
            ESP_LOGE(TAG, "Failed to open %s for writing", file_path);
            return -1;
        }
        stream_to_sd_ctx_t ctx = {
            .fp            = fp,
            .total_written = 0,
            .write_error   = false,
            .link_buf      = link_buf,
            .capture_link  = capture_link,
        };

        /* ---- Configure HTTP client ---- */
        esp_http_client_config_t cfg = {};
        cfg.url               = url;
        cfg.event_handler     = http_event_handler_sd;
        cfg.user_data         = &ctx;
        cfg.transport_type    = HTTP_TRANSPORT_OVER_SSL;
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
        cfg.timeout_ms        = 15000;
        cfg.buffer_size       = 4096;   // internal receive buffer
        cfg.buffer_size_tx    = 1024;

        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) {
            ESP_LOGE(TAG, "http_client_init failed");
            fclose(fp);
            return -1;
        }

        esp_err_t err    = esp_http_client_perform(client);
        int       status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);

        if (fflush(fp) != 0) {
            ESP_LOGE(TAG, "fflush failed");
            ctx.write_error = true;
        }

        int fd = fileno(fp);
        if (fd >= 0) {
            fsync(fd);
        }

        fclose(fp);

        ESP_LOGI(TAG, "attempt %d: err=%d status=%d bytes=%zu write_err=%d",
                 attempt, err, status, ctx.total_written, ctx.write_error);

        if (err == ESP_OK && status == 200 &&
            ctx.total_written > 0 && !ctx.write_error) {
            ESP_LOGI(TAG, "Saved %zu bytes to %s", ctx.total_written, file_path);
            return (int)ctx.total_written;
        }
        remove(file_path);
        ESP_LOGW(TAG, "Attempt %d failed, retrying in 1s...", attempt);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGE(TAG, "All %d attempts failed for %s", MAX_RETRIES, url);
    return -1;
}

int scaife_get_passage_xml(const char *full_urn, char *buf, int buf_size)
{
    char url[256];
    snprintf(url, sizeof(url),
             "https://scaife.perseus.org/library/%s/cts-api-xml/",
             full_urn);
    ESP_LOGI(TAG, "GET XML: %s", url);
    return scaife_fetch(url, buf, buf_size, false);
}

int scaife_get_passage_json(const char *full_urn, char *buf, int buf_size, bool link_h_only)
{
    char url[256];
    if (link_h_only) {snprintf(url, sizeof(url), "https://scaife.perseus.org/library/passage/%s/json/", full_urn);}
    else 			 {snprintf(url, sizeof(url), "https://scaife.perseus.org/library/%s/json/", full_urn);}
    ESP_LOGI(TAG, "GET JSON: %s", url);
    return scaife_fetch(url, buf, buf_size, link_h_only);
}
#define JSON_BUF  (24 * 1024)
static int locate_texts(char *author, int rawlen, int offset)
{
 	char author_ref[128];
 	snprintf(author_ref, sizeof(author_ref), "%.*s", rawlen-9-6, author+9);ESP_LOGI(TAG, "%s", author_ref);
	FILE *f = fopen("/sdcard/scaife/scaife.perseus.org.json", "r");
	if (f == NULL) return 0;
	char *json_buf = (char *)heap_caps_malloc(JSON_BUF, MALLOC_CAP_SPIRAM);
    if (!json_buf) return false;
	fseek(f, offset, SEEK_SET);
    char temp[128];
    char *tag1 = "\"urn\": \"";
    char *tag2 = "\"label\": \"";
    char *t = author_ref;
    char *p = nullptr;
    int bar =0; int loc =0;
    memcpy(json_buf, "          ", 10);
    while (fgets(json_buf + 10, JSON_BUF - 10, f)) {
    	loc = ftell(f);
    	p = json_buf;
    	while (p && (p = strstr(p, t)) != NULL) {
    		p += strlen(t);
			const char *q = p;
			while (*q && !(*q == '"' && *(q-1) != '\\')) q++;
			if (!*q) {
				int rem = (int)(json_buf + 10 + strlen(json_buf + 10) - (p - strlen(t)));
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
				//ESP_LOGI(TAG, "u=%s", temp);
				if (strstr(temp, author_ref) == NULL) {
					//ESP_LOGI(TAG, "ended at %s (offset %d)", temp, loc - JSON_BUF + p - json_buf);
					free(json_buf);
					fclose(f);
					return loc - JSON_BUF + p - json_buf - strlen(t);
				}
				if (strchr(temp, '.') == strrchr(temp, '.')) {
					t = tag2;
				} else {
					//strncpy(lines[bar].text, temp, 255);
					bar++;
				}
			} else if (t == tag2) {
				snprintf(temp, 128, "%.*s", len, p);
				//strncpy(lines[bar].text, temp, 255);
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
	ESP_LOGI(TAG, "end of file");
    fclose(f);
    return bar;
}
int load_authors_json()
{
	const char *folder_path = "/sdcard/scaife";
    struct stat st = {0};
    int foo = stat(folder_path, &st);
    if (foo == -1) {
        if (mkdir(folder_path, 0777) == 0) {
            ESP_LOGI(TAG, "Directory created successfully");
        } else {
            ESP_LOGI(TAG, "Failed to create directory");
        }
    } else {
        if (S_ISDIR(st.st_mode)) {
            ESP_LOGI(TAG, "Folder exists: %s", folder_path);
        } else {
            ESP_LOGI(TAG, "Path exists but is not a directory: %s", folder_path);
        }
    }
    FILE *fp = fopen("/sdcard/scaife/scaife.perseus.org.json", "r");
    char path[32] = "/sdcard/scaife/catalog-a.csv";
    char *buffer = (char *)heap_caps_malloc((24*1024), MALLOC_CAP_SPIRAM);
    char line[200];
    char temp[128];
    char *tag1 = "\"json_url\": \"";
    char *tag2 = "\"label\": \"";
    char *t = tag1;
    char *p = nullptr;
    int bar =0; int loc =1847310; int next =0;
    if (fp == NULL) goto done;
    ESP_LOGI(TAG, "325");
    memcpy(buffer, "          ", 10);
    while (fgets(buffer + 10, (24*1024) - 10, fp)) {
    	p = buffer;     //ESP_LOGI(TAG, "%.50s", p);
    	while (p && (p = strstr(p, t)) != NULL) {
    		p += strlen(t);
			const char *q = p;
			while (*q && !(*q == '"' && *(q-1) != '\\')) q++;
			if (!*q) {
				int rem = (int)(buffer + 10 + strlen(buffer + 10) - (p - strlen(t)));
				memmove(buffer + 10, p - strlen(t), rem);
				buffer[10 + rem] = '\0';
				if (fgets(buffer + 10 + rem, (24*1024) - 10 - rem, fp) == NULL) {
					fclose(fp);
					return 1;
				}
				p = buffer;
				continue;
			}
			int len = (int)(q - p);
			if (t == tag1) {
				snprintf(temp, 128, "%.*s", len, p);
				next = locate_texts(temp, len, loc);
				t = tag2;
			} else {
				snprintf(line, 200, "%.*s,%s,%d", len, p, temp, loc);
				snprintf(path, 32, "/sdcard/scaife/catalog-%c.csv", *p|0x20);
				FILE *fp1= fopen(path, "a");
				fprintf(fp1, "%s\n", line); ESP_LOGI(TAG, "%s", line);
				fclose(fp1);
				if (strncmp(line, "va", 2) == 0) goto done;
				t = tag1;
				bar++; loc=next;
			}
		}
		if (p == NULL) {
			memcpy(buffer, buffer + (24*1024) - 11, 10);
		}
		//vTaskDelay(pdMS_TO_TICKS(10));
		
    }
	ESP_LOGI(TAG, "end of file");
done: fclose(fp);
	free(buffer);
    return bar;
}