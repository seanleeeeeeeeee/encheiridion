#pragma once
#include "header.h"
#ifdef __cplusplus
extern "C" {
#endif

void epd_full_update(void);
void epd_part_update(BB_RECT *rect);
#ifdef __cplusplus
}
#endif
