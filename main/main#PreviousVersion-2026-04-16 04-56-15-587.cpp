#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <sys/stat.h>
#include "dirent.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "miniz.h"
#include "esp_wifi.h"
#include "FastEPD.h"
#include "gt911.h"
#include "buttons.c"

#include "scaife/scaife_client.h"
#include "scaife/xml_parser.h"
#include "scaife/app/page_state.h"
#include "scaife/app/s_navigator.h"

#include "reader/epub_parser.h"
#include "reader/webserver.h"
#include "reader/app/r_navigator.h"

#include "apps/navigator.h"

#include "ui/header.h"
#include "ui/column.h"
#include "ui/opus_picker.h"
#include "ui/numpad.h"
#include "ui/epd_update.h"
#include "ui/ui_event.h"
#include "ui/touch_bus.h"

#include "strings/greek_encode.h"
#include "fonts/font_manager.h"

#define GT911_IRQ_PIN  3
#define WIFI_SSID  "Seans-iPhone"
#define WIFI_PASS  "password"

static const char *TAG = "encheiridion";

FASTEPD main_epaper;
FASTEPD* epaper = &main_epaper;
BB_RECT rect;
gt911_handle_t g_touch   = NULL;

QueueHandle_t g_ui_queue = NULL;
TaskHandle_t xServer = NULL;
TaskHandle_t xTouchTask = NULL;
TaskHandle_t xIoTask = NULL;
TaskHandle_t xUiTask = NULL;

static TextLine *g_lines_grc = NULL;
static TextLine *g_lines_eng = NULL;
static int       g_n_grc     = 0;
static int       g_n_eng     = 0;

static const NavigatorInterface *active_nav = NULL;
static PageState g_state;
typedef enum {scaife = 0,server,reader} AppState;
AppState m_state;
static bool select_mode = false;

static void server_task(void *arg);
static bool render_current_page(void);
static bool load_passage_and_wrap(void);
static void handle_touch(int tx, int ty);

int scaife_reset() {
    const char *path = "/sdcard/scaife/";          // Directory to scan
    const char *prefix = "catalog";     // Files starting with this will be deleted
    struct dirent *entry;
    DIR *dir = opendir(path);

    if (dir == NULL) return 1;

    while ((entry = readdir(dir)) != NULL) {
        // Check if filename starts with the prefix
        if (strncmp(entry->d_name, prefix, strlen(prefix)) == 0) {
            if (remove(entry->d_name) == 0) {
                ESP_LOGI(TAG, "Deleted: %s\n", entry->d_name);
            } else {
                ESP_LOGI(TAG, "Error deleting %s", entry->d_name);
            }
        }
    }
    closedir(dir);
    return 0;
}
int max_page_for_passage(void)
{
    int mv        = max_visible_lines(g_state.shelf % 2);
    int max_lines = (g_n_grc > g_n_eng) ? g_n_grc : g_n_eng;
    if (max_lines == 0) return 0;
    ESP_LOGI("h", "lines: %d / %d = %d", max_lines, mv, (max_lines - 1) / mv);
    return (max_lines - 1) / mv;
}

// ── Render ────────────────────────────────────────────────────────────────────
static bool render_current_page(void)
{
	flag_of_pausing = true;
	ESP_LOGI(TAG, "99");
    touch_bus_pre_update();
    ESP_LOGI(TAG, "101");

    if (!g_lines_grc && !g_lines_eng) return false;

    int mv = max_visible_lines(g_state.shelf % 2);
    char left_buf[64];
    char right_buf[64];
    snprintf(right_buf, sizeof(right_buf), "%s..%d", g_state.urn_ref, g_state.page);
    nvs_array_get(g_state.shelf, g_state.opus_index, 0, left_buf, sizeof(left_buf));
	landscape();
    draw_header(left_buf, right_buf);

    if (g_state.shelf % 2 == 0){
    	ESP_LOGI(TAG, "grc %d %d", COL_RIGHT_X, TEXT_Y_START);
    	draw_column(g_lines_grc, g_n_grc,
                COL_RIGHT_X, TEXT_Y_START, mv, g_state.page,
                g_orpheus16_ram, g_orpheus_gk_ram, g_orpheus_ext_ram);
    } else { portrait(); }
    ESP_LOGI(TAG, "eng %d %d", COL_LEFT_X, TEXT_Y_START);
    draw_column(g_lines_eng, g_n_eng,
                COL_LEFT_X, TEXT_Y_START, mv, g_state.page,
                g_futura16_ram, g_orpheus_gk_ram, g_orpheus_ext_ram);
    main_epaper.drawRect(2, 2, screen_w - 4, screen_h - 4, BBEP_BLACK);
    //epd_full_update();
    main_epaper.fullUpdate(false, false);
    touch_bus_post_update();
    flag_of_pausing = false;
    ESP_LOGI(TAG, "Page rendered: %s .. %d", g_state.urn_ref, g_state.page);
    main_epaper.setMode(BB_MODE_1BPP);
    return true;
}

