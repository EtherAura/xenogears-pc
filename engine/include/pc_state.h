#ifndef PC_STATE_H
#define PC_STATE_H

/*
 * What a canonical save state needs to know from the C beyond its types
 * (tools/state_schema.py, tools/statecanon.py): what each heap block holds,
 * and where the PC keeps a pointer in memory that has no C type. A state
 * written by one build loads in another because every pointer is saved by
 * what it points at; these say where the pointers are. (The Enhanced engine's
 * copy: the PC side of include/pc_state.h.)
 *
 * PC_HEAP_OF(T, size): the next allocateHeap holds T, as many as fit, or one
 *   T whose last member is a counted array (PC_STATE_COUNTED). size is the
 *   request as before (PC_HEAP_NATIVE(n) and so on). T is a type's name, a
 *   pointer type (Node*), or PC_PRIMS (GPU primitives: each a tag linking it
 *   into an ordering table, then its words). The block's header keeps the
 *   name's hash (HeapBlock mC_pcType); the schema maps it back, from these
 *   macros in the source.
 *
 * PC_HEAP_ONE(T, size): one T, then bytes (and the ranges recorded in them):
 *   a T with a tail of the caller's.
 *
 * PC_HEAP_NAMED(name, size): the same with the type chosen at run time, name
 *   from a table of PC_STATE_NAME(T) (a type's name, checked, as a constant).
 *
 * PC_STATE_AT(at, T, n): n T at `at`, in memory that has no C type (a disc
 *   file's bytes, where the PC keeps a native pointer) or after the T of a
 *   PC_HEAP_OF block (a converted model's tables). Forgotten when its heap
 *   block is freed; recording one again replaces it.
 *
 * PC_STATE_COUNTED(T, array, count, extra), next to T's definition (no
 *   semicolon: it brings its own): T's last member `array` holds T's member
 *   `count` plus `extra` elements.
 *
 * PC_STATE_TAIL(T, array), the same way: T's last member `array` fills the
 *   rest of its block (a header and as many entries as were allocated).
 *
 * PC_STATE_UNION(T, member, selector, (view0, view1, ...)), the same way:
 *   T's union `member` holds the view its member `selector` names, view0 for
 *   0, view1 for 1, and so on (other values: every view, the most typed last).
 *
 * The names are written out (no macros as T), one use per line: the schema
 * reads them from the source.
 */

/*
 * The state rules (docs: canonical states, 17.2): how a state converts
 * between builds where saved data changed. A slot keeps its writer's schema,
 * and the newer of the two builds' schemas carries every rule between them,
 * so the same rule takes an older state forward and a newer one back. The
 * rules language is fixed: tools/state_schema.py refuses any other PC_STATE_*
 * macro, and every converter since the first canonical release reads these.
 *
 * PC_STATE_REVISION: this source's state revision. A change that needs a rule
 *   raises it by one, and its rules carry the new value as rev ("from builds
 *   of revision rev on"). A rule, once released, stays as it is.
 *
 * History (each with rev; the newer build's apply between two revisions):
 * PC_STATE_WAS(rev, T, member, "old"): before rev, T's member was "old" (a
 *   member name, its key from its offset tag; or a key, "+1c" or "pc:name").
 *   member may be a path through unnamed structs (m8_in.pcLevel).
 * PC_STATE_VAR_WAS(rev, var, "old"): before rev, the variable's identity was
 *   "old": a PC variable's old name, or an identity ("field:800adc04",
 *   "main:pc:name", "pc:system/hw.c:name").
 * PC_STATE_TYPE_WAS(rev, T, "Old"): before rev, the struct T was named Old
 *   (heap blocks are typed by name).
 * PC_STATE_TAG_WAS(rev, "part.expr", "part.old"): a host part's chunk tag
 *   before rev (XG_STATE_VAR's expression).
 * PC_STATE_FN_WAS(rev, fn, "old"): before rev, the function fn was named old
 *   (saved function pointers name their target; a game function is found by
 *   its retail address too, where one function has it).
 * PC_STATE_VALUES(rev, T, member, ((old, new), ...)): from rev, the integer
 *   member (a bitfield, or each element of an integer array) holds new where
 *   it held old. A newer build maps old to new, an older one new to old, the
 *   first pair that matches; values in no pair stay.
 * PC_STATE_VAR_VALUES(rev, var, ((old, new), ...)): the same for a variable.
 * PC_STATE_RESET(rev, T, member), PC_STATE_VAR_RESET(rev, var): what it holds
 *   changed at rev as no rule says: a state from the other side of rev does
 *   not carry it (the reader's default).
 * PC_STATE_REMOVED(rev, T, value, ("old", ...)): T's members removed at rev
 *   (their names or keys); a build before rev gives them value.
 * PC_STATE_VAR_REMOVED(rev, value, ("identity", ...)): variables removed at
 *   rev, by their identities.
 * Properties (no rev; the reading build's own):
 * PC_STATE_DEFAULT(T, member, value), PC_STATE_VAR_DEFAULT(var, value): what
 *   an integer gets when the state does not carry it (else zero).
 * PC_STATE_HASHES(T, member): the member holds heap type hashes
 *   (pcStateTypeId), which a type's rename changes too.
 *
 * Integers are literals (decimal or hex), names and identities strings. Each
 * goes next to what it names, after its definition (no semicolon: it brings
 * its own); PC_STATE_TAG_WAS beside the part's XG_STATE_VAR.
 */
