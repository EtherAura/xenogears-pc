#ifndef PORT_SYSTEM_STATESCHEMA_H
#define PORT_SYSTEM_STATESCHEMA_H

/*
 * A build's state schema in C (stateschema.c; docs/CANONICAL_STATES.md §16):
 * the JSON tools/state_schema.py writes beside the executable
 * (state_schema.json), with the indexes tools/statecanon.py's Schema builds
 * over it, built the same way so the C converter decides as the reference
 * does: variables one per address and their identities, functions and their
 * identities, symbols, named constants, heap types, counted arrays and
 * union rules.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { ST_STRUCT, ST_UNION, ST_INT, ST_FLOAT, ST_PTR, ST_ARR, ST_OTAG, ST_ADDR, ST_SCRATCH, ST_UNKNOWN };

#define ST_NONE (-1)   /* no type, variable, ... */
#define ST_NO_RETAIL (-1)

typedef struct {
    const char* name;
    int64_t off;
    int32_t type;    /* a type index, ST_NONE when the schema has no such type */
    int64_t retail;  /* the retail offset, ST_NO_RETAIL for a PC-only field */
    int32_t bits;    /* a bitfield's width (0: not a bitfield) */
    int32_t bit;     /* a bitfield's first bit, from the struct's start */
} StField;

typedef struct {
    const char* tid;  /* its key: a DWARF offset in hex, ".d0" ... for an array's inner dimensions */
    uint8_t kind;     /* ST_* */
    uint8_t isSigned;
    uint8_t fn;       /* ST_PTR: to a function */
    int64_t size;
    int32_t to;       /* ST_PTR: the pointed-at type (ST_NONE: void or unknown) */
    int32_t of;       /* ST_ARR: the element type */
    int64_t n;        /* ST_ARR: the element count */
    const char* name; /* "" when unnamed */
    int32_t fieldCount;
    StField* fields;
} StType;

typedef struct {
    const char* name;
    uint64_t addr;
    int64_t size;
    const char* module;   /* NULL for a platform static */
    const char* section;
    int64_t retail;       /* ST_NO_RETAIL when none */
    const char* platform; /* the platform file, NULL for game data */
    int32_t type;
    const char* id;       /* its identity (statecanon Schema.identity, collisions get ":name") */
    uint8_t isConst;      /* "const": the build's own constant, which no state carries (the reader keeps its own) */
} StVar;

typedef struct {
    const char* name;
    uint64_t addr;
    int64_t retail;
    const char* id; /* "fn:%08x:name" or "fn:name" */
} StFunc;

typedef struct {
    const char* name;
    uint64_t addr;
    uint64_t size;
    char kind; /* nm's letter */
} StSymbol;

typedef struct {
    const char* name;
    uint64_t lo, hi;
} StRange;

enum { ST_SPEC_NONE, ST_SPEC_TYPE, ST_SPEC_PRIMS, ST_SPEC_PTRS, ST_SPEC_OTAGS };

typedef struct {
    uint32_t hash; /* pcStateTypeId of the name */
    const char* name;
    int spec;      /* ST_SPEC_*: what the block or range holds */
    int32_t type;  /* ST_SPEC_TYPE: the type */
    int one;       /* PC_HEAP_ONE: one of type, then bytes */
} StHeapType;

typedef struct {
    int32_t type; /* the struct (its tid's index) */
    const char* name;
    const char* array;
    const char* count; /* NULL: PC_STATE_TAIL (the block's end) */
    int64_t extra;
} StCounted;

typedef struct {
    int32_t type;
    const char* name;
    const char* member;
    const char* selector;
    int32_t viewCount;
    const char** views;
} StUnion;

/* A state rule (include/pc_state.h; docs/CANONICAL_STATES.md 17.2), as the schema keeps it. A rule this reader
 * cannot read (a kind it does not know, a key missing or of another type: statecanon rule_target None) has
 * readable 0 and is ignored by the converter, which counts it; it never refuses a state. */
enum { ST_RULE_WAS, ST_RULE_VALUES, ST_RULE_RESET, ST_RULE_REMOVED, ST_RULE_DEFAULT, ST_RULE_HASHES };
enum { ST_ON_FIELD, ST_ON_VAR, ST_ON_TYPENAME, ST_ON_TAG, ST_ON_FN, ST_ON_FIELDS, ST_ON_VARS };

typedef struct {
    int readable;
    int kind;            /* ST_RULE_* */
    int on;              /* ST_ON_*: what it names (the first of its kind's keys it has) */
    int64_t rev;         /* history rules: from which revision on (0 for default, hashes) */
    const char* name;    /* field: the member's key; var: the identity; typename, tag, fn: the name */
    const char* old;     /* was: the old key, identity or name */
    int32_t tidCount;    /* field, fields: the struct's type entries */
    const char** tids;   /* their keys */
    int32_t* types;      /* their indexes in this schema (ST_NONE: not one of its types) */
    int32_t nameCount;   /* fields: the removed members' keys; vars: the removed variables' identities */
    const char** names;
    int32_t mapCount;    /* values: (old, new) pairs */
    int64_t (*map)[2];
    int64_t value;       /* removed, default */
    const char* at;      /* where the macro is (informative) */
} StRule;

typedef struct StateSchema StateSchema;

/* The schema in a JSON text (n bytes); NULL on an error, described in err. */
StateSchema* stateSchemaParse(const char* json, size_t n, char* err, size_t errSize);
/* A schema file: state_schema.json as the build writes it, or packed by statePack ("XGSCHEMA" + sizes). */
StateSchema* stateSchemaLoad(const char* path, char* err, size_t errSize);
/* The same, rebased: every address the schema gives (variables, functions, symbols, sections, regions,
 * the read-only ranges; not their file offsets) plus bias, modulo 2^64, before anything is indexed. A
 * schema holds link addresses; a process's image sits at link address + its load bias (statefile.h
 * stateImageBias, a state's header): rebased by it, the schema describes that process's memory, as
 * statecanon.py Schema(build, bias) does. */
StateSchema* stateSchemaParseAt(const char* json, size_t n, uint64_t bias, char* err, size_t errSize);
StateSchema* stateSchemaLoadAt(const char* path, uint64_t bias, char* err, size_t errSize);
void stateSchemaFree(StateSchema* s);

const char* stateSchemaVersion(const StateSchema* s); /* "us" or "jp" */
/* Whether a state may convert to or from this build: 0 for a schema whose build left the data's types
 * unknown (clang: no debug information for extern declarations, xg-htn.60.8), with why; absent: 1. */
int stateSchemaPortable(const StateSchema* s, const char** why);
uint64_t stateSchemaBias(const StateSchema* s);       /* what its addresses were rebased by */
/* The identity of the image the schema was read from (its "image": statefile.h stateImageIdText, which
 * neither the load address nor stripped debug information changes); "" in a schema without one. */
const char* stateSchemaImageId(const StateSchema* s);

int32_t stateSchemaTypeCount(const StateSchema* s);
const StType* stateSchemaTypeAt(const StateSchema* s, int32_t i);
int32_t stateSchemaType(const StateSchema* s, const char* tid);        /* ST_NONE if absent */
int32_t stateSchemaTypeNamed(const StateSchema* s, const char* name);  /* type_names: ST_NONE if absent */
const char* stateSchemaTidName(const StateSchema* s, int32_t type);   /* statecanon tid_name, NULL if none */

/* The variables, one per address (the largest where two share one), in address order. */
int32_t stateSchemaVarCount(const StateSchema* s);
const StVar* stateSchemaVarAt(const StateSchema* s, int32_t i);
int32_t stateSchemaVarById(const StateSchema* s, const char* id);
int32_t stateSchemaVarNamed(const StateSchema* s, const char* name);       /* the first with that name */
int32_t stateSchemaPlatformVar(const StateSchema* s, const char* file, const char* name);
/* The variable holding addr (statecanon Schema.var_at, its search limits too), ST_NONE if none. */
int32_t stateSchemaVarHolding(const StateSchema* s, uint64_t addr);
/* Ranges no state carries (scratch64 variables): count and each. */
int32_t stateSchemaScratchCount(const StateSchema* s);
StRange stateSchemaScratch(const StateSchema* s, int32_t i);

int32_t stateSchemaFuncCount(const StateSchema* s);
const StFunc* stateSchemaFuncAt(const StateSchema* s, int32_t i);
/* A code address's identity (statecanon fn_identity) into out; 0 if none. */
int stateSchemaFnIdentity(const StateSchema* s, uint64_t addr, char* out, size_t size);
/* An identity's address in this build (statecanon fn_address: by identity, by name, by symbol, and a game
 * function's by its retail address alone where one function has it); 0 if none. */
uint64_t stateSchemaFnAddress(const StateSchema* s, const char* identity);

int32_t stateSchemaSymbolCount(const StateSchema* s);
const StSymbol* stateSchemaSymbolAt(const StateSchema* s, int32_t i);
/* The first address of a symbol name; 0 if none. */
uint64_t stateSchemaSymbolAddress(const StateSchema* s, const char* name);
/* The symbol at or below addr, any kind (statecanon symbol_at): its name and address; NULL if none. */
const char* stateSchemaSymbolBelow(const StateSchema* s, uint64_t addr, uint64_t* at);
/* The named constant (nm r/R, sized) holding addr (statecanon ro_named_at); NULL if none. */
const StRange* stateSchemaNamedConstant(const StateSchema* s, uint64_t addr);
/* Whether addr is in the file-backed read-only data (statecanon ro_at), and its file offset. */
int stateSchemaReadOnly(const StateSchema* s, uint64_t addr, uint64_t* fileOffset);
/* The file-backed read-only ranges themselves (statecanon Schema.ro, in its order): (lo, hi, file offset). */
int32_t stateSchemaReadOnlyCount(const StateSchema* s);
void stateSchemaReadOnlyAt(const StateSchema* s, int32_t i, uint64_t* lo, uint64_t* hi, uint64_t* fileOffset);
/* Whether a symbol name is defined (statecanon sym_addr), and its first address: a symbol at 0 is found too. */
int stateSchemaSymbolFind(const StateSchema* s, const char* name, uint64_t* addr);
/* Whether an identity has an address in this build (statecanon fn_address is not None), and which. */
int stateSchemaFnFind(const StateSchema* s, const char* identity, uint64_t* addr);
/* The executable the schema describes (its "exe": where statecanon reads the strings), "" if none. */
const char* stateSchemaExe(const StateSchema* s);

/* sections ("main.data" ...) and regions ("heapArena" ...): ST_NONE-like {NULL} if absent */
StRange stateSchemaSection(const StateSchema* s, const char* name);
int32_t stateSchemaSectionCount(const StateSchema* s);
StRange stateSchemaSectionAt(const StateSchema* s, int32_t i);
StRange stateSchemaRegion(const StateSchema* s, const char* name);
/* the platform file of a host part ("hw" -> "system/hw.c"), NULL if none */
const char* stateSchemaPartFile(const StateSchema* s, const char* part);

const StHeapType* stateSchemaHeapType(const StateSchema* s, uint32_t hash);
int32_t stateSchemaHeapTypeCount(const StateSchema* s);
const StCounted* stateSchemaCounted(const StateSchema* s, int32_t type);         /* by the struct's type */
const StCounted* stateSchemaCountedNamed(const StateSchema* s, const char* name);
const StUnion* stateSchemaUnion(const StateSchema* s, int32_t type);
const StUnion* stateSchemaUnionNamed(const StateSchema* s, const char* name);

/* The state rules: their language, this build's state revision (0 for a schema from before rules), and the
 * rules in the schema's order. */
int64_t stateSchemaRulesLanguage(const StateSchema* s);
int64_t stateSchemaRevision(const StateSchema* s);
int32_t stateSchemaRuleCount(const StateSchema* s);
const StRule* stateSchemaRuleAt(const StateSchema* s, int32_t i);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SYSTEM_STATESCHEMA_H */