// ── Load ──────────────────────────────────────────────────────────────────────
static bool load_passage_and_wrap(void)
{
    {rect.x = rect.y = 0;
    rect.h = rect.w = 50;
    main_epaper.fillRect(0, 0, 50, 50, BBEP_BLACK);
    flag_of_pausing = true;
    touch_bus_pre_update();
    main_epaper.fullUpdate(true, false, &rect);
    touch_bus_post_update();
    flag_of_pausing = false;} // loading square
    PassageData pd; 
	if (!active_nav->load(&g_state, &pd)) return false;
	if (pd.text_grc == NULL){
		ESP_LOGI(TAG, "womp");
	}
	ESP_LOGI(TAG, "got chapter: %.60s", pd.text_grc);
	g_n_grc = wrap_text(pd.text_grc, col_w*2, g_lines_grc, MAX_LINES,
						g_orpheus16_ram, g_orpheus_gk_ram, g_orpheus_ext_ram);
	if (pd.text_eng){
		g_n_eng = wrap_text(pd.text_eng, col_w*2, g_lines_eng, MAX_LINES,
							g_futura16_ram,  g_orpheus_gk_ram, g_orpheus_ext_ram);
	}
	ESP_LOGI(TAG, "wrapped: grc=%d eng=%d", g_n_grc, g_n_eng);
	passage_data_free(&pd);
	return true;
}