#include "common.h"

/* GPU primitives linked into an ordering table (PC_HEAP_OF(PC_PRIMS, ...)). */
typedef struct PC_PRIMS PC_PRIMS;

/*
 * Safe points (docs: canonical states, P3): places in a mode's main loop,
 * between frames, where everything the loop needs is in memory. A canonical
 * state is saved at one (no stack: it cannot be carried to another build),
 * and a load starts the game again there: bootGame re-arms its context and
 * calls the mode's entry, which skips its setup (pcResumeAt) and enters its
 * loop with the locals it kept in a resume struct.
 */
/* A safe point's id: its mode's index in kernel.c gameModes[] times 256, plus
 * which of the mode's loops (0: its main loop). bootGame's bootMode is 0 while
 * a mode runs, so the id names the mode to take up. */
enum { PC_SAFE_NONE = 0, PC_SAFE_FIELD = 1 << 8, PC_SAFE_BATTLE = 2 << 8, PC_SAFE_BATTLE_TURN = (2 << 8) | 1,
       PC_SAFE_WORLDMAP = 3 << 8, PC_SAFE_BATTLING = 4 << 8 /* the arena's task waiting at a menu or a fight */,
       PC_SAFE_MENU = 5 << 8 /* the menu mode: its menus' loops only */ };
#define PC_SAFE_MODE(id) ((id) >> 8)
/* The menus' loops (the low byte, from 2): the same in every mode that opens them
 * (field, world map, the menu mode itself); pcMenuSafeId adds the mode. */
enum {
    PC_SAFE_SUB_MENU = 2, PC_SAFE_SUB_STATUS, PC_SAFE_SUB_ITEM, PC_SAFE_SUB_EQUIP, PC_SAFE_SUB_GEAR, PC_SAFE_SUB_ETHER,
    PC_SAFE_SUB_SAVE, PC_SAFE_SUB_SOUND, PC_SAFE_SUB_STATUS_EQUIP, PC_SAFE_SUB_STATUS_ETHER, PC_SAFE_SUB_SKILLS,
    PC_SAFE_SUB_ITEM_USE, PC_SAFE_SUB_GEAR_EQUIP, PC_SAFE_SUB_GEAR_ETHER, PC_SAFE_SUB_GEAR_ETHER2,
    /* the shop, the gear shop, name entry and party change (menuToEnter 4, 5, 3, 1) */
    PC_SAFE_SUB_SHOP, PC_SAFE_SUB_SHOP_BUY, PC_SAFE_SUB_SHOP_SELL, PC_SAFE_SUB_SHOP_SELL_EQUIPPED, PC_SAFE_SUB_SHOP_SELL_LIST,
    PC_SAFE_SUB_GEAR_SHOP, PC_SAFE_SUB_GEAR_SHOP_SUB, PC_SAFE_SUB_GEAR_SHOP_BUY, PC_SAFE_SUB_GEAR_SHOP_SELL_LIST,
    PC_SAFE_SUB_NAME_ENTRY, PC_SAFE_SUB_PARTY,
    /* the title's Continue/New Game menu (menuToEnter 2), the disc change's wait for the other disc (6) */
    PC_SAFE_SUB_TITLE, PC_SAFE_SUB_DISC, PC_SAFE_SUB_COUNT
};
/* port/src/game/menu/resume.c: a screen's integer locals at its loop's top, and the
 * screen a shared one (equip, ether) is opened as. pcMenuKeep/pcMenuTake take n
 * (pointer, size) pairs. */
void pcMenuKeep(s32 sub, s32 n, ...);
void pcMenuTake(s32 sub, s32 n, ...);
#define PC_LOCAL(x) (u8*)&(x), (s32)sizeof(x)
void pcMenuChild(s32 sub);
s32 pcMenuChildNow(void);
void pcMenuHost(s32 mode);   /* the mode opening the menu (kernel.c gameModes[] index) */
s32 pcMenuSafeId(s32 sub);   /* a menu loop's safe point in that mode */
s32 pcResumeMenu(void);      /* the menu loop a stackless load takes up (its sub), or 0 */
/* port/src/game/battling/resume.c: the arena task's locals at the yield where it waits, and
 * battlingMain's at its safe point (BattlingScene and BattlingThread: battling.h) */
