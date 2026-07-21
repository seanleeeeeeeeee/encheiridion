#ifndef GT911_H
#define GT911_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pin Definitions */
#define GT911_RST_PIN       GPIO_NUM_9
#define GT911_INT_PIN       GPIO_NUM_3
#define GT911_SCL_PIN       GPIO_NUM_40
#define GT911_SDA_PIN       GPIO_NUM_39

/* I2C Configuration */
#define GT911_I2C_PORT      I2C_NUM_0
#define GT911_I2C_FREQ_HZ   400000
#define GT911_I2C_ADDR_1    0x5D    /* INT pulled HIGH during reset */
#define GT911_I2C_ADDR_2    0x14    /* INT pulled LOW during reset  */
#define GT911_I2C_ADDR      GT911_I2C_ADDR_1

/* GT911 Register Addresses */
#define GT911_REG_CMD           0x8040
#define GT911_REG_CONFIG        0x8047
#define GT911_REG_CONFIG_VER    0x8047
#define GT911_REG_X_OUTPUT_MAX  0x8048
#define GT911_REG_Y_OUTPUT_MAX  0x804A
#define GT911_REG_TOUCH_NUM     0x804C
#define GT911_REG_MODULE_SWITCH 0x804D
#define GT911_REG_PRODUCT_ID    0x8140
#define GT911_REG_FIRMWARE_VER  0x8144
#define GT911_REG_X_RESOLUTION  0x8146
#define GT911_REG_Y_RESOLUTION  0x8148
#define GT911_REG_VENDOR_ID     0x814A
#define GT911_REG_STATUS        0x814E
#define GT911_REG_POINT_1       0x814F
#define GT911_REG_CONFIG_CHKSUM 0x80FF
#define GT911_REG_CONFIG_FRESH  0x8100

/* GT911 Status Flags */
#define GT911_STATUS_BUFFER_READY   0x80
#define GT911_STATUS_LARGE_DETECT   0x40
#define GT911_STATUS_PROXIMITY      0x20
#define GT911_STATUS_HAVE_KEY       0x10
#define GT911_STATUS_POINT_MASK     0x0F

/* Maximum touch points */
#define GT911_MAX_TOUCH_POINTS  5

/* Touch point data size in bytes */
#define GT911_POINT_DATA_SIZE   8

/**
 * @brief Touch point structure
 */
typedef struct {
    uint8_t  id;        /*!< Touch point ID */
    uint16_t x;         /*!< X coordinate   */
    uint16_t y;         /*!< Y coordinate   */
    uint16_t size;      /*!< Touch area     */
    uint8_t  reserved;  /*!< Reserved byte  */
} gt911_touch_point_t;

/**
 * @brief Touch data structure
 */
typedef struct {
    uint8_t             count;                          /*!< Number of active touch points */
    bool                large_detect;                   /*!< Large touch detected           */
    gt911_touch_point_t points[GT911_MAX_TOUCH_POINTS]; /*!< Touch point data               */
} gt911_touch_data_t;

/**
 * @brief GT911 device handle
 */
typedef struct gt911_dev_t *gt911_handle_t;

/**
 * @brief Touch event callback type
 */
typedef void (*gt911_touch_cb_t)(gt911_touch_data_t *touch_data, void *user_data);

/**
 * @brief GT911 configuration structure
 */
typedef struct {
    i2c_master_bus_handle_t i2c_bus;    /*!< I2C master bus handle           */
    uint8_t                 i2c_addr;   /*!< I2C device address               */
    uint16_t                x_max;      /*!< Maximum X resolution             */
    uint16_t                y_max;      /*!< Maximum Y resolution             */
    bool                    swap_xy;    /*!< Swap X and Y coordinates         */
    bool                    invert_x;   /*!< Invert X coordinate              */
    bool                    invert_y;   /*!< Invert Y coordinate              */
    gt911_touch_cb_t        touch_cb;   /*!< Touch event callback (optional)  */
    void                   *user_data;  /*!< User data for callback           */
} gt911_config_t;

/* Default configuration macro */
#define GT911_DEFAULT_CONFIG() {            \
    .i2c_bus  = NULL,                       \
    .i2c_addr = GT911_I2C_ADDR,            \
    .x_max    = 480,                        \
    .y_max    = 272,                        \
    .swap_xy  = false,                      \
    .invert_x = false,                      \
    .invert_y = false,                      \
    .touch_cb  = NULL,                      \
    .user_data = NULL,                      \
}

/* ── Public API ─────────────────────────────────────────────────────────── */

/**
 * @brief  Initialise I2C bus, GPIO, and the GT911 device.
 *
 * @param[in]  config  Pointer to configuration structure.
 * @param[out] handle  Pointer to store the created device handle.
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if arguments are invalid
 *   - ESP_ERR_NO_MEM if memory allocation failed
 *   - other esp_err_t codes from underlying drivers
 */
esp_err_t gt911_init(const gt911_config_t *config, gt911_handle_t *handle);

/**
 * @brief  Read the current touch state from the GT911.
 *
 * @param[in]  handle      Device handle.
 * @param[out] touch_data  Pointer to touch data structure to fill.
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if arguments are invalid
 *   - ESP_ERR_INVALID_STATE if no new data is available
 */
esp_err_t gt911_read_touch(gt911_handle_t handle, gt911_touch_data_t *touch_data);

/**
 * @brief  Read product ID string from GT911.
 *
 * @param[in]  handle   Device handle.
 * @param[out] buf      Buffer to store product ID (minimum 5 bytes).
 * @param[in]  buf_len  Buffer length.
 * @return ESP_OK on success.
 */
esp_err_t gt911_get_product_id(gt911_handle_t handle, char *buf, size_t buf_len);

/**
 * @brief  Read firmware version from GT911.
 *
 * @param[in]  handle  Device handle.
 * @param[out] ver     Pointer to store firmware version.
 * @return ESP_OK on success.
 */
esp_err_t gt911_get_firmware_ver(gt911_handle_t handle, uint16_t *ver);

/**
 * @brief  Send a soft-reset command to the GT911.
 *
 * @param[in] handle  Device handle.
 * @return ESP_OK on success.
 */
esp_err_t gt911_soft_reset(gt911_handle_t handle);

/**
 * @brief  Enable or disable the interrupt-driven touch task.
 *
 * @param[in] handle  Device handle.
 * @param[in] enable  true to enable, false to disable.
 * @return ESP_OK on success.
 */
esp_err_t gt911_enable_interrupt(gt911_handle_t handle, bool enable);

/**
 * @brief  Deinitialise and free all resources held by the driver.
 *
 * @param[in] handle  Device handle.
 * @return ESP_OK on success.
 */
esp_err_t gt911_deinit(gt911_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* GT911_H */