// ── Touch dispatcher ──────────────────────────────────────────────────────────
static void handle_touch(int16_t tx, int16_t ty)
{
    ESP_LOGI(TAG, "input %d,%d", tx, ty);
	if (!select_mode){
		if (header_hit_left(tx, ty)) {
			ESP_LOGI(TAG, "→ opus picker");
			select_mode = true;
			landscape();
			int sel = opus_picker_run(&g_state);
			ESP_LOGI(TAG, "opus picker → %d", sel);
			page_state_init(&g_state, sel);
			if (sel == 7) {
				char author[32];
				if (keyboard(NULL, author, 32)) {
					g_state.urn_ref[0] = '*'; g_state.page = (int)(g_lines_grc);
					snprintf(g_lines_grc[0].text, 256, "%s", author);
					active_nav->load(&g_state, NULL);
					ESP_LOGI(TAG, "Got %d authors", g_state.page);
					char label[128]; char json[128]; int loc=0;
					int n = urn_picker(1, g_lines_grc, g_state.page, 0);
					if (n < 0) {ESP_LOGE(TAG, "cancel"); return;}
					sscanf(g_lines_grc[n].text, "%[^,],%[^,],%d", label, json, &loc);
					ESP_LOGI(TAG, "\"%s\"", g_lines_grc[n].text);
					snprintf(g_lines_grc[0].text, 256, "%s", json);
					g_state.urn_ref[0] = '?'; g_state.page = loc;
					active_nav->load(&g_state, NULL);
					ESP_LOGI(TAG, "Got %d works", g_state.page);
					n = urn_picker(0, g_lines_grc, g_state.page, 0);
					if (n < 0) {ESP_LOGE(TAG, "cancel"); return;}
					nvs_array_set(0,7,2,g_lines_grc[n].text);
					nvs_array_set(0,7,3,g_lines_grc[n+1].text);
					ESP_LOGI(TAG, "got URN %s", g_lines_grc[n].text);
					goto numpad;
				}
				
			} else
			if (sel >= 0) {
				
				char urn_grc[128];
				nvs_array_get(g_state.shelf, g_state.opus_index, 2, urn_grc, 128);
				active_nav = (urn_grc[0] == '/') ? navigator_r() : navigator_scaife();
				ESP_LOGI(TAG, "Using navigator: %s", active_nav->name);
				if (!load_passage_and_wrap()) 
				{	ESP_LOGE(TAG, "Failed to load opus %d", sel); return;
				}
			}
			select_mode = false;
			if (g_state.shelf % 2)	{portrait();}	else	{landscape();}
			render_current_page();
			return;
		}
		if (header_hit_right(tx, ty)) {
			char urn_grc[128];
			nvs_array_get(g_state.shelf, g_state.opus_index, 2, urn_grc, 128);
			landscape();
			if (urn_grc[0] == '/') {
				ESP_LOGI(TAG, "→ chapter");
				select_mode = true;
				int ch = chapter_picker_run(0);
				ESP_LOGI(TAG, "%d", ch);
				if (ch >= 0) {
					char pg[5];
					if (!numpad_run(NULL, pg, 5)) snprintf(pg, 5, "0");
					snprintf(g_state.urn_ref, URN_REF_MAX, spine->entries[ch].idref);
					g_state.page = atoi(pg);
					ESP_LOGI(TAG, "%d", g_state.page);
					load_passage_and_wrap();
				}
			} else {
numpad:			ESP_LOGI(TAG, "→ numpad");
				select_mode = true;
				char new_ref[URN_REF_MAX];
				int ret = numpad_run(g_state.urn_ref, new_ref, URN_REF_MAX);
				ESP_LOGI(TAG, "numpad stat=%d ref=%s", ret, new_ref);
				if (ret == -20) {
					//scaife_reset();
					ESP_LOGI(TAG, "loaded %d groups", load_authors_json());
				} else if (ret == -10) {// IO48 = server
					m_state = server;
					xTaskCreatePinnedToCore(server_task,"server", 24576, NULL, 2, &xServer, 0);
				} else if (ret && strlen(new_ref)>0 && strcmp(new_ref, g_state.urn_ref)!=0) {
					strncpy(g_state.urn_ref, new_ref, URN_REF_MAX - 1);
					g_state.urn_ref[URN_REF_MAX - 1] = '\0';
					g_state.page = 0;
					if (!load_passage_and_wrap()) ESP_LOGE(TAG, "Failed to load refs");
				}
			}
			select_mode = false;
			if (g_state.shelf % 2)	{portrait();}	else	{landscape();}
			render_current_page();
			return;
		}
		if (page_hit_left(tx, ty))  {
			bool load = active_nav->go_prev_page(&g_state);
			if (load){
				load_passage_and_wrap();
				g_state.page = max_page_for_passage();
				page_state_save(&g_state);
			}
			render_current_page();
			return; 
		}
		if (page_hit_right(tx, ty)) {
			bool load = active_nav->go_next_page(&g_state, max_page_for_passage());
			if (load) {
				load_passage_and_wrap();
			}
			render_current_page();
			return;
		}
	}
    ESP_LOGW(TAG, "touch not in any zone");
}

