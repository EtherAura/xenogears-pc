#ifndef PC_SLIDE_H
#define PC_SLIDE_H
/* The two bootField call paths whose saved registers overlap its warm-up
 * wall-slide edge buffer. Other field/script callers have different stacks. */
enum { PC_SLIDE_LOADER_OFF, PC_SLIDE_LOADER_INIT, PC_SLIDE_LOADER_MUSIC };
/* The first check inherits initFieldData's &D_800AFB10, then each following
 * party check inherits doPCCollisionCheckAfterLoading's actor index. */
enum { PC_SLIDE_PARTY_FIRST = -1 };
void pcSlideEdgeLoader(int phase);
/* First call saves the object count in $s3; a later party call saves its
 * actor-array address. Zero for an unknown address preserves that word. */
void pcSlideEdgePartyInit(int actor, unsigned int savedActorOrCount);
void pcSlideEdgePartySprite(const void* sprite);
void pcSlideEdgeMusicRead(int result);
/* Only the direct updateScriptAndMoveEntities -> setVisualAnimation call
 * places spriteActorSetPlayingAnimation's saved animation in edge word 2. */
void pcSlideEdgeAnimationScope(int active);
void pcSlideEdgeAnimation(int animation, const void* array, const void* actor, int count, int stride);
#endif
