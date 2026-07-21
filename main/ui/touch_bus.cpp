#include "touch_bus.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>
#include "driver/i2c_master.h"
bool flag_of_pausing = false;
SemaphoreHandle_t s_mutex = xSemaphoreCreateMutex();
static const char *TAG = "touch_bus";
static i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x20,
        .scl_speed_hz = 400000,
};


void touch_bus_init(const i2c_master_bus_config_t *bus_cfg,
                    const gt911_config_t          *gt911_cfg,
                    i2c_master_bus_handle_t        bus,
                    gt911_handle_t                *touch_ptr,
                    i2c_master_dev_handle_t		  *pca9535_dev)
{
    s_bus_cfg   = *bus_cfg;
    s_gt911_cfg = *gt911_cfg;
    s_bus       = bus;
    s_touch_ptr = touch_ptr;
    s_button_dev = pca9535_dev;

}

void touch_bus_lock(void)
{
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
}

void touch_bus_unlock(void)
{
    if (s_mutex) xSemaphoreGive(s_mutex);
}

void touch_bus_pre_update(void)
{
    touch_bus_lock();

    if (s_touch_ptr && *s_touch_ptr) {
        if (gt911_deinit(*s_touch_ptr) == ESP_OK)
            *s_touch_ptr = NULL;
        else
            ESP_LOGE(TAG, "gt911_deinit failed, NOT clearing handle");
    }

    if (s_button_dev && *s_button_dev) {
        if (i2c_master_bus_rm_device(*s_button_dev) == ESP_OK)
            *s_button_dev = NULL;
        else
            ESP_LOGE(TAG, "rm pca failed");
    }

    // Only delete the bus if every device was actually removed
    if (s_bus && (!*s_touch_ptr) && (!*s_button_dev)) {
        if (i2c_del_master_bus(s_bus) == ESP_OK)
            s_bus = NULL;
        else
            ESP_LOGE(TAG, "del bus failed, keeping handle");
    }
//     touch_bus_lock();
// 
//     // Step 1: deinit GT911 driver — removes i2c device from bus, frees handle
//     if (s_touch_ptr && *s_touch_ptr) {
//         esp_err_t err = gt911_deinit(*s_touch_ptr);
//         if (err != ESP_OK) {
//             ESP_LOGE(TAG, "gt911_deinit: 0x%x", err);
//         }
//         *s_touch_ptr = NULL;
//     }
// 	if (s_button_dev && *s_button_dev){
//     esp_err_t re = i2c_master_bus_rm_device(*s_button_dev);
//     if (re != ESP_OK){
//     	ESP_LOGI(TAG, "remove pca err");
//     }
//     *s_button_dev = NULL;
// 	}
//     ESP_LOGI(TAG, "bus devs deinitialised");
// 
//     // Step 2: now the bus has no devices — delete the peripheral entirely
//     if (s_bus) {
//         esp_err_t err = i2c_del_master_bus(s_bus);
//         ESP_LOGI(TAG, "i2c_del_master_bus: 0x%x", err);
//         s_bus = NULL;
//     }
// 
//     ESP_LOGE(TAG, "pre_update: I2C peripherals deleted");
}

void touch_bus_post_update(void)
{
    //ESP_LOGE(TAG, "post_update: recreating I2C peripheral");

    // Step 3: recreate the I2C master peripheral from scratch
    // This re-initializes all hardware registers to a clean state
    esp_err_t err = i2c_new_master_bus(&s_bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: 0x%x", err);
        touch_bus_unlock();
        return;
    }

    // Give the bus hardware time to stabilize
    vTaskDelay(pdMS_TO_TICKS(10));

    // Step 4: reinit GT911 with the fresh bus handle
    s_gt911_cfg.i2c_bus = s_bus;
    err = gt911_init(&s_gt911_cfg, s_touch_ptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gt911_init failed: 0x%x", err);
    } else {
        //ESP_LOGE(TAG, "post_update: GT911 reinitialised OK");
    }
    
	err = i2c_master_bus_add_device(s_bus, &dev_cfg, s_button_dev);
    if (err != ESP_OK) {
    	ESP_LOGE(TAG, "pca update err");
	} else {
    	ESP_LOGE(TAG, "update ok");
	}

    touch_bus_unlock();
}