#include "font_manager.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>

#include "fonts/futura16.h"
#include "fonts/futura20.h"
#include "fonts/orpheus16.h"
#include "fonts/orpheus_gk_basic.h"
#include "fonts/orpheus_gk_ext.h"

static const char *TAG = "font_manager";

uint8_t *g_futura16_ram     = NULL;
uint8_t *g_futura20_ram     = NULL;
uint8_t *g_orpheus16_ram    = NULL;
uint8_t *g_orpheus_gk_ram   = NULL;
uint8_t *g_orpheus_ext_ram  = NULL;

bool fonts_to_ram(void)
{
    struct FontCopy {
        const uint8_t *src;
        uint8_t      **dst;
        size_t         size;
        const char    *name;
    } table[] = {
        { futura16,         &g_futura16_ram,    3982  + 64, "futura16"        },
        { futura20,         &g_futura20_ram,    3298  + 64, "futura20"        },
        { orpheus16,        &g_orpheus16_ram,   3974  + 64, "orpheus16"       },
        { orpheus_gk_basic, &g_orpheus_gk_ram,  5199  + 64, "orpheus_gk_basic"},
        { orpheus_gk_ext,   &g_orpheus_ext_ram, 17121 + 64, "orpheus_gk_ext"  },
    };

    for (auto &f : table) {
        // Try internal RAM first, fall back to PSRAM if not enough
        *f.dst = (uint8_t *)heap_caps_malloc(f.size, 
                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        
        if (!*f.dst) {
            ESP_LOGW(TAG, "internal RAM failed for %s, trying PSRAM...", f.name);
            *f.dst = (uint8_t *)heap_caps_malloc(f.size, MALLOC_CAP_SPIRAM);
        }
        
        if (!*f.dst) {
            ESP_LOGE(TAG, "font RAM alloc failed: %s (%u bytes)",
                     f.name, (unsigned)f.size);
            return false;
        }
        memcpy(*f.dst, f.src, f.size - 64);
        memset(*f.dst + f.size - 64, 0, 64);
        ESP_LOGI(TAG, "font %s → RAM %p (%u bytes)",
                 f.name, *f.dst, (unsigned)f.size);
    }
    return true;
}
