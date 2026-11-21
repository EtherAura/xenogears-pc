#ifndef XG_GAME_ASMLABEL_H
#define XG_GAME_ASMLABEL_H
/* A C name written inside assembly must carry the target ABI's prefix. */
#define XG_STRINGIFY_1(x) #x
#define XG_STRINGIFY(x) XG_STRINGIFY_1(x)
#define XG_ASM_LABEL(name) XG_STRINGIFY(__USER_LABEL_PREFIX__) #name
#endif
