#include "module.h"
#include "payload.inc"

int common[2];
static int scratch;
static Data state = {BEFORE, &common[1], AFTER, SEED};

Data *privateData(void) {
    return &state;
}

int *privateBss(void) {
    return &scratch;
}