static void touch_task(void *arg)
{
    ESP_LOGI(TAG, "touch_task started");
    bool prev_touching = false;
    bool io_buf = false;
	bool bt_buf = false;
	bool io_pressed = false;
    bool boot_pressed = false;
    gt911_touch_data_t td;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(20));
        //touch_bus_lock();
        if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE){
			if (!g_touch) { xSemaphoreGive(s_mutex); continue; }
			esp_err_t err = gt911_read_touch(g_touch, &td);
			io_pressed = pca9535_get_button();
			if (err == ESP_ERR_NOT_FOUND) prev_touching = false;
			// if (err != ESP_OK) {
// 				
// 				xSemaphoreGive(s_mutex);ESP_LOGI(TAG, "2");
// 				continue;
// 			}
			boot_pressed = !gpio_get_level(GPIO_NUM_0);
			if (boot_pressed && !bt_buf){
				UiEvent evt = { UI_EVT_TOUCH,(int16_t)(-10),(int16_t)(0) };
				xQueueSend(g_ui_queue, &evt, pdMS_TO_TICKS(200));
				bt_buf = true;
			} else if (!boot_pressed){
				bt_buf = false;
			}
			if (io_pressed && !io_buf) {
				ESP_LOGI(TAG, "IO/0,-10");
				UiEvent evt = { UI_EVT_TOUCH,(int16_t)(0),(int16_t)(-10) };
				xQueueSend(g_ui_queue, &evt, pdMS_TO_TICKS(200));
				io_buf = true;
			}else if (!io_pressed){
				io_buf = false;
			}
			if (td.count > 0 && !prev_touching) {
				ESP_LOGI(TAG, "touch_task: %d,%d", td.points[0].x, td.points[0].y);
				UiEvent evt = { UI_EVT_TOUCH, (int16_t)td.points[0].x, (int16_t)td.points[0].y };
				xQueueSend(g_ui_queue, &evt, pdMS_TO_TICKS(200));
				prev_touching = true;
			} else if (td.count == 0) {
				prev_touching = false;
			}
			xSemaphoreGive(s_mutex);
        }
    }
}
static void io_task(void *arg)
{	ESP_LOGI(TAG, "io_task started");
	bool io_buf = false;
	bool bt_buf = false;
	bool io_pressed = false;
    bool boot_pressed = false;
    while (true)
    {	
		vTaskDelay(pdMS_TO_TICKS(25));
		if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE && !flag_of_pausing)
    	{	io_pressed = pca9535_get_button();
    		xSemaphoreGive(s_mutex);
    	}
        boot_pressed = !gpio_get_level(GPIO_NUM_0);
		if (boot_pressed && !bt_buf)
		{	ESP_LOGI(TAG, "BOOT/-10,0");
            UiEvent evt = { UI_EVT_TOUCH,(int16_t)(-10),(int16_t)(0) };
            xQueueSend(g_ui_queue, &evt, pdMS_TO_TICKS(200));
            bt_buf = true;
		} else if (!boot_pressed){
			bt_buf = false;
		}
		if (io_pressed && !io_buf) {
			ESP_LOGI(TAG, "IO/0,-10");
            UiEvent evt = { UI_EVT_TOUCH,(int16_t)(0),(int16_t)(-10) };
            xQueueSend(g_ui_queue, &evt, pdMS_TO_TICKS(200));
            io_buf = true;
		}else if (!io_pressed){
			io_buf = false;
		}
		xSemaphoreGive(s_mutex);
    }
}
static void server_task(void *arg)
{
	ESP_LOGI(TAG, "server task started");
	vTaskDelay(pdMS_TO_TICKS(1000));
	webserver_start(&s_webserver);
	vTaskDelete(NULL);
}
static void ui_task(void *arg)
{	ESP_LOGI(TAG, "ui_task started");
    UiEvent evt;
    while (true) {
        ESP_LOGI(TAG, "ui_task: waiting");
        if (xQueueReceive(g_ui_queue, &evt, portMAX_DELAY) != pdTRUE)
            continue;

        ESP_LOGI(TAG, "ui_task: x=%d y=%d", evt.x, evt.y);

        switch (evt.type) {
        case UI_EVT_TOUCH:
            handle_touch(evt.x, evt.y);
            break;
        }

        ESP_LOGI(TAG, "ui_task: done");
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting app_main");
    int r1 = main_epaper.initPanel(BB_PANEL_EPDIY_V7);
    ESP_LOGI(TAG, "initPanel = %d", r1);
    int r2 = main_epaper.getMode();
    ESP_LOGI(TAG, "getMode = %d", r2);
    int r3 = main_epaper.setPanelSize(960, 540);
    main_epaper.setRotation(0);
    ESP_LOGI(TAG, "setPanelSize = %d", r3);
    ESP_LOGI(TAG, "free stack: %d", uxTaskGetStackHighWaterMark(NULL));
    g_ui_queue = xQueueCreate(4, sizeof(UiEvent));
    if (!g_ui_queue) { ESP_LOGE(TAG, "queue alloc failed"); return; }
    if (!s_mutex) {ESP_LOGE(TAG, "mutex create failed");}
    void *guard = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "guard @ %p", guard);
    g_lines_grc = (TextLine *)heap_caps_malloc(MAX_LINES * sizeof(TextLine), MALLOC_CAP_SPIRAM);
    g_lines_eng = (TextLine *)heap_caps_malloc(MAX_LINES * sizeof(TextLine), MALLOC_CAP_SPIRAM);
    if (!g_lines_grc || !g_lines_eng) {ESP_LOGE(TAG, "line buf alloc failed"); return;}

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase(); nvs_flash_init(); }
    if (!fonts_to_ram()) { ESP_LOGE(TAG, "fonts failed"); return; }

    page_state_init(&g_state, 0, true);
    char urn_grc[128];
	nvs_array_get(g_state.shelf, g_state.opus_index, 2, urn_grc, 128);
	active_nav = (urn_grc[0] == '/') ? navigator_r() : navigator_scaife();
	ESP_LOGI(TAG, "Using navigator: %s", active_nav->name);
	vTaskDelay(pdMS_TO_TICKS(20));
	
	pca9535_init();
	button_init();
    gt911_config_t gt911_cfg = GT911_DEFAULT_CONFIG();
		gt911_cfg.i2c_bus  = i2c_bus;
		gt911_cfg.x_max    = screen_w;
		gt911_cfg.y_max    = screen_h;
		gt911_cfg.touch_cb = NULL;
	esp_err_t er = gt911_init(&gt911_cfg, &g_touch);
	vTaskDelay(pdMS_TO_TICKS(20));
    if (er != ESP_OK){ESP_LOGI(TAG, "touch init error %s", esp_err_to_name(er));}
    ESP_LOGI(TAG, "ui ready");
    touch_bus_init(&conf, &gt911_cfg, i2c_bus, &g_touch, &s_pca9535_dev);
    touch_bus_pre_update();
    vTaskDelay(pdMS_TO_TICKS(500));
    int ret2 = main_epaper.clearWhite(true);
    //main_epaper.fullUpdate(true);
    //vTaskDelay(pdMS_TO_TICKS(500));
    landscape();
    //vTaskDelay(pdMS_TO_TICKS(500));
    //main_epaper.fillRect(0, 0, 960, 540, BBEP_WHITE);
    main_epaper.setFont(g_futura20_ram);
    main_epaper.setTextColor(BBEP_BLACK);
    main_epaper.drawString("Connecting to WiFi...", 400, 270);
    main_epaper.fullUpdate(true);
    main_epaper.einkPower(0);
    touch_bus_post_update();
	wifi_init_sta();
	vTaskDelay(pdMS_TO_TICKS(10));
	sd_init();
	vTaskDelay(pdMS_TO_TICKS(10));
	if (!gpio_get_level(GPIO_NUM_0)){
		char author[32];
		if (keyboard(NULL, author, 32)) {
		}
		//xTaskCreatePinnedToCore(server_task,"server", 24576, NULL, 2, &xServer, 0);
	}
