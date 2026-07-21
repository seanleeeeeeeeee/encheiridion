#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── XML ───────────────────────────────────────────────────────────────────────
int  xml_find_passage_text(const char *xml, char *out, int out_size);

// ── JSON ──────────────────────────────────────────────────────────────────────

// Called by scaife_client when the HTTP Link header is received.
void json_store_link_header(const char *value);

// Extract text.label from passage JSON.
// bool json_find_label(const char *json, char *out, int out_size);

// Extract prev/next reference strings from the stored Link header.
// Call after scaife_get_passage_json() returns.
// out_prev / out_next: at least URN_REF_MAX bytes. Either may be NULL.
void json_get_prevnext_refs(char *out_prev, int prev_size,
                             char *out_next, int next_size);

#ifdef __cplusplus
}
#endif