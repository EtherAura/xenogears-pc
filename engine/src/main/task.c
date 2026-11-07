/*
 * Task lists: allocation, linking, update/draw/delete callbacks, owner/child
 * queries.
 *
 * Provisional file 0x8001C8DC-0x8001D298 (main executable); bounds come from
 * compiler/assembler changes and may move. Built with GCC 2.7.2-cdk.
 */
#include "common.h"
#include "main/memory.h"
#include "main/task.h"
#include "main/sprite.h"

TaskHeader* spriteCallbacksPreviousEntry; /* entry whose callback runs (Noah: spriteCallbacksPreviousEntry) */
extern s32 spritesCallback2Delay;
extern s16 spriteCallback2Var4; /* bytecode sizes (Noah: sizePerBytecodeTable) */
void registerSpriteCallback2(TaskHeader* owner, TaskHeader* task);
void allocateSavePointMeshDataSub0(TaskHeader* owner, TaskHeader* task);
void setTaskDrawFunction(TaskHeader* task, void (*callback)(TaskHeader*));
u32 registerSpriteCallback2Counter2 = 0; /* next task id (Noah: registerSpriteCallback2Counter2) */
s32 allocateSavePointMeshDataSub0_var1 = 0; /* tasks in list 2 (allocateSavePointMeshDataSub0_var1) */
s32 registerSpriteCallback2Counter = 0; /* tasks in list 1 (registerSpriteCallback2Counter) */
TaskHeader* spriteCallback2Var2; /* list 2 head (spriteCallback2Var2) */
TaskHeader* spriteCallbacksCurrentEntry; /* entry being iterated (spriteCallbacksCurrentEntry) */
TaskHeader* spriteCallbackHead; /* list 1 head (spriteCallbackHead) */
extern u8 spriteBytecode2ExtendedE0_Var0; /* spriteBytecode2ExtendedE0_Var0 */
extern u8 battleScriptLoaderTaskCreationInProgress; /* heap selector passed to the allocator */
extern s32 allocateSavePointMeshDataSub0_var0;
void registerSpriteCallback2Sub0(TaskHeader* task);
void freeDrawTask(TaskHeader* task);
void allocateSavePointMeshDataSub0_callback(TaskHeader* task);
void freeUpdateTask(TaskHeader* task);

/* ==== task.c (0x8001C8DC..) ==== */


/* Delete every task in both lists (calls each delete callback until the lists are empty). */
void deleteAllTasks(void) {
    TaskHeader* t;
    while ((t = spriteCallback2Var2) != NULL) {
        t->mC_deleteCallback(t);
    }
    while ((t = spriteCallbackHead) != NULL) {
        t->mC_deleteCallback(t);
    }
}

void resetSpriteCallbacks(void) {
    spriteCallback2Var2 = NULL;
    spriteCallbackHead = NULL;
    allocateSavePointMeshDataSub0_var1 = 0;
    registerSpriteCallback2Counter = 0;
    spritesCallback2Delay = 0;
}

/* Noah: execSpritesCallbacksList2 (list 2 = spriteCallback2Var2, delayed by spritesCallback2Delay) */
void execSpritesCallbacksList2(void) {
    if (spritesCallback2Delay != 0) {
        if (--spritesCallback2Delay == 0) {
            spriteCallback2Var4 = 0;
        }
    } else {
        spriteCallbacksCurrentEntry = spriteCallback2Var2;
        while (spriteCallbacksCurrentEntry != NULL) {
            TaskHeader* t = spriteCallbacksCurrentEntry;
            TaskHeader* next = t->m18_pNext;
            void (*cb)(TaskHeader*) = t->m8_updateCallback;
            spriteCallbacksPreviousEntry = t;
            spriteCallbacksCurrentEntry = next;
            if (cb != NULL) {
                cb(t);
            }
        }
    }
}

/* Noah: execSpritesCallbacksList1 (list 1 = spriteCallbackHead) */
void execSpritesCallbacksList1(void) {
    spriteCallbacksCurrentEntry = spriteCallbackHead;
    while (spriteCallbacksCurrentEntry != NULL) {
        TaskHeader* t = spriteCallbacksCurrentEntry;
        TaskHeader* next = t->m18_pNext;
        void (*cb)(TaskHeader*) = t->m8_updateCallback;
        spriteCallbacksPreviousEntry = t;
        spriteCallbacksCurrentEntry = next;
        if (cb != NULL) {
            cb(t);
        }
    }
}

