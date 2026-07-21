#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t *g_futura16_ram;
extern uint8_t *g_futura20_ram;
extern uint8_t *g_orpheus16_ram;
extern uint8_t *g_orpheus_gk_ram;
extern uint8_t *g_orpheus_ext_ram;

bool fonts_to_ram(void);

#ifdef __cplusplus
}
#endif
