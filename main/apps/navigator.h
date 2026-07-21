#pragma once
#include "scaife/app/page_state.h"
#include "passage_data.h"   // extract PassageData into its own header
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;  // e.g. "scaife" or "local"

    bool (*load)(PageState *s, PassageData *pd);
    int (*search)(PageState *s);
/*
    bool (*get_prevnext)(const PageState *s,
                         char *out_prev, int prev_size,
                         char *out_next, int next_size);
*/
    int (*build_urn)(const PageState *s, bool greek,
                      char *out, int out_size);
    
    bool (*go_prev_page)(PageState *s);
    bool (*go_next_page)(PageState *s, int max_page);
} NavigatorInterface;

#ifdef __cplusplus
}
#endif
