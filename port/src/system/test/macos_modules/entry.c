#include "module.h"

/* Both objects in each module contribute the same tentative definition. */
int common[2];

View ENTRY(void) {
    return (View){privateData(), common, privateBss(), PEER};
}
