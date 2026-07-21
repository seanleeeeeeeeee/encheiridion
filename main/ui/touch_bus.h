#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/i2c_master.h"
#include "gt911.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif
extern SemaphoreHandle_t s_mutex;
extern bool flag_of_pausing;
static i2c_master_bus_config_t  s_bus_cfg;
static gt911_config_t           s_gt911_cfg;
static i2c_master_bus_handle_t  s_bus;
static gt911_handle_t          *s_touch_ptr;
static i2c_master_dev_handle_t *s_button_dev;
// Call once after the first gt911_init succeeds.
void touch_bus_init(const i2c_master_bus_config_t *bus_cfg,
                    const gt911_config_t          *gt911_cfg,
                    i2c_master_bus_handle_t        bus,
                    gt911_handle_t                *touch_ptr,
                    i2c_master_dev_handle_t		  *pca9535_dev);

// Acquire exclusive access to the I2C bus.
// Call before every gt911_read_touch.
void touch_bus_lock(void);

// Release exclusive access.
// Call after every gt911_read_touch.
void touch_bus_unlock(void);

// Call before epaper->fullUpdate():
//   - acquires the mutex
//   - calls gt911_deinit (removes I2C device, frees handle)
void touch_bus_pre_update(void);

// Call after epaper->fullUpdate():
//   - calls gt911_init (HW reset + fresh I2C device)
//   - releases the mutex
void touch_bus_post_update(void);

#ifdef __cplusplus
}
#endif