//     main_epaper.clearWhite(true);
//     main_epaper.setFont(g_futura20_ram, false);
//     main_epaper.drawString("Loading passage...", MARGIN, screen_h / 2);
//     main_epaper.fullUpdate(true);

	
//     vTaskDelay(pdMS_TO_TICKS(10));
//     if (!load_passage_and_wrap()) {
//         main_epaper.clearWhite(true);
//         main_epaper.drawString("Failed to load.", MARGIN, screen_h / 2);
//         main_epaper.fullUpdate(true); main_epaper.einkPower(0);
//     }
// 	if (g_state.shelf % 2)	{portrait();}	else	{landscape();}
//     render_current_page();
	UiEvent evt = { UI_EVT_TOUCH,(int16_t)(500),(int16_t)(0) };
    xQueueSend(g_ui_queue, &evt, pdMS_TO_TICKS(200));
    xTaskCreatePinnedToCore(touch_task, "touch",
                            4096,  NULL, 5, &xTouchTask, 1);
    xTaskCreatePinnedToCore(ui_task,   "ui",
                            24576, NULL, 4, &xUiTask, 0);
     //xTaskCreatePinnedToCore(io_task, "btns",
     						//4096, NULL, 5, &xIoTask, 1);


    ESP_LOGI(TAG, "tasks launched");
    vTaskDelete(NULL);
}
    /*
    i2c_master_bus_handle_t i2c_bus;
    i2c_master_bus_config_t conf = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
    	.glitch_ignore_cnt = 7,
    	.flags = {
    		.enable_internal_pullup = true,
    	}
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&conf, &i2c_bus));*/

