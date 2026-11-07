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
} PortStartRequest;

#ifdef __cplusplus
extern "C" {
#endif
int portRequestStart(const PortStartRequest* request);
/* Remains active across the replay's field changes, until the next reset. */
int portSceneReplayActive(void);
#ifdef __cplusplus
}
#endif
#endif
