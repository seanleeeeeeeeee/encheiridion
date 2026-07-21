#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "esp_err.h"
#include "esp_http_server.h"

#define WIFI_SSID  "Seans-iPhone"
#define WIFI_PASS  "password"
#define WIFI_SSID_ALT  "Easyconnect2520"
#define WIFI_PASS_ALT  "chase5374anyway"

#define UPLOAD_BUFFER_SIZE      4096
#define SD_MOUNT_POINT          "/sdcard"
#define EPUB_STORAGE_PATH       "/sdcard/books"
#define MAX_FILENAME_LEN        128
typedef struct {
    httpd_handle_t server;
    bool running;
} webserver_ctx_t;
extern webserver_ctx_t s_webserver;

void srvr_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data);
esp_err_t wifi_connect(void);
esp_err_t sd_init(void);
void demo_read_page(const char *epub_path, uint32_t page_num);
void ensure_dir(const char *path);
esp_err_t upload_post_handler(httpd_req_t *req);
esp_err_t list_get_handler(httpd_req_t *req);
esp_err_t root_get_handler(httpd_req_t *req);


esp_err_t webserver_start(webserver_ctx_t *ctx);
esp_err_t webserver_stop(webserver_ctx_t *ctx);

#endif // WEBSERVER_H
