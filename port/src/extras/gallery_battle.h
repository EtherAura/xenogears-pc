#ifndef PORT_GALLERY_BATTLE_H
#define PORT_GALLERY_BATTLE_H
#include <stdint.h>

/* Reachable literal OP27 requests in a decompressed battle-event script.
 * The callback receives its bytecode offset and type, stream, fade, last
 * frame. Variable operands remain unresolved. Returns the requests found,
 * or -1 for an invalid header/allocation failure; unknown ops stop a path. */
typedef void (*XgBattleMovieFn)(void* context, int pc, const int args[4]);
int xgBattleMovies(const uint8_t* script, int size, XgBattleMovieFn found, void* context);
#endif
