#ifndef MAIN_TASK_H
#define MAIN_TASK_H

/* Kernel task lists (Noah: kernel/taskLists.cpp). */

#include "common.h"

/* Task id word (TaskHeader m10/m14): the code writes these as bitfields. */
typedef struct {
    u32 m0_id : 29;
    u32 m29 : 1;
    u32 m30 : 1;
    u32 m31 : 1; /* m14: task was created while spriteBytecode2ExtendedE0_Var0 was set */
} TaskId;

/* Noah: sTaskHeader. */
typedef struct TaskHeader {
    /* 0x00 */ struct TaskHeader* m0_owner;
    /* 0x04 */ void* m4_data;
    /* 0x08 */ void (*m8_updateCallback)(struct TaskHeader*);
    /* 0x0C */ void (*mC_deleteCallback)(struct TaskHeader*);
    /* 0x10 */ TaskId m10_id;
    /* 0x14 */ TaskId m14_ownerId;
    /* 0x18 */ struct TaskHeader* m18_pNext;
} TaskHeader;

void setTaskDrawFunction(TaskHeader* task, void (*callback)(TaskHeader*));
void setTaskUpdateFunction(TaskHeader* task, void (*callback)(TaskHeader*));
void setTaskDeleteFunction(TaskHeader* task, void (*callback)(TaskHeader*));

#endif /* MAIN_TASK_H */