void registerSpriteCallback2(TaskHeader* owner, TaskHeader* task) {
    task->m0_owner = owner;
    task->m18_pNext = spriteCallbackHead;
    spriteCallbackHead = task;
    task->m14_ownerId.m0_id = owner->m10_id.m0_id;
    task->m14_ownerId.m29 = 0;
    task->m14_ownerId.m30 = 0;
    task->m14_ownerId.m31 = 0;
    task->m10_id.m0_id = registerSpriteCallback2Counter2++;
    task->m8_updateCallback = 0;
    task->mC_deleteCallback = registerSpriteCallback2Sub0;
    registerSpriteCallback2Counter++;
}

TaskHeader* allocateDrawTask(TaskHeader* owner, s32 size) {
    TaskHeader* task = allocateHeap(size + sizeof(TaskHeader), battleScriptLoaderTaskCreationInProgress);
    registerSpriteCallback2(owner, task);
    task->mC_deleteCallback = freeDrawTask;
    return task;
}

void registerSpriteCallback2Sub0(TaskHeader* task) {
    TaskHeader* cur = spriteCallbackHead;
    TaskHeader* prev = NULL;

    while (cur != NULL) {
        if (cur == task) {
            if (prev != NULL) {
                prev->m18_pNext = cur->m18_pNext;
            } else {
                spriteCallbackHead = cur->m18_pNext;
            }
            if (spriteCallbacksCurrentEntry == task) {
                spriteCallbacksCurrentEntry = task->m18_pNext;
            }
            break;
        }
        prev = cur;
        cur = cur->m18_pNext;
    }
    if (cur == NULL) {
        registerSpriteCallback2Counter++;
    }
    registerSpriteCallback2Counter--;
}

void freeDrawTask(TaskHeader* task) {
    registerSpriteCallback2Sub0(task);
    freeHeap(task);
}

void allocateSavePointMeshDataSub0(TaskHeader* owner, TaskHeader* task) {
    TaskHeader* t = task;

    task->m0_owner = owner;
    /* The battle passes no owner: the PS1 reads low RAM, zero there. */
    task->m14_ownerId.m0_id = owner != NULL ? owner->m10_id.m0_id : 0;
    task->m14_ownerId.m29 = 0;
    task->m14_ownerId.m30 = 0;
    task->m14_ownerId.m31 = 0;
    task->mC_deleteCallback = allocateSavePointMeshDataSub0_callback;
    task->m8_updateCallback = 0;
    task->m18_pNext = spriteCallback2Var2;
    spriteCallback2Var2 = task;
    task->m10_id.m0_id = registerSpriteCallback2Counter2++;
    if (spriteBytecode2ExtendedE0_Var0) {
        allocateSavePointMeshDataSub0_var0++;
        t->m14_ownerId.m31 = 1;
    } else {
        t->m14_ownerId.m31 = 0;
    }
    allocateSavePointMeshDataSub0_var1++;
}

TaskHeader* allocateUpdateTask(TaskHeader* owner, s32 size) {
    TaskHeader* task = allocateHeap(size + sizeof(TaskHeader), battleScriptLoaderTaskCreationInProgress);
    allocateSavePointMeshDataSub0(owner, task);
    task->mC_deleteCallback = freeUpdateTask;
    task->m4_data = 0;
    return task;
}

void setTaskDrawFunction(TaskHeader* task, void (*callback)(TaskHeader*)) {
    task->m8_updateCallback = callback;
}

void setTaskUpdateFunction(TaskHeader* task, void (*callback)(TaskHeader*)) {
    task->m8_updateCallback = callback;
}

void setTaskDeleteFunction(TaskHeader* task, void (*callback)(TaskHeader*)) {
    task->mC_deleteCallback = callback;
}

void* getTaskUpdateFunction(TaskHeader* task) {
    return task->m8_updateCallback;
}

void* getTaskDeleteFunction(TaskHeader* task) {
    return task->mC_deleteCallback;
}

void allocateSavePointMeshDataSub0_callback(TaskHeader* task) {
    TaskHeader* cur = spriteCallback2Var2;
    TaskHeader* prev = NULL;

    while (cur != NULL) {
        if (cur == task) {
            if (prev != NULL) {
                prev->m18_pNext = task->m18_pNext;
            } else {
                spriteCallback2Var2 = task->m18_pNext;
            }
            if (spriteCallbacksCurrentEntry == task) {
                spriteCallbacksCurrentEntry = task->m18_pNext;
            }
            break;
        }
        prev = cur;
        cur = cur->m18_pNext;
    }
    if (task->m14_ownerId.m31) {
        allocateSavePointMeshDataSub0_var0--;
    }
    allocateSavePointMeshDataSub0_var1--;
}

void freeUpdateTask(TaskHeader* task) {
    allocateSavePointMeshDataSub0_callback(task);
    freeHeap(task);
}

