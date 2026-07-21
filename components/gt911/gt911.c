#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"
#include "gt911.h"

/* ── Private constants ──────────────────────────────────────────────────── */

static const char *TAG = "GT911";

#define GT911_I2C_TIMEOUT_MS        100
#define GT911_RESET_DELAY_MS        10
#define GT911_STARTUP_DELAY_MS      1000
#define GT911_INT_TASK_STACK        4096
#define GT911_INT_TASK_PRIORITY     5

/* ── Private types ──────────────────────────────────────────────────────── */

/**
 * @brief Internal device structure
 */
struct gt911_dev_t {
    i2c_master_dev_handle_t i2c_dev;
    gt911_config_t          config;
    SemaphoreHandle_t       touch_sem;
    SemaphoreHandle_t       i2c_mutex;      /* ← add this */
    TaskHandle_t            int_task_handle;
    bool                    int_enabled;
};

/* ── Forward declarations ───────────────────────────────────────────────── */

static esp_err_t gt911_write_reg(gt911_handle_t handle,
                                 uint16_t reg,
                                 const uint8_t *data,
                                 size_t len);

static esp_err_t gt911_read_reg(gt911_handle_t handle,
                                uint16_t reg,
                                uint8_t *data,
                                size_t len);

static esp_err_t gt911_clear_status(gt911_handle_t handle);
static void      gt911_isr_handler(void *arg);
static void      gt911_int_task(void *arg);
static esp_err_t gt911_gpio_init(gt911_handle_t handle);
static void      gt911_gpio_deinit(void);
static esp_err_t gt911_hw_reset(uint8_t i2c_addr);

/* ── Register helpers ───────────────────────────────────────────────────── */

/**
 * @brief Write one or more bytes to a 16-bit GT911 register address.
 */
static esp_err_t gt911_write_reg(gt911_handle_t handle,
                                 uint16_t reg,
                                 const uint8_t *data,
                                 size_t len)
{
    uint8_t *buf = malloc(2 + len);
    if (!buf) return ESP_ERR_NO_MEM;

    buf[0] = (reg >> 8) & 0xFF;
    buf[1] =  reg       & 0xFF;
    memcpy(&buf[2], data, len);

    xSemaphoreTake(handle->i2c_mutex, portMAX_DELAY);
    esp_err_t ret = i2c_master_transmit(handle->i2c_dev,
                                        buf, 2 + len,
                                        GT911_I2C_TIMEOUT_MS);
    xSemaphoreGive(handle->i2c_mutex);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write reg 0x%04X failed: %s", reg, esp_err_to_name(ret));
    }

    free(buf);
    return ret;
}

static esp_err_t gt911_read_reg(gt911_handle_t handle,
                                uint16_t reg,
                                uint8_t *data,
                                size_t len)
{
    uint8_t reg_buf[2] = {
        (reg >> 8) & 0xFF,
         reg       & 0xFF
    };

    //xSemaphoreTake(handle->i2c_mutex, portMAX_DELAY);
    esp_err_t ret = i2c_master_transmit_receive(handle->i2c_dev,
                                                reg_buf, sizeof(reg_buf),
                                                data, len,
                                                GT911_I2C_TIMEOUT_MS);
    //xSemaphoreGive(handle->i2c_mutex);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C read reg 0x%04X failed: %s", reg, esp_err_to_name(ret));
    }

    return ret;
}

/**
 * @brief Clear the status register (tell GT911 we have read the data).
 */
static esp_err_t gt911_clear_status(gt911_handle_t handle)
{
    uint8_t zero = 0x00;
    return gt911_write_reg(handle, GT911_REG_STATUS, &zero, 1);
}

/* ── GPIO / reset ───────────────────────────────────────────────────────── */

/**
 * @brief Perform hardware reset sequence.
 *
 * The I2C address is selected by the logic level on the INT pin during
 * the reset de-assertion window:
 *   - INT HIGH → address 0x5D
 *   - INT LOW  → address 0x14
 */
