#pragma once
#include <stdint.h>
#include "esp_log.h"
#include "esp_err.h"
#include <stdio.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OPUS_COUNT  8
/* ─── NVS string array dimensions ─────────────────────────────────────────── */
#define NVS_PAGES   16
#define NVS_ENTRIES 8
#define NVS_ATTRS   6
#define NVS_STR_MAX 128   /* max length of each stored string (including '\0') */
#define NVS_NAMESPACE "str_array"


typedef struct {
    const char *label;          // Human-readable, e.g. "Aeschines, Against Timarchus"
    const char *work_id;        // e.g. "tlg0026.tlg001"
    const char *urn_grc;        // full URN base (no reference), e.g.
                                // "urn:cts:greekLit:tlg0026.tlg001.perseus-grc2"
    const char *urn_eng;        // English URN base (may be NULL)
    const char *bookmark1;
    const char *bookmark2;
} OpusEntry;

extern const OpusEntry g_opus_catalogue[OPUS_COUNT];

// ── Live state ───────────────────────────────────────────────────────────────
// URN reference component only, e.g. "5.40"
#define URN_REF_MAX  64

typedef struct {
	int			shelf;
    int         opus_index;             // index into g_opus_catalogue
    char        urn_ref[URN_REF_MAX];   // e.g. "5.40"
    int         page;                   // 0-based page within this passage
} PageState;

//void nvs_key_from_index(int page, int entry, int attr, char *key, size_t key_sz);
esp_err_t nvs_array_set(int page, int entry, int attr, const char *value);
esp_err_t nvs_array_get(int page, int entry, int attr, char *buf, size_t buf_sz);

// Initialise state from NVS (or defaults if not found)
int page_state_init(PageState *s, int opus = 0, bool load_last = false);

// Persist state to NVS
void page_state_save(const PageState *s);

int page_state_build_urn(const PageState *s, bool greek, char *out, int out_size);

#ifdef __cplusplus
}
#endif