struct BattlingScene;
struct BattlingThread;
void pcBattlingKeep(s32 site, struct BattlingScene* task, s32 timer, s32 state, s32 hold);
void pcBattlingYielded(void);
s32 pcBattlingResume(struct BattlingScene** task, s32* timer, s32* state, s32* hold);
void pcBattlingSafePoint(struct BattlingThread* thread, u32 prev);
struct BattlingThread* pcBattlingRestart(u32* prev);
void pcSafePoint(s32 where);   /* in the loop: keep `where`, park if a safe save is due */
s32 pcResumeAt(void);          /* an entry: the safe point to take up, or 0 */
void pcResumeDone(void);       /* the entry has taken it up */
void pcResumeGame(void);       /* the game stack's restart entry after a stackless load */

u32 pcStateTypeId(const char* name);
s32 pcHeapRequestType(const char* name, s32 size);
u32 pcHeapTakeType(void);
void pcStateAt(const void* at, const char* name, s32 count);
void pcStateHeapFree(const void* start, s32 bytes);
void pcStateForget(const void* start, const void* end);

#define PC_STATE_NAME(T) (#T + 0 * sizeof(T*))
#define PC_HEAP_OF(T, size) pcHeapRequestType(PC_STATE_NAME(T), (size))
#define PC_HEAP_ONE(T, size) pcHeapRequestType((#T "[1]") + 0 * sizeof(T*), (size))
#define PC_HEAP_NAMED(name, size) pcHeapRequestType((name), (size))
#define PC_STATE_AT(at, T, n) pcStateAt((at), PC_STATE_NAME(T), (n))
#define PC_STATE_COUNTED(T, array, count, extra) \
    _Static_assert(sizeof(((T*)0)->array[0]) > 0 && sizeof(((T*)0)->count) > 0 && (extra) >= 0, \
                   "PC_STATE_COUNTED: " #T);
#define PC_STATE_TAIL(T, array) _Static_assert(sizeof(((T*)0)->array[0]) > 0, "PC_STATE_TAIL: " #T);
#define PC_STATE_UNION(T, member, selector, views) \
    _Static_assert(sizeof(((T*)0)->member) > 0 && sizeof(((T*)0)->selector) > 0, "PC_STATE_UNION: " #T);

/* The state rules (above). A member is checked as any type, a bitfield too: sizeof((void)member, 0). */
#define PC_STATE_REVISION 1
#define PC_STATE_MEMBER(T, member) (sizeof((void)((T*)0)->member, 0) > 0)
#define PC_STATE_VARIABLE(var) (sizeof((void)(var), 0) > 0)
#define PC_STATE_WAS(rev, T, member, old) \
    _Static_assert((rev) > 0 && PC_STATE_MEMBER(T, member) && sizeof(old) > 1, "PC_STATE_WAS: " #T);
#define PC_STATE_VAR_WAS(rev, var, old) \
    _Static_assert((rev) > 0 && PC_STATE_VARIABLE(var) && sizeof(old) > 1, "PC_STATE_VAR_WAS: " #var);
#define PC_STATE_TYPE_WAS(rev, T, old) _Static_assert((rev) > 0 && sizeof(T) > 0 && sizeof(old) > 1, "PC_STATE_TYPE_WAS: " #T);
#define PC_STATE_TAG_WAS(rev, tag, old) _Static_assert((rev) > 0 && sizeof(tag) > 1 && sizeof(old) > 1, "PC_STATE_TAG_WAS");
#define PC_STATE_FN_WAS(rev, fn, old) _Static_assert((rev) > 0 && sizeof(&(fn)) > 0 && sizeof(old) > 1, "PC_STATE_FN_WAS: " #fn);
#define PC_STATE_VALUES(rev, T, member, map) _Static_assert((rev) > 0 && PC_STATE_MEMBER(T, member), "PC_STATE_VALUES: " #T);
#define PC_STATE_VAR_VALUES(rev, var, map) _Static_assert((rev) > 0 && PC_STATE_VARIABLE(var), "PC_STATE_VAR_VALUES: " #var);
#define PC_STATE_RESET(rev, T, member) _Static_assert((rev) > 0 && PC_STATE_MEMBER(T, member), "PC_STATE_RESET: " #T);
#define PC_STATE_VAR_RESET(rev, var) _Static_assert((rev) > 0 && PC_STATE_VARIABLE(var), "PC_STATE_VAR_RESET: " #var);
#define PC_STATE_REMOVED(rev, T, value, names) _Static_assert((rev) > 0 && sizeof(T) > 0, "PC_STATE_REMOVED: " #T);
#define PC_STATE_VAR_REMOVED(rev, value, identities) _Static_assert((rev) > 0, "PC_STATE_VAR_REMOVED");
#define PC_STATE_DEFAULT(T, member, value) _Static_assert(PC_STATE_MEMBER(T, member), "PC_STATE_DEFAULT: " #T);
#define PC_STATE_VAR_DEFAULT(var, value) _Static_assert(PC_STATE_VARIABLE(var), "PC_STATE_VAR_DEFAULT: " #var);
#define PC_STATE_HASHES(T, member) _Static_assert(PC_STATE_MEMBER(T, member), "PC_STATE_HASHES: " #T);

#endif