static esp_err_t gt911_hw_reset(uint8_t i2c_addr)
{
    //ESP_LOGI(TAG, "Starting HW reset sequence, target addr=0x%02X", i2c_addr);

    /* Drive both pins low */
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << GT911_RST_PIN) | (1ULL << GT911_INT_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&out_cfg), TAG, "gpio_config (output) failed");

    gpio_set_level(GT911_RST_PIN, 0);
    gpio_set_level(GT911_INT_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    /*
     * Address selection:
     *   INT = 0  → 0x5D
     *   INT = 1  → 0x14
     */
    gpio_set_level(GT911_INT_PIN,
                   (i2c_addr == GT911_I2C_ADDR_1) ? 0 : 1);
    vTaskDelay(pdMS_TO_TICKS(1));

    /* Release reset */
    gpio_set_level(GT911_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    /*
     * ── Critical: pulse INT low→high→float to signal end of
     *    address-selection and enter normal operating mode.
     *    Without this the GT911 NACKs all subsequent I2C traffic.
     */
    gpio_set_level(GT911_INT_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(GT911_INT_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Now release INT as a floating input */
    gpio_config_t in_cfg = {
        .pin_bit_mask = (1ULL << GT911_INT_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&in_cfg), TAG, "gpio_config (input) failed");

    /* Wait for GT911 to complete internal startup */
    vTaskDelay(pdMS_TO_TICKS(200));

    //ESP_LOGI(TAG, "HW reset complete");
    return ESP_OK;
}

/**
 * @brief Configure GPIO for RST and INT pins.
 */
static esp_err_t gt911_gpio_init(gt911_handle_t handle)
{
    /* RST pin – output, start high */
    gpio_config_t rst_cfg = {
        .pin_bit_mask = (1ULL << GT911_RST_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&rst_cfg), TAG, "RST gpio_config failed");
    gpio_set_level(GT911_RST_PIN, 1);

    /* INT pin – input with falling-edge interrupt */
    gpio_config_t int_cfg = {
        .pin_bit_mask = (1ULL << GT911_INT_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&int_cfg), TAG, "INT gpio_config failed");

    /* Install ISR service (safe to call if already installed) */
    //gpio_install_isr_service(0);
    ESP_RETURN_ON_ERROR(
        gpio_isr_handler_add(GT911_INT_PIN, gt911_isr_handler, handle),
        TAG, "Failed to add ISR handler");

    return ESP_OK;
}

/**
 * @brief Remove GPIO ISR and reset pin configurations.
 */
static void gt911_gpio_deinit(void)
{
    gpio_isr_handler_remove(GT911_INT_PIN);
    gpio_reset_pin(GT911_INT_PIN);
    gpio_reset_pin(GT911_RST_PIN);
}

/* ── Interrupt handling ─────────────────────────────────────────────────── */

/**
 * @brief GPIO ISR – fires on falling edge of INT pin.
 *        Posts to the semaphore so the task can read touch data.
 */
static void IRAM_ATTR gt911_isr_handler(void *arg)
{
    gt911_handle_t handle = (gt911_handle_t)arg;
    BaseType_t higher_prio_woken = pdFALSE;
    xSemaphoreGiveFromISR(handle->touch_sem, &higher_prio_woken);
    portYIELD_FROM_ISR(higher_prio_woken);
}

/**
 * @brief Background task that waits for the touch semaphore and calls
 *        the user-supplied callback.
 */
static void gt911_int_task(void *arg)
{
    gt911_handle_t     handle = (gt911_handle_t)arg;
    gt911_touch_data_t touch_data;
    uint8_t            consecutive_errors = 0;

    ESP_LOGI(TAG, "Interrupt task started");

    while (handle->int_enabled) {

        bool irq_fired = (xSemaphoreTake(handle->touch_sem,
                                          pdMS_TO_TICKS(20)) == pdTRUE);
        if (!handle->int_enabled) break;

        if (irq_fired) {
            ESP_LOGD(TAG, "IRQ fired, INT pin level=%d",
                     gpio_get_level(GT911_INT_PIN));
        }

        esp_err_t ret = gt911_read_touch(handle, &touch_data);

        if (ret == ESP_ERR_NOT_FOUND) {
            /* No new data ready — perfectly normal when polling */
            consecutive_errors = 0;
            continue;
        }

        if (ret != ESP_OK) {
            consecutive_errors++;
            ESP_LOGW(TAG, "read_touch error: %s, consecutive=%d",
                     esp_err_to_name(ret), consecutive_errors);

            if (consecutive_errors >= 5) {
                ESP_LOGE(TAG, "Too many errors, recovering...");
                vTaskDelay(pdMS_TO_TICKS(200));
                gt911_clear_status(handle);
                consecutive_errors = 0;
            }
            continue;
        }

        consecutive_errors = 0;

        if (touch_data.count > 0 && handle->config.touch_cb) {
            handle->config.touch_cb(&touch_data, handle->config.user_data);
        }
    }

    handle->int_task_handle = NULL;
    vTaskDelete(NULL);
}

/* ── Public API ─────────────────────────────────────────────────────────── */

esp_err_t gt911_init(const gt911_config_t *config, gt911_handle_t *out_handle)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(config && out_handle, ESP_ERR_INVALID_ARG,
                        TAG, "Invalid arguments");
    ESP_RETURN_ON_FALSE(config->i2c_bus, ESP_ERR_INVALID_ARG,
                        TAG, "i2c_bus must not be NULL");

    struct gt911_dev_t *dev = calloc(1, sizeof(struct gt911_dev_t));
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_NO_MEM, TAG, "No memory for device");

    memcpy(&dev->config, config, sizeof(gt911_config_t));

    /* ── Create I2C mutex before any register access ── */
    dev->i2c_mutex = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(dev->i2c_mutex, ESP_ERR_NO_MEM,
                      err_free, TAG, "Failed to create I2C mutex");

    /* ── 1. Hardware reset ── */
    ESP_GOTO_ON_ERROR(
        gt911_hw_reset(config->i2c_addr),
        err_mutex, TAG, "HW reset failed");

    /* ── 2. Register I2C device ── */
    i2c_device_config_t i2c_dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = config->i2c_addr,
        .scl_speed_hz    = GT911_I2C_FREQ_HZ,
    };
    ESP_GOTO_ON_ERROR(
        i2c_master_bus_add_device(config->i2c_bus, &i2c_dev_cfg, &dev->i2c_dev),
        err_mutex, TAG, "Failed to add I2C device");

    /* ── 3. Probe both addresses if needed ── */
    char product_id[5] = {0};
    ret = gt911_get_product_id(dev, product_id, sizeof(product_id));

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No response at 0x%02X, trying alternate address...",
                 config->i2c_addr);

        i2c_master_bus_rm_device(dev->i2c_dev);
        dev->i2c_dev = NULL;

        uint8_t alt_addr = (config->i2c_addr == GT911_I2C_ADDR_1)
                           ? GT911_I2C_ADDR_2
                           : GT911_I2C_ADDR_1;

        dev->config.i2c_addr       = alt_addr;
        i2c_dev_cfg.device_address = alt_addr;

        ESP_GOTO_ON_ERROR(
            gt911_hw_reset(alt_addr),
            err_mutex, TAG, "HW reset (alt addr) failed");

        ESP_GOTO_ON_ERROR(
            i2c_master_bus_add_device(config->i2c_bus, &i2c_dev_cfg, &dev->i2c_dev),
            err_mutex, TAG, "Failed to add I2C device (alt addr)");

        ESP_GOTO_ON_ERROR(
            gt911_get_product_id(dev, product_id, sizeof(product_id)),
            err_i2c, TAG, "Failed to read product ID on both addresses");
    }

    //ESP_LOGI(TAG, "GT911 found at 0x%02X, Product ID: %s",
      //       dev->config.i2c_addr, product_id);

    uint16_t fw_ver = 0;
    ESP_GOTO_ON_ERROR(
        gt911_get_firmware_ver(dev, &fw_ver),
        err_i2c, TAG, "Failed to read firmware version");
    //ESP_LOGI(TAG, "Firmware version: 0x%04X", fw_ver);


    gt911_clear_status(dev);

    /* ── 5. GPIO and semaphore ── */
    dev->touch_sem = xSemaphoreCreateBinary();
    ESP_GOTO_ON_FALSE(dev->touch_sem, ESP_ERR_NO_MEM,
                      err_i2c, TAG, "Failed to create semaphore");

    ESP_GOTO_ON_ERROR(
        gt911_gpio_init(dev),
        err_sem, TAG, "GPIO init failed");

    /* ── 6. Start task ── */
    if (config->touch_cb) {
        dev->int_enabled = true;
        BaseType_t task_ret = xTaskCreate(
            gt911_int_task,
            "gt911_int",
            GT911_INT_TASK_STACK,
            dev,
            GT911_INT_TASK_PRIORITY,
            &dev->int_task_handle);

        if (task_ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create interrupt task");
            goto err_gpio;
        }
    }

    *out_handle = dev;
    //ESP_LOGI(TAG, "GT911 initialised successfully");
    return ESP_OK;

err_gpio:
    gt911_gpio_deinit();
err_sem:
    vSemaphoreDelete(dev->touch_sem);
err_i2c:
    i2c_master_bus_rm_device(dev->i2c_dev);
err_mutex:
    vSemaphoreDelete(dev->i2c_mutex);
err_free:
    free(dev);
    return ESP_FAIL;
}
/* ─────────────────────────────────────────────────────────────────────────── */

esp_err_t gt911_read_touch(gt911_handle_t handle, gt911_touch_data_t *touch_data)
{
    ESP_RETURN_ON_FALSE(handle && touch_data, ESP_ERR_INVALID_ARG,
                        TAG, "Invalid arguments");

    memset(touch_data, 0, sizeof(gt911_touch_data_t));

    /* Read status register */
    uint8_t status = 0;
    esp_err_t ret = gt911_read_reg(handle, GT911_REG_STATUS, &status, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read status register: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Buffer-ready bit not set — no new data, not an error */
    if (!(status & GT911_STATUS_BUFFER_READY)) {
        return ESP_ERR_NOT_FOUND;   /* distinct from I2C errors */
    }

    touch_data->large_detect = (status & GT911_STATUS_LARGE_DETECT) != 0;
    touch_data->count        =  status & GT911_STATUS_POINT_MASK;

    if (touch_data->count > GT911_MAX_TOUCH_POINTS) {
        touch_data->count = GT911_MAX_TOUCH_POINTS;
    }

    for (uint8_t i = 0; i < touch_data->count; i++) {
        uint16_t point_reg = GT911_REG_POINT_1 + (i * GT911_POINT_DATA_SIZE);
        uint8_t  raw[GT911_POINT_DATA_SIZE] = {0};

        ret = gt911_read_reg(handle, point_reg, raw, GT911_POINT_DATA_SIZE);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read touch point %d: %s",
                     i, esp_err_to_name(ret));
            return ret;
        }

        gt911_touch_point_t *p = &touch_data->points[i];
        p->id   = raw[0];
        p->x    = (uint16_t)(raw[1]) | ((uint16_t)(raw[2]) << 8);
        p->y    = (uint16_t)(raw[3]) | ((uint16_t)(raw[4]) << 8);
        p->size = (uint16_t)(raw[5]) | ((uint16_t)(raw[6]) << 8);

        if (handle->config.swap_xy) {
            uint16_t tmp = p->x; p->x = p->y; p->y = tmp;
        }
        if (handle->config.invert_x) p->x = handle->config.x_max - p->x;
        if (handle->config.invert_y) p->y = handle->config.y_max - p->y;
    }

    /* Clear status so GT911 can report the next event */
    ret = gt911_clear_status(handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to clear status: %s", esp_err_to_name(ret));
    }

    return ESP_OK;
}

/* ─────────────────────────────────────────────────────────────────────────── */

esp_err_t gt911_get_product_id(gt911_handle_t handle, char *buf, size_t buf_len)
{
    ESP_RETURN_ON_FALSE(handle && buf && buf_len >= 5, ESP_ERR_INVALID_ARG,
                        TAG, "Invalid arguments");

    uint8_t raw[4] = {0};
    ESP_RETURN_ON_ERROR(
        gt911_read_reg(handle, GT911_REG_PRODUCT_ID, raw, sizeof(raw)),
        TAG, "Failed to read product ID");

    snprintf(buf, buf_len, "%c%c%c%c", raw[0], raw[1], raw[2], raw[3]);
    return ESP_OK;
}

/* ─────────────────────────────────────────────────────────────────────────── */

esp_err_t gt911_get_firmware_ver(gt911_handle_t handle, uint16_t *ver)
{
    ESP_RETURN_ON_FALSE(handle && ver, ESP_ERR_INVALID_ARG,
                        TAG, "Invalid arguments");

    uint8_t raw[2] = {0};
    ESP_RETURN_ON_ERROR(
        gt911_read_reg(handle, GT911_REG_FIRMWARE_VER, raw, sizeof(raw)),
        TAG, "Failed to read firmware version");

    *ver = (uint16_t)(raw[0]) | ((uint16_t)(raw[1]) << 8);
    return ESP_OK;
}

/* ─────────────────────────────────────────────────────────────────────────── */

esp_err_t gt911_soft_reset(gt911_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");

    uint8_t cmd = 0x02;
    return gt911_write_reg(handle, GT911_REG_CMD, &cmd, 1);
}

/* ─────────────────────────────────────────────────────────────────────────── */

esp_err_t gt911_enable_interrupt(gt911_handle_t handle, bool enable)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");

    if (enable) {
        gpio_intr_enable(GT911_INT_PIN);
    } else {
        gpio_intr_disable(GT911_INT_PIN);
    }

    return ESP_OK;
}

/* ─────────────────────────────────────────────────────────────────────────── */

esp_err_t gt911_deinit(gt911_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid handle");

    handle->int_enabled = false;
    if (handle->int_task_handle) {
        xSemaphoreGive(handle->touch_sem);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    gt911_gpio_deinit();

    if (handle->touch_sem)  vSemaphoreDelete(handle->touch_sem);
    if (handle->i2c_mutex)  vSemaphoreDelete(handle->i2c_mutex);  /* ← add */
    if (handle->i2c_dev)    i2c_master_bus_rm_device(handle->i2c_dev);

    free(handle);
    ESP_LOGI(TAG, "GT911 deinitialised");
    return ESP_OK;
}
