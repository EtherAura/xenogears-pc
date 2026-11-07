#ifndef PORT_EXTRAS_FIELD_LOCATIONS_H
#define PORT_EXTRAS_FIELD_LOCATIONS_H

namespace extras {
struct FieldLocation {
    const char* area;
    const char* room;
};
// Original descriptive labels for the physical places represented by fields.
// Story variants of a room share a label. Numeric source ids remain separate.
const FieldLocation& fieldLocation(int field);
}

#endif
