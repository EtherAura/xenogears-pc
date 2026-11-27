#ifndef PORT_SYSTEM_START_H
#define PORT_SYSTEM_START_H

/* One isolated field start, applied after the disc's initial game state is
 * loaded. Variable indices are u16 slots in m1930_fieldVarsBackup; story,
 * entrance and disc are specified separately. No retail state blob is kept. */
#define PORT_START_VARIABLES 8
typedef struct { int index, value; } PortStartVariable;
typedef struct {
    int field, entry, progress, disc;
    int party[3]; /* IDs 0..10; slot 0 is required, slots 1/2 may be 255 (empty) */
    int variableCount;
    PortStartVariable variables[PORT_START_VARIABLES];
    /* The music the story has playing as the field is entered, 1 + its id; 0
     * none. A field whose script plays no music keeps the one before it, which
     * a start has not got (xg-xukn: field 13 plays the Lahan theme, id 0). */
    int music;
} PortStartRequest;

#ifdef __cplusplus
extern "C" {
#endif
int portRequestStart(const PortStartRequest* request);
void portRequestSoftReset(void);
/* Remains active across the replay's field changes, until the next reset. */
int portSceneReplayActive(void);
#ifdef __cplusplus
}
#endif
#endif
