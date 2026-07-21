#pragma once
#include <stdint.h>
#include <string.h>

/*
 * greek_encode.h
 * 
 * Utilities for working with UTF-8 encoded polytonic Greek strings.
 *
 * FastEPD's BBF font format stores glyphs by Unicode codepoint.
 * drawString() needs to walk UTF-8 bytes and look up each codepoint.
 * 
 * These utilities let you:
 *   1. Decode UTF-8 strings codepoint by codepoint
 *   2. Check if a codepoint is Greek
 *   3. Split a mixed Latin/Greek string into segments
 *      so you can swap fonts mid-line
 */

/* ── UTF-8 decoder ──────────────────────────────────────────────────────────
 * Reads one Unicode codepoint from a UTF-8 string.
 * Advances *pp to the next character.
 * Returns 0 at end of string.
 */
static inline uint32_t utf8_next(const char **pp)
{
    const uint8_t *p = (const uint8_t *)*pp;
    uint32_t uc = 0;

    if (*p == 0) return 0;

    if (*p < 0x80) {
        /* 1-byte ASCII */
        uc = *p++;
    } else if ((*p & 0xE0) == 0xC0) {
        /* 2-byte sequence: U+0080 – U+07FF */
        uc = (*p++ & 0x1F) << 6;
        uc |= (*p++ & 0x3F);
    } else if ((*p & 0xF0) == 0xE0) {
        /* 3-byte sequence: U+0800 – U+FFFF (covers all Greek) */
        uc = (*p++ & 0x0F) << 12;
        uc |= (*p++ & 0x3F) << 6;
        uc |= (*p++ & 0x3F);
    } else if ((*p & 0xF8) == 0xF0) {
        /* 4-byte sequence: U+10000 – U+10FFFF */
        uc = (*p++ & 0x07) << 18;
        uc |= (*p++ & 0x3F) << 12;
        uc |= (*p++ & 0x3F) << 6;
        uc |= (*p++ & 0x3F);
    } else {
        p++; /* invalid byte, skip */
    }

    *pp = (const char *)p;
    return uc;
}

/* ── Codepoint range checks ─────────────────────────────────────────────── */

static inline int is_basic_latin(uint32_t uc)
{
    return (uc >= 0x0020 && uc <= 0x007E);
}

static inline int is_greek(uint32_t uc)
{
    return (
        (uc >= 0x0370 && uc <= 0x03FF) ||  /* Greek and Coptic          */
        (uc >= 0x1F00 && uc <= 0x1FFF)     /* Greek Extended (polytonic)*/
    );
}

static inline int is_latin_extended(uint32_t uc)
{
    return (uc >= 0x0080 && uc <= 0x036F);
}

/* ── Segment types ──────────────────────────────────────────────────────── */

typedef enum {
    SEG_LATIN = 0,
    SEG_GREEK = 1,
    SEG_OTHER = 2
} seg_type_t;

typedef struct {
    const char *start;      /* pointer into original UTF-8 string */
    int         byte_len;   /* length in bytes (not codepoints)   */
    seg_type_t  type;
} str_segment_t;

#define MAX_SEGMENTS 32

/* ── String segmenter ───────────────────────────────────────────────────────
 * Splits a UTF-8 string into Latin/Greek segments so you can
 * call setFont() before drawing each segment.
 *
 * Returns number of segments found.
 *
 * Example:
 *   str_segment_t segs[MAX_SEGMENTS];
 *   int n = utf8_segment("Hello τοῦτον world", segs, MAX_SEGMENTS);
 *   // segs[0] = {Latin, "Hello "}
 *   // segs[1] = {Greek, "τοῦτον"}
 *   // segs[2] = {Latin, " world"}
 */
static inline int utf8_segment(const char *str,
                                str_segment_t *segs,
                                int max_segs)
{
    const char *p = str;
    int n = 0;

    while (*p && n < max_segs) {
        const char *seg_start = p;
        const char *lookahead = p;
        uint32_t first_uc = utf8_next(&lookahead);

        seg_type_t type;
        if (is_greek(first_uc))       type = SEG_GREEK;
        else if (is_basic_latin(first_uc) ||
                 is_latin_extended(first_uc)) type = SEG_LATIN;
        else                           type = SEG_OTHER;

        /* consume chars of the same type */
        p = lookahead;
        while (*p) {
            const char *next = p;
            uint32_t uc = utf8_next(&next);
            seg_type_t t;
            if (is_greek(uc))          t = SEG_GREEK;
            else if (is_basic_latin(uc) ||
                     is_latin_extended(uc)) t = SEG_LATIN;
            else                        t = SEG_OTHER;

            if (t != type) break;
            p = next;
        }

        segs[n].start    = seg_start;
        segs[n].byte_len = (int)(p - seg_start);
        segs[n].type     = type;
        n++;
    }

    return n;
}

/* ── Measure string width in codepoints (not bytes) ────────────────────── */
static inline int utf8_codepoint_len(const char *str)
{
    int count = 0;
    const char *p = str;
    while (utf8_next(&p)) count++;
    return count;
}
