#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

#include "driver/i2c_master.h"


#define I2C_MASTER_SCL_IO    GPIO_NUM_40
#define I2C_MASTER_SDA_IO    GPIO_NUM_39
#define I2C_MASTER_FREQ_HZ   150000
#define I2C_MASTER_NUM       I2C_NUM_0
#define PCA9535_ADDR         0x20

// PCA9535 Register Map
#define PCA9535_INPUT_PORT0   0x00
#define PCA9535_INPUT_PORT1   0x01
#define PCA9535_OUTPUT_PORT0  0x02
#define PCA9535_OUTPUT_PORT1  0x03
#define PCA9535_CONFIG_PORT0  0x06
#define PCA9535_CONFIG_PORT1  0x07

// BUTTON is on IO1_2
#define BUTTON_BIT           (1 << 2)
#define BOOT_BUTTON_PIN		GPIO_NUM_0
#define IO48_BUTTON_PIN     GPIO_NUM_0

#define SHORT_PRESS_MS   500
#define LONG_PRESS_MS   2000

static const char* TTAG = "buttons";

static i2c_master_bus_config_t conf = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
    	.glitch_ignore_cnt = 7,
    	.flags = {
    		.enable_internal_pullup = true,
    	}
};
QueueHandle_t gpio_event_queue = NULL;

static i2c_master_bus_handle_t i2c_bus = NULL;
static i2c_master_dev_handle_t s_pca9535_dev = NULL;

typedef enum {
    PRESS_SHORT,
    PRESS_LONG,
    PRESS_VERY_LONG,
} press_type_t;

typedef struct {
    uint32_t    gpio_num;
    int64_t     press_time_us;
    bool        is_pressed;
} button_state_t;


static button_state_t btn_power = { .gpio_num = BOOT_BUTTON_PIN  };
static button_state_t btn_io48  = { .gpio_num = IO48_BUTTON_PIN  };

static void IRAM_ATTR button_isr_handler(void *arg)
{
	ESP_LOGI(TTAG, "gpio handler start");
    button_state_t *btn = (button_state_t *)arg;
    uint32_t gpio_num   = btn->gpio_num;

    if (gpio_get_level((gpio_num_t)gpio_num) == 0) {
        // Falling edge - button pressed
        btn->press_time_us = esp_timer_get_time();
        btn->is_pressed    = true;
        ESP_LOGI(TTAG, "press");
    } else {
    	ESP_LOGI(TTAG, "release");
        // Rising edge - button released
        if (btn->is_pressed) {
            int64_t duration_ms = 
                (esp_timer_get_time() - btn->press_time_us) / 1000;
            btn->is_pressed = false;
            xQueueSendFromISR(gpio_event_queue, &gpio_num, NULL);
        }
    }
}



press_type_t get_press_type(button_state_t *btn)
{
    int64_t duration_ms = 
        (esp_timer_get_time() - btn->press_time_us) / 1000;

    if (duration_ms >= LONG_PRESS_MS) return PRESS_VERY_LONG;
    if (duration_ms >= SHORT_PRESS_MS) return PRESS_LONG;
    return PRESS_SHORT;
}

esp_err_t i2c_master_init(void) {
    esp_err_t err = i2c_new_master_bus(&conf, &i2c_bus);
    if (err != ESP_OK) {
    	ESP_LOGI(TTAG, "i2c init %s", esp_err_to_name(err));
        return err;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCA9535_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    err = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &s_pca9535_dev);
    if (err != ESP_OK) {
    	ESP_LOGI(TTAG, "ic2 init2");
        i2c_del_master_bus(i2c_bus);
        i2c_bus = NULL;
        return err;
    }
    return ESP_OK;
}


esp_err_t pca9535_write_reg(uint8_t reg, uint8_t value)
{
    if (s_pca9535_dev == NULL) {
    	ESP_LOGI(TTAG, "pca9535 write err");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(s_pca9535_dev, buf, sizeof(buf), 100);
}

esp_err_t pca9535_read_reg(uint8_t reg, uint8_t *value)
{
    if (value == NULL) {
    	ESP_LOGI(TTAG, "pca9535 read err1");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_pca9535_dev == NULL) {
    	ESP_LOGI(TTAG, "pca9535 read err2");
        return ESP_ERR_INVALID_STATE;
    }

    return i2c_master_transmit_receive(s_pca9535_dev, &reg, 1, value, 1, 100);
}

void pca9535_init(void)
{
    // Configure Port1:
    // IO1_7=IN, IO1_6=IN, IO1_5=OUT, IO1_4=OUT
    // IO1_3=OUT, IO1_2=IN(BUTTON), IO1_1=OUT, IO1_0=OUT
    // 1=input, 0=output
    uint8_t port1_config = 0b11000100;  // IO1_7, IO1_6, IO1_2 as inputs

    if (i2c_master_init() != ESP_OK) {
    		esp_intr_dump(stdout);
    	ESP_LOGI(TTAG, "pca9535 init");
        return;
    }

    (void)pca9535_write_reg(PCA9535_CONFIG_PORT0, 0xFF); // Port0 all input (unused)
    (void)pca9535_write_reg(PCA9535_CONFIG_PORT1, port1_config);
}

bool pca9535_get_button(void)
{
    uint8_t port1_value = 0xFF;
	esp_err_t err = pca9535_read_reg(PCA9535_INPUT_PORT1, &port1_value);
    if (err != ESP_OK) {
    	ESP_LOGI(TTAG, "button read err");
        return false;
    }

    // assuming active LOW
    return !(port1_value & BUTTON_BIT);
}

void button_init(void)
{
	gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE 
    };
    gpio_config(&io_conf);

    gpio_event_queue = xQueueCreate(10, sizeof(uint32_t));
    if (gpio_event_queue == NULL){
    	ESP_LOGI(TTAG, "queue!");
    }
    if (gpio_install_isr_service(0) != ESP_OK) {
    	ESP_LOGI(TTAG, "isr!");
    }

	if (gpio_isr_handler_add(BOOT_BUTTON_PIN, button_isr_handler, &btn_power) != ESP_OK) {
    	ESP_LOGI(TTAG, "pwr button!");
    }
    
    ESP_LOGI(TTAG, "done init");
}
