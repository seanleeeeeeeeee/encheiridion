#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *text_grc;
    char *text_eng;
    char *label;
    int   n_lines_grc;
    int   n_lines_eng;
} PassageData;

void passage_data_free(PassageData *pd);

#ifdef __cplusplus
}
#endif
