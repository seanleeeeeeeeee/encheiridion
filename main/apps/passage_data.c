#include "passage_data.h"
#include <stdlib.h>

void passage_data_free(PassageData *pd) {
    if (!pd) return;
    free(pd->text_grc);  pd->text_grc = NULL;
    free(pd->text_eng);  pd->text_eng = NULL;
    free(pd->label);	 pd->label = NULL;
}
