#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "dirent.h"

#include "esp_log.h"
#include <errno.h>
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "esp_vfs_fat.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "webserver.h"
#include "epub_parser.h"
#include "scaife/app/page_state.h"

static const char *TAG = "WEBSERVER";
#define WIFI_MAX_RETRY  4

/* ─── SD SPI pin config ───────────────────────────────────────────────────── */
#define SD_MOSI_GPIO   13
#define SD_MISO_GPIO   21
#define SD_CLK_GPIO    14
#define SD_CS_GPIO     12

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static int s_retry_num = 0;
webserver_ctx_t s_webserver = {0};

/* ─── WiFi event handler ─────────────────────────────────────────────────── */

void srvr_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retrying WiFi connection...");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        webserver_stop(&s_webserver);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        //webserver_start(&s_webserver);
    }
}

/* ─── WiFi init ──────────────────────────────────────────────────────────── */

esp_err_t wifi_init_sta(char *s, char *pwd)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t inst_any, inst_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &srvr_event_handler, NULL, &inst_any));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &srvr_event_handler, NULL, &inst_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
//             .ssid     = (uint8_t)s,
//             .password = (uint8_t)pwd,
            .threshold= {.authmode = WIFI_AUTH_WPA2_PSK},
        },
    };
	strncpy((char *)wifi_config.sta.ssid, s, sizeof(wifi_config.sta.ssid));
	strncpy((char *)wifi_config.sta.password, pwd, sizeof(wifi_config.sta.password));
	
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to: %s", WIFI_SSID);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Failed to connect");
    return ESP_FAIL;
}

esp_err_t wifi_connect(void){
	esp_err_t err = wifi_init_sta(WIFI_SSID, WIFI_PASS);
	if (err) {
		return wifi_init_sta(WIFI_SSID_ALT, WIFI_PASS_ALT);
	} else {
		return ESP_OK;
	}
}
/* ─── SD card init (SPI) ─────────────────────────────────────────────────── */

esp_err_t sd_init()
{
    static sdmmc_card_t *s_card = NULL;
    spi_host_device_t spi_host = SPI2_HOST;
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files              = 8,
        .allocation_unit_size   = 16 * 1024,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 400;
    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num   = SD_MOSI_GPIO;
    bus_cfg.miso_io_num   = SD_MISO_GPIO;
    bus_cfg.sclk_io_num   = SD_CLK_GPIO;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 4096;

    esp_err_t ret = spi_bus_initialize(
        static_cast<spi_host_device_t>(host.slot),
        &bus_cfg,
        SPI_DMA_CH_AUTO
    );
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = static_cast<gpio_num_t>(SD_CS_GPIO);
    slot_config.host_id = static_cast<spi_host_device_t>(host.slot);

    ret = esp_vfs_fat_sdspi_mount(
        SD_MOUNT_POINT,
        &host,
        &slot_config,
        &mount_config,
        &s_card
    );
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    sdmmc_card_print_info(stdout, s_card);
    ESP_LOGI(TAG, "SD card mounted at %s", SD_MOUNT_POINT);
    const char *folder_path = "/sdcard/books";

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
    return ESP_OK;
}

/* ─── Demo: read and print a page ────────────────────────────────────────── */

void demo_read_page(const char *epub_path, uint32_t page_num)
{
    epub_page_cfg_t cfg = {
        .chars_per_page = 512,
    };

    uint32_t total = 0;
    if (epub_page_count(epub_path, &cfg, &total) == ESP_OK) {
        ESP_LOGI(TAG, "Book has %u pages", total);
    }

    epub_page_result_t result;
    esp_err_t ret = epub_get_page(epub_path, page_num, &cfg, &result);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get page %u: %s",
                 page_num, esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "─── Page %u / %u ───────────────────────────", page_num, total);

    for (size_t i = 0; i < result.span_count; i++) {
        const char *type_str = (result.spans[i].type == TEXT_HEADING)
                               ? "[H]" : "[T]";
        ESP_LOGI(TAG, "%s %s", type_str, result.spans[i].text);
    }

    ESP_LOGI(TAG, "─── FLAT BUFFER ────────────────────────────");
    printf("%s\n", result.flat);

    epub_page_result_free(&result);
}

/* ─── HTML upload page (with NVS array editor) ───────────────────────────── */

static const char UPLOAD_HTML[] = R"rawhtml(<!DOCTYPE html><html><head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>EPUB Upload &amp; NVS Editor</title>
<script src='https://unpkg.com/jszip@3.10.1/dist/jszip.min.js'></script>
<style>
body{font-family:sans-serif;max-width:600px;margin:40px auto;padding:0 16px}
h2,h3{color:#333}
input[type=file]{margin:12px 0;display:block}
.btn{background:#2a7ae2;color:#fff;border:none;padding:10px 24px;
     border-radius:4px;cursor:pointer;font-size:1rem;margin:4px 2px}
.btn:hover{background:#1a5bb5}
.btn-red{background:#c0392b}.btn-red:hover{background:#962d22}
#prog{margin:12px 0;height:20px;background:#eee;border-radius:4px;overflow:hidden}
#bar{height:100%;background:#2a7ae2;width:0%;transition:width 0.2s}
#status{margin-top:16px;color:#444;white-space:pre-wrap;font-size:0.9rem;
        max-height:200px;overflow-y:auto}
.editor{background:#f4f4f4;padding:16px;border-radius:8px;margin-top:24px}
.editor label{display:inline-block;width:60px;font-weight:bold}
.editor input[type=number]{width:60px;padding:6px;margin:4px 8px 4px 0;
      border:1px solid #aaa;border-radius:4px}
.editor input[type=text]{width:100%;padding:8px;margin:8px 0;
      border:1px solid #aaa;border-radius:4px;box-sizing:border-box}
#nvs-status{margin-top:8px;color:#27ae60;font-weight:bold}
.dump{background:#222;color:#0f0;padding:12px;border-radius:6px;
      font-family:monospace;font-size:0.85rem;white-space:pre-wrap;
      max-height:400px;overflow-y:auto;margin-top:12px}
</style></head><body>

<h2>Upload EPUB</h2>
<input type='file' id='file' accept='.epub'>
<button class='btn' onclick='upload()'>Upload</button>
<div id='prog'><div id='bar'></div></div>
<div id='status'></div>
<h3>Books on device:</h3>
<button class='btn' onclick='loadList()'>Refresh List</button>
<button class='btn btn-red' onclick='stopServer()'>Exit</button>
<div id='list'>Loading...</div>

<div class='editor'>
<h3>NVS String Array Editor [4 &times; 8 &times; 4]</h3>
<div>
<label>Page</label><input type='number' id='pg' min='0' max='3' value='0'>
<label>Entry</label><input type='number' id='en' min='0' max='7' value='0'>
<label>Attr</label><input type='number' id='at' min='0' max='5' value='0'>
</div>
<button class='btn' onclick='nvsLoad()'>Load</button>
<input type='text' id='nvsval' placeholder='string value (Enter to save)' maxlength='127'>
<div id='nvs-status'></div>
<button class='btn' onclick='nvsDump()'>Dump All</button>
<div id='nvs-dump'></div>
</div>

<script>
/* ── EPUB upload with client-side decompression ── */
async function upload() {
  var f = document.getElementById('file').files[0];
  if (!f) return;
  var status = document.getElementById('status');
  var bar = document.getElementById('bar');
  bar.style.width = '0%';
  status.textContent = 'Reading ZIP in browser...\n';

  var bookName = f.name.replace(/\.epub$/i, '').replace(/[^a-zA-Z0-9._-]/g, '_');

  try {
    var zipData = await f.arrayBuffer();
    var zip = await JSZip.loadAsync(zipData);
    var entries = Object.keys(zip.files);
    status.textContent += 'Found ' + entries.length + ' files in EPUB\n';

    var resp = await fetch('/begin', {
      method: 'POST',
      headers: {'Content-Type':'application/json'},
      body: JSON.stringify({book: bookName, count: entries.length})
    });
    if (!resp.ok) throw new Error('Begin failed: ' + await resp.text());

    var sent = 0;
    for (var i = 0; i < entries.length; i++) {
      var path = entries[i];
      var entry = zip.files[path];
      if (entry.dir) { sent++; continue; }

      var data = await entry.async('uint8array');
      status.textContent += 'Sending: ' + path + ' (' + data.length + ' B)\n';
      status.scrollTop = status.scrollHeight;

      resp = await fetch('/file', {
        method: 'POST',
        headers: {
          'X-Path': encodeURIComponent(bookName + '/' + path),
          'Content-Type': 'application/octet-stream'
        },
        body: data
      });
      if (!resp.ok) throw new Error('Failed ' + path + ': ' + await resp.text());

      sent++;
      bar.style.width = Math.round((sent / entries.length) * 100) + '%';
    }

    await fetch('/end', {
      method: 'POST',
      headers: {'Content-Type':'application/json'},
      body: JSON.stringify({book: bookName})
    });

    status.textContent += '\nDone! ' + sent + ' files uploaded.\n';
    bar.style.width = '100%';
    loadList();
  } catch(err) {
    status.textContent += '\nERROR: ' + err.message + '\n';
  }
}

function loadList(){
  fetch('/list').then(function(r){return r.text();}).then(function(t){
    document.getElementById('list').innerHTML = t;
  });
}
function stopServer(){ fetch('/quit'); }

/* ── NVS editor ── */
function nvsIdx(){
  return 'page=' + document.getElementById('pg').value
    + '&entry=' + document.getElementById('en').value
    + '&attr=' + document.getElementById('at').value;
}
function nvsLoad(){
  fetch('/nvs_get?' + nvsIdx()).then(function(r){return r.json();}).then(function(j){
    document.getElementById('nvsval').value = j.value || '';
    document.getElementById('nvs-status').textContent =
      j.ok ? 'Loaded ['+j.page+']['+j.entry+']['+j.attr+']' : '(empty)';
  });
}
document.getElementById('nvsval').addEventListener('keydown', function(e){
  if (e.key === 'Enter') {
    e.preventDefault();
    var v = this.value;
    fetch('/nvs_set?' + nvsIdx() + '&value=' + encodeURIComponent(v))
    .then(function(r){return r.json();}).then(function(j){
      document.getElementById('nvs-status').textContent =
        j.ok ? 'Saved ['+j.page+']['+j.entry+']['+j.attr+']' : 'Error: '+j.err;
    });
  }
});
function nvsDump(){
  fetch('/nvs_dump').then(function(r){return r.text();}).then(function(t){
    document.getElementById('nvs-dump').innerHTML = '<div class="dump">' + t + '</div>';
  });
}

loadList();
</script></body></html>)rawhtml";


/* ─── Helper: ensure directory exists ───────────────────────────────────── */

void ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        mkdir(path, 0755);
    }
}
static void mkdir_p(const char *path)
{
    char *tmp = strdup(path);
    if (!tmp) return;

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            struct stat st;
            if (stat(tmp, &st) != 0) {
                mkdir(tmp, 0755);
            }
            *p = '/';
        }
    }
    free(tmp);
}

static void url_decode(const char *src, char *dst, size_t dst_sz)
{
    char *d = dst;
    char *end = dst + dst_sz - 1;
    while (*src && d < end) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], 0 };
            *d++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *d++ = ' ';
            src++;
        } else {
            *d++ = *src++;
        }
    }
    *d = '\0';
}

esp_err_t quit_get_handler(httpd_req_t *req)
{
    httpd_resp_sendstr(req, "Server stopping...");
    webserver_stop(&s_webserver);
    return ESP_OK;
}

/* ─── GET / → upload page ────────────────────────────────────────────────── */

esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, UPLOAD_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* ─── GET /list → simple HTML list of epubs on SD ───────────────────────── */

esp_err_t list_get_handler(httpd_req_t *req)
{
    ensure_dir(EPUB_STORAGE_PATH);

    DIR *dir = opendir(EPUB_STORAGE_PATH);

    size_t buf_sz = 4096;
    char *buf = (char *)malloc(buf_sz);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    size_t pos = 0;
    pos += snprintf(buf + pos, buf_sz - pos, "<ul>");

    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            /* Show directories (each is an extracted book) and .epub files */
            if (entry->d_type == DT_DIR) {
                if (strcmp(entry->d_name, ".") == 0 ||
                    strcmp(entry->d_name, "..") == 0) continue;
                int w = snprintf(buf + pos, buf_sz - pos,
                                 "<li>&#128214; %s/</li>", entry->d_name);
                if (w > 0) pos += w;
            }
            if (pos >= buf_sz - 128) break;
        }
        closedir(dir);
    }

    if (pos <= 5) { /* only "<ul>" */
        pos += snprintf(buf + pos, buf_sz - pos,
                        "<li><em>No books found</em></li>");
    }

    snprintf(buf + pos, buf_sz - pos, "</ul>");

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, buf);
    free(buf);
    return ESP_OK;
}



