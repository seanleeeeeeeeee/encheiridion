#pragma once
#include "apps/navigator.h"

#ifdef __cplusplus
extern "C" {
#endif

// Returns a pointer to the static Scaife navigator vtable
const NavigatorInterface *navigator_scaife(void);

#ifdef __cplusplus
}
#endif

/*#pragma once
#include "page_state.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque passage data
typedef struct {
    char *text_grc;     // heap-allocated, caller must free
    char *text_eng;     // heap-allocated, caller must free
    char *label;        // heap-allocated, caller must free
    int   n_lines_grc;
    int   n_lines_eng;
} PassageData;

// Free the contents of a PassageData (not the struct itself)
void passage_data_free(PassageData *pd);

// Build a full URN string:
//   out must be at least 128 bytes
void page_state_build_urn(const PageState *s, bool greek, char *out, int out_size);

// Fetch + wrap the passage described by *s.
// Returns true on success.  pd->text_* are PSRAM-allocated.
bool navigator_load(const PageState *s, PassageData *pd);

// Query Scaife for prev/next URN reference strings.
// out_prev / out_next: caller-supplied buffers of at least URN_REF_MAX bytes.
// Either pointer may be NULL if that direction is not needed.
// Returns true if the request succeeded (even if one direction is absent).
bool navigator_get_prevnext(const PageState *s,
                             char *out_prev, int prev_size,
                             char *out_next, int next_size);

#ifdef __cplusplus
}
#endif
*/