/* ==== 0x8001CE74-0x8001D2B0 (cdk; currently an asm segment) ==== */


/* Noah: registerSpriteCallback2_2 - delete all child tasks of `task` from both lists. */
void registerSpriteCallback2_2(TaskHeader* task) {
    TaskHeader* cur;
    TaskHeader* prev;

    prev = NULL;
    for (cur = spriteCallbackHead; cur != NULL; cur = cur->m18_pNext) {
        if (cur->m0_owner == task && !cur->m14_ownerId.m30 && cur->m14_ownerId.m0_id == task->m10_id.m0_id) {
            if (prev != NULL) {
                prev->m18_pNext = cur->m18_pNext;
            } else {
                spriteCallbackHead = cur->m18_pNext;
            }
            if (spriteCallbacksCurrentEntry == cur) {
                spriteCallbacksCurrentEntry = cur->m18_pNext;
            }
            if (cur->mC_deleteCallback != NULL) {
                cur->mC_deleteCallback(cur);
            }
        } else {
            prev = cur;
        }
    }
    prev = NULL;
    for (cur = spriteCallback2Var2; cur != NULL; cur = cur->m18_pNext) {
        if (cur->m0_owner == task && !cur->m14_ownerId.m30 && cur->m14_ownerId.m0_id == task->m10_id.m0_id) {
            if (prev != NULL) {
                prev->m18_pNext = cur->m18_pNext;
            } else {
                spriteCallback2Var2 = cur->m18_pNext;
            }
            if (spriteCallbacksCurrentEntry == cur) {
                spriteCallbacksCurrentEntry = cur->m18_pNext;
            }
            if (cur->mC_deleteCallback != NULL) {
                cur->mC_deleteCallback(cur);
            }
        } else {
            prev = cur;
        }
    }
}

/* Clear m70 of the sprite actors owned by children of `task` flagged m29. */
void detachChildSpriteActors(TaskHeader* task) {
    TaskHeader* cur;
    for (cur = spriteCallback2Var2; cur != NULL; cur = cur->m18_pNext) {
        if (cur->m0_owner == task && cur->m14_ownerId.m0_id == task->m10_id.m0_id && cur->m14_ownerId.m29) {
            ((SpriteActorCore*)cur->m4_data)->m70_parent = NULL;
        }
    }
}

/* Noah: findMatchingGraphicEntity - child of `owner` in list 2 with the given update callback. */
TaskHeader* findMatchingGraphicEntity(TaskHeader* owner, void (*callback)(TaskHeader*)) {
    TaskHeader* cur;
    for (cur = spriteCallback2Var2; cur != NULL; cur = cur->m18_pNext) {
        if (cur->m0_owner == owner && cur->m14_ownerId.m0_id == owner->m10_id.m0_id && cur->m8_updateCallback == callback) {
            return cur;
        }
    }
    return NULL;
}

/* First child of `owner` in list 2. */
TaskHeader* findFirstChildTask(TaskHeader* owner) {
    TaskHeader* cur;
    for (cur = spriteCallback2Var2; cur != NULL; cur = cur->m18_pNext) {
        if (cur->m0_owner == owner && cur->m14_ownerId.m0_id == owner->m10_id.m0_id) {
            return cur;
        }
    }
    return NULL;
}

/* First task in list 2 with the given update callback. */
TaskHeader* findTaskByCallback(void (*callback)(TaskHeader*)) {
    TaskHeader* cur;
    for (cur = spriteCallback2Var2; cur != NULL; cur = cur->m18_pNext) {
        if (cur->m8_updateCallback == callback) {
            return cur;
        }
    }
    return NULL;
}

/* Default delete callback of a task pair created by allocateTaskPair. */
void freeTaskPair(TaskHeader* task) {
    registerSpriteCallback2Sub0(task + 1);
    allocateSavePointMeshDataSub0_callback(task);
    freeHeap(task);
}

/* Allocate a task pair (list 2 + list 1) with draw/update/delete callbacks. */
TaskHeader* allocateTaskPair(s32 size, TaskHeader* owner, void (*update)(TaskHeader*), void (*draw)(TaskHeader*), void (*del)(TaskHeader*)) {
    TaskHeader* task = allocateHeap(size, battleScriptLoaderTaskCreationInProgress);
    allocateSavePointMeshDataSub0(owner, task);
    registerSpriteCallback2(task, task + 1);
    setTaskUpdateFunction(task, update);
    setTaskDrawFunction(task + 1, draw);
    if (del != NULL) {
        setTaskDeleteFunction(task, del);
    } else {
        setTaskDeleteFunction(task, freeTaskPair);
    }
    task->m4_data = task;
    task[1].m4_data = task;
    return task;
}