/* ─── Helper: parse integer query parameter ──────────────────────────────── */

static int query_get_int(httpd_req_t *req, const char *key, int def_val)
{
    char qstr[256];
    if (httpd_req_get_url_query_str(req, qstr, sizeof(qstr)) != ESP_OK)
        return def_val;
    char val[16];
    if (httpd_query_key_value(qstr, key, val, sizeof(val)) != ESP_OK)
        return def_val;
    return atoi(val);
}

static esp_err_t query_get_str(httpd_req_t *req, const char *key,
                                char *buf, size_t buf_sz)
{
    char qstr[512];
    if (httpd_req_get_url_query_str(req, qstr, sizeof(qstr)) != ESP_OK)
        return ESP_FAIL;
    return httpd_query_key_value(qstr, key, buf, buf_sz);
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void nvs_url_decode(char *dst, const char *src, size_t dst_sz)
{
    size_t di = 0;
    while (*src && di < dst_sz - 1) {
        if (*src == '%' && hex_val(src[1]) >= 0 && hex_val(src[2]) >= 0) {
            dst[di++] = (char)((hex_val(src[1]) << 4) | hex_val(src[2]));
            src += 3;
        } else if (*src == '+') {
            dst[di++] = ' ';
            src++;
        } else {
            dst[di++] = *src++;
        }
    }
    dst[di] = '\0';
}
/* ─── GET /nvs_get?page=P&entry=E&attr=A → JSON ─────────────────────────── */

esp_err_t nvs_get_handler(httpd_req_t *req)
{
    int pg = query_get_int(req, "page", -1);
    int en = query_get_int(req, "entry", -1);
    int at = query_get_int(req, "attr", -1);

    char value[NVS_STR_MAX] = {0};
    esp_err_t ret = nvs_array_get(pg, en, at, value, sizeof(value));

    /* JSON-escape the value minimally (escape quotes and backslashes) */
    char escaped[NVS_STR_MAX * 2];
    size_t ei = 0;
    for (size_t i = 0; value[i] && ei < sizeof(escaped) - 2; i++) {
        if (value[i] == '"' || value[i] == '\\') escaped[ei++] = '\\';
        escaped[ei++] = value[i];
    }
    escaped[ei] = '\0';

    char resp[512];
    snprintf(resp, sizeof(resp),
             "{\"ok\":%s,\"page\":%d,\"entry\":%d,\"attr\":%d,\"value\":\"%s\"}",
             (ret == ESP_OK) ? "true" : "false", pg, en, at, escaped);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

/* ─── GET /nvs_set?page=P&entry=E&attr=A&value=V → JSON ─────────────────── */

esp_err_t nvs_set_handler(httpd_req_t *req)
{
    int pg = query_get_int(req, "page", -1);
    int en = query_get_int(req, "entry", -1);
    int at = query_get_int(req, "attr", -1);

    char raw_value[NVS_STR_MAX] = {0};
    char value[NVS_STR_MAX] = {0};
    query_get_str(req, "value", raw_value, sizeof(raw_value));
    nvs_url_decode(value, raw_value, sizeof(value));    /* ← decode here */

    esp_err_t ret = nvs_array_set(pg, en, at, value);

    char resp[512];
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "NVS SET [%d][%d][%d] = \"%s\"", pg, en, at, value);
        snprintf(resp, sizeof(resp),
                 "{\"ok\":true,\"page\":%d,\"entry\":%d,\"attr\":%d}", pg, en, at);
    } else {
        snprintf(resp, sizeof(resp),
                 "{\"ok\":false,\"err\":\"%s\"}", esp_err_to_name(ret));
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

/* ─── GET /nvs_dump → plain text dump of all stored strings ──────────────── */

esp_err_t nvs_dump_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");

    char line[256];
    char value[NVS_STR_MAX];
    nvs_handle_t h;
	int32_t v = 0;
	if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {ESP_LOGI(TAG, "No NVS state found"); return ESP_FAIL;}
    if (nvs_get_i32(h, "last_book", &v) != ESP_OK) { return ESP_FAIL; }
    nvs_close(h);
	int len = snprintf(line, sizeof(line), "last_book: %d \n", (int)v);
    httpd_resp_send_chunk(req, line, len);
    for (int p = 0; p < NVS_PAGES; p++) {
        for (int e = 0; e < NVS_ENTRIES; e++) {
            for (int a = 0; a < NVS_ATTRS; a++) {
                esp_err_t ret = nvs_array_get(p, e, a, value, sizeof(value));
                if (ret == ESP_OK && value[0] != '\0') {
                    int len = snprintf(line, sizeof(line),
                                       "[%d][%d][%d] = \"%s\"\n", p, e, a, value);
                    httpd_resp_send_chunk(req, line, len);
                }
            }
        }
    }

    /* If nothing was printed, say so */
    char check[NVS_STR_MAX];
    bool any = false;
    for (int p = 0; p < NVS_PAGES && !any; p++)
        for (int e = 0; e < NVS_ENTRIES && !any; e++)
            for (int a = 0; a < NVS_ATTRS && !any; a++)
                if (nvs_array_get(p, e, a, check, sizeof(check)) == ESP_OK && check[0])
                    any = true;
    if (!any) {
        httpd_resp_send_chunk(req, "(all entries empty)\n", HTTPD_RESP_USE_STRLEN);
    }

    /* End chunked response */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* ─── POST /upload → receive multipart, write to SD ─────────────────────── */

static esp_err_t begin_post_handler(httpd_req_t *req)
{
    int content_len = req->content_len;
    if (content_len <= 0 || content_len > 512) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body");
        return ESP_FAIL;
    }

    char *body = (char *)malloc(content_len + 1);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    int received = 0;
    while (received < content_len) {
        int ret = httpd_req_recv(req, body + received, content_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            free(body);
            return ESP_FAIL;
        }
        received += ret;
    }
    body[received] = '\0';

    ESP_LOGI(TAG, "Begin upload: %s", body);

    ensure_dir(EPUB_STORAGE_PATH);

    free(body);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * POST /file  — receive one decompressed file from browser
 * ═══════════════════════════════════════════════════════════════════════════ */

static esp_err_t file_post_handler(httpd_req_t *req)
{
    /* Get path from X-Path header */
    char *encoded_path = (char *)malloc(512);
    if (!encoded_path) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    if (httpd_req_get_hdr_value_str(req, "X-Path",
                                     encoded_path, 512) != ESP_OK) {
        free(encoded_path);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing X-Path");
        return ESP_FAIL;
    }

    char *rel_path = (char *)malloc(512);
    if (!rel_path) {
        free(encoded_path);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    url_decode(encoded_path, rel_path, 512);
    free(encoded_path);

    /* Sanitize */
    if (strstr(rel_path, "..")) {
        free(rel_path);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
        return ESP_FAIL;
    }

    /* Build full path */
    char *full_path = (char *)malloc(640);
    if (!full_path) {
        free(rel_path);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    snprintf(full_path, 640, "%s/%s", EPUB_STORAGE_PATH, rel_path);
    free(rel_path);

    /* Create parent directories */
    mkdir_p(full_path);

    /* Open file for writing */
    FILE *f = fopen(full_path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot create: %s", full_path);
        free(full_path);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Cannot create file");
        return ESP_FAIL;
    }

    /* Stream body to file */
    int total = req->content_len;
    int received = 0;

    char *chunk = (char *)malloc(UPLOAD_BUFFER_SIZE);
    if (!chunk) {
        fclose(f);
        free(full_path);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    bool write_ok = true;
    while (received < total) {
        int to_read = total - received;
        if (to_read > UPLOAD_BUFFER_SIZE) to_read = UPLOAD_BUFFER_SIZE;

        int ret = httpd_req_recv(req, chunk, to_read);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ESP_LOGE(TAG, "Receive error at %d/%d", received, total);
            write_ok = false;
            break;
        }

        size_t written = fwrite(chunk, 1, ret, f);
        if ((int)written != ret) {
            ESP_LOGE(TAG, "Write error: %zu/%d", written, ret);
            write_ok = false;
            break;
        }
        received += ret;
    }

    free(chunk);
    fclose(f);

    if (write_ok) {
        ESP_LOGI(TAG, "Wrote %d bytes -> %s", received, full_path);
        free(full_path);
        httpd_resp_sendstr(req, "OK");
        return ESP_OK;
    } else {
        free(full_path);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Write failed");
        return ESP_FAIL;
    }
}

static esp_err_t end_post_handler(httpd_req_t *req)
{
    char *body = (char *)malloc(256);
    if (body) {
        int ret = httpd_req_recv(req, body, 255);
        if (ret > 0) {
            body[ret] = '\0';
            ESP_LOGI(TAG, "Upload complete: %s", body);
        }
        free(body);
    }
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

/* ─── Start / Stop ───────────────────────────────────────────────────────── */

esp_err_t webserver_start(webserver_ctx_t *ctx)
{
    if (ctx->running) {
        ESP_LOGW(TAG, "Server already running");
        return ESP_OK;
    }

    httpd_config_t config      = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers    = 12;
    config.stack_size          = 8192;
    config.recv_wait_timeout   = 30;
    config.send_wait_timeout   = 30;
    config.max_open_sockets    = 3;

    esp_err_t ret = httpd_start(&ctx->server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start httpd: %s", esp_err_to_name(ret));
        return ret;
    }

    const httpd_uri_t routes[] = {
        { .uri = "/",         .method = HTTP_GET,  .handler = root_get_handler,  .user_ctx = NULL },
        { .uri = "/list",     .method = HTTP_GET,  .handler = list_get_handler,  .user_ctx = NULL },
        { .uri = "/begin",    .method = HTTP_POST, .handler = begin_post_handler,.user_ctx = NULL },
        { .uri = "/file",     .method = HTTP_POST, .handler = file_post_handler, .user_ctx = NULL },
        { .uri = "/end",      .method = HTTP_POST, .handler = end_post_handler,  .user_ctx = NULL },
        { .uri = "/quit",     .method = HTTP_GET,  .handler = quit_get_handler,  .user_ctx = ctx  },
        { .uri = "/nvs_get",  .method = HTTP_GET,  .handler = nvs_get_handler,   .user_ctx = NULL },
        { .uri = "/nvs_set",  .method = HTTP_GET,  .handler = nvs_set_handler,   .user_ctx = NULL },
        { .uri = "/nvs_dump", .method = HTTP_GET,  .handler = nvs_dump_handler,  .user_ctx = NULL },
    };

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(ctx->server, &routes[i]);
    }

    ctx->running = true;
    ESP_LOGI(TAG, "Web server started (with NVS editor)");
    return ESP_OK;
}

esp_err_t webserver_stop(webserver_ctx_t *ctx)
{
    if (!ctx->running) {
		ESP_LOGE(TAG, "Web server already stopped!");
    	return ESP_OK;
    }
    ctx->running = false;
    ESP_LOGI(TAG, "Web server stopping");
    httpd_stop(ctx->server);
    return ESP_OK;
}