#pragma once
#include "apps/navigator.h"
#include "reader/epub_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_SPINE 64

typedef struct {
	char idref[128];
	char href[128];
} SpineEntry;
typedef struct {
	SpineEntry entries[64];
	size_t count;
	char opf_dir[128];
	char book_dir[128];
} SpineIndex;

typedef struct {
    char     spine_idref[128];   // which chapter (spine idref)
    int32_t  char_offset;       // offset into that chapter's plain text
} book_position_t;

typedef struct {
    char        *text;          // plain text for this section
    size_t       text_len;
    text_span_t *spans;         // heading/normal spans
    size_t       span_count;
    bool         has_next;      // false if this is the last spine item
    bool         has_prev;      // false if this is the first spine item
} section_result_t;
typedef struct {
    char idref[128];
    char href[128];
} spine_entry_t;

typedef struct {
    spine_entry_t entries[MAX_SPINE];
    size_t        count;
    char          opf_dir[128];
    char          book_dir[128];
    char book_title[128];
} spine_index_t;
extern spine_index_t *spine;

const NavigatorInterface *navigator_r(void);

#ifdef __cplusplus
}
#endif