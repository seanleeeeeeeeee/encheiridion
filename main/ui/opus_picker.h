#pragma once
#include "scaife/app/page_state.h"
#include "column.h"



// Draw an 8-entry dropdown over the current screen.
// Returns the selected index (0-7), or -1 if cancelled
// (touch outside the list).
// Blocks until a touch is received.
int opus_picker_run(PageState *current);
int chapter_picker_run(int page);
int urn_picker(bool csv, const TextLine *lines, int count, int page);

