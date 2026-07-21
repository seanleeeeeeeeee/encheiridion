#include "xml_parser.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "xml_parser";

int xml_find_passage_text(const char *xml, char *out, int out_size)
{
    // Find <text> ... </text> block, then strip all XML tags
    const char *start = strstr(xml, "<text>");
    if (!start) {
        // Try <body> as fallback
        start = strstr(xml, "<body>");
        if (!start) { ESP_LOGE(TAG, "no <text> or <body>"); return -1; }
    }

    const char *end = strstr(start, "</text>");
    if (!end) end = strstr(start, "</body>");
    if (!end) end = xml + strlen(xml);

    int   out_len = 0;
    bool  in_tag  = false;
    bool  last_sp = false;

    for (const char *p = start; p < end && out_len < out_size - 1; p++) {
        if (*p == '<') { in_tag = true; continue; }
        if (*p == '>') { in_tag = false; last_sp = false; continue; }
        if (in_tag) continue;

        char c = *p;
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';

        if (c == ' ') {
            if (last_sp) continue;
            last_sp = true;
        } else {
            last_sp = false;
        }
        out[out_len++] = c;
    }

    // Trim trailing space
    while (out_len > 0 && out[out_len - 1] == ' ') out_len--;
    out[out_len] = '\0';
    return (out_len > 0) ? 0 : -1;
}

/* ── JSON helpers ─────────────────────────────────────────────────────────── */

// Find the value of a JSON string field: "key": "value"
// Returns pointer to start of value (inside json), sets *len to value length.
// Returns NULL if not found.
// static const char *json_find_string_field(const char *json, const char *key, int *len)
// {
//     char pat[128];
//     snprintf(pat, sizeof(pat), "\"%s\": \"", key);   // |
//     const char *p = strstr(json, pat);               // "key":{
//     if (!p) {
//         // Try without space: "key":"
//         snprintf(pat, sizeof(pat), "\"%s\":\"", key);
//         p = strstr(json, pat);
//         if (!p) return NULL;
//     }
//     char *r = strstr(json, "{"); r++;
//     int nesting = 0;
//     while ((p = strstr(p, pat)) != NULL) {
// 		while (r < p) {
// 			if (*r == '{' || *r == '[' || *r == '(') {
// 				nesting++;
// 			} else if (*r == '}' || *r == ']' || *r == ')') {
// 				nesting--;
// 			}
// 			r++;
// 		}
// 		p += strlen(pat);
// 		if (nesting == 0) break;
// 	}
//     const char *q = p;
//     while (*q && !(*q == '"' && *(q-1) != '\\')) q++;
//     if (!*q) return NULL;
// 
//     *len = (int)(q - p);
//     return p;
// }
// 
// bool json_find_label(const char *json, char *out, int out_size)
// {   const char *p = strstr(json, "\"urn\": \"") + 8;
// 	const char *q = strchr(p, '\"');
// 	p = strrchr(q - 1, ':');
// 	int len = q - p;
// 	memcpy(urn, p + 1, len);
// 	p = strstr(p, "\"label\": \"") + 10;	//"label": "_"
// 	p = strstr(p, "\"label\": \"") + 10;	//_1234567890
// 	q = strchr(p--, '\"');
// 	len = q - p;
// 	memcpy(label, p + 1, len);
// 	const char *text_obj = strstr(json, "\"text\": {");
//     if (!text_obj) text_obj = strstr(json, "\"text\":{");
//     if (!text_obj) {
//         ESP_LOGE(TAG, "json_find_label: no 'text' object");
//         return false;
//     }
// 
//     int len = 0;
//     const char *val = json_find_string_field(text_obj, "label", &len);
//     if (!val) {
//         ESP_LOGE(TAG, "json_find_label: no 'label' field in text object");
//         return false;
//     }
// 
//     if (len >= out_size) len = out_size - 1;
//     memcpy(out, val, len);
//     out[len] = '\0';
//     return true;
// }

/* ── Link header storage ──────────────────────────────────────────────────── */
// The HTTP Link header contains prev/next URNs.
// scaife_client.c stores the raw Link header value here via
// json_store_link_header() after each JSON fetch.

static char s_link_header[512] = {0};

void json_store_link_header(const char *value)
{
    strncpy(s_link_header, value, sizeof(s_link_header) - 1);
    s_link_header[sizeof(s_link_header) - 1] = '\0';
    ESP_LOGI(TAG, "Link header: %s", s_link_header);
}

// Parse one direction from a Link header.
// Link header format (multiple entries separated by ", "):
//   </reader/urn:cts:...:2/>; rel="next"; urn="urn:cts:...:2"
// We extract the urn= field for the given rel value.
static bool parse_link_direction(const char *link,
                                  const char *rel,
                                  char       *out_ref,
                                  int         out_size)
{
    // Find rel="<rel>" in the header
    char rel_pat[32];
    snprintf(rel_pat, sizeof(rel_pat), "rel=\"%s\"", rel);

    const char *entry = link;
    while (entry && *entry) {
        // Find the next entry (separated by ", <")
        const char *comma = strstr(entry, ", <");
        const char *entry_end = comma ? comma : entry + strlen(entry);

        // Check if this entry has the rel we want
        // Search only within this entry
        char segment[256];
        int seg_len = (int)(entry_end - entry);
        if (seg_len >= (int)sizeof(segment)) seg_len = sizeof(segment) - 1;
        memcpy(segment, entry, seg_len);
        segment[seg_len] = '\0';

        if (strstr(segment, rel_pat)) {
            // Found the right entry — extract urn="..."
            const char *urn_start = strstr(segment, "urn=\"");
            if (urn_start) {
                urn_start += 5; // skip urn="
                const char *urn_end = strchr(urn_start, '"');
                if (urn_end) {
                    // Extract just the reference (last colon component)
                    // e.g. "urn:cts:greekLit:tlg0061.tlg001.perseus-eng1:2" → "2"
                    const char *ref = strrchr(urn_start, ':');
                    if (ref && ref < urn_end) {
                        ref++; // skip the colon
                        int ref_len = (int)(urn_end - ref);
                        if (ref_len >= out_size) ref_len = out_size - 1;
                        memcpy(out_ref, ref, ref_len);
                        out_ref[ref_len] = '\0';
                        return true;
                    }
                }
            }
        }

        if (!comma) break;
        entry = comma + 2; // skip ", "
        // Skip leading "<"
        while (*entry == ' ') entry++;
    }

    out_ref[0] = '\0';
    return false;
}

void json_get_prevnext_refs(char *out_prev, int prev_size,
                             char *out_next, int next_size)
{
    if (out_prev && prev_size > 0) {
        if (!parse_link_direction(s_link_header, "prev", out_prev, prev_size))
            out_prev[0] = '\0';
    }
    if (out_next && next_size > 0) {
        if (!parse_link_direction(s_link_header, "next", out_next, next_size))
            out_next[0] = '\0';
    }
    ESP_LOGI(TAG, "prevnext: prev='%s' next='%s'",
             out_prev ? out_prev : "(null)",
             out_next ? out_next : "(null)");
}