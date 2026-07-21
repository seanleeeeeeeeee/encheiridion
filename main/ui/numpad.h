#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Show a numpad overlay.
// initial_ref: current URN reference string, e.g. "5.40"
// out_ref:     buffer to write result into (URN_REF_MAX bytes)
// Returns true if the user confirmed (right-arrow), false if cancelled.
int numpad_run(const char *initial_ref, char *out_ref, int out_size);
int keyboard(const char *initial_ref, char *out_ref, int out_size);

#ifdef __cplusplus
}
#endif
