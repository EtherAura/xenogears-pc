#ifndef PORT_EXTRAS_SPRITES_GAME_H
#define PORT_EXTRAS_SPRITES_GAME_H

/*
 * A sprite actor shown by the game's own sprite code (sprites_game.c), for
 * the sprite viewer (sprites.cpp, xg-htn.2). In a code sandbox (sandbox.h)
 * the actor is made as the field or the battle makes it:
 *
 *   - its VRAM made ready first, as the field does it: the field's texture
 *     file (04/B9+2n) through filesystem.c's own sector decoder
 *     (processImageFileSectorPc), the TIMs of its section 0, the NPC sheets
 *     of its section 4 (uploadNpcSpriteSheet, at the field's VRAM mapping);
 *     the battle's effect sprites (2C/02, createSavePointMeshData_mode5) and their texture
 *     (2C/01, uploadTextureToVram) for animations that spawn effects;
 *   - createSpriteActor(EX) with the VRAM place, CLUT row and palette the
 *     game gives it (resetFieldScriptEntityGraphicEntity's choices for the
 *     field; the battle loader's for battle sprites);
 *   - each drawn frame as the field's renderChars runs it: the frame's parts
 *     assembled and its 4-bit pieces queued for VRAM (uploadCharacterSprites:
 *     uploadCharacterSprite / uploadCharacterSpriteSub1), effect sprites the
 *     animation spawned drawn and updated (execSpritesCallbacksList1/2), the
 *     actor's sub-sprites projected by the GTE into an ordering table
 *     (renderSpriteActor), then the sprite bytecode VM ticked
 *     (OP_INIT_ENTITY_SCRIPT_sub0Sub9: executeSpriteBytecode2, or the battle
 *     overlay's executeSpriteBytecode2_battle); paired party battle actors
 *     also run native movement over sandbox ground, the queued pieces sent to
 *     VRAM (shapeTransfert) and the table drawn (DrawOTag) into the
 *     sandbox's GPU (vgpu.h), whose primitives a view shows.
 *
 * Scale: as the field (the sprite's pixels 8 units, its matrix and scale
 * 0xC00: 6 world units a pixel) or the battle (scale 0x2000: 2 units a
 * pixel), seen face on from a camera whose projection maps world units to
 * screen units one to one; xgSpriteUnits gives the units a sprite pixel
 * takes. Effect sprites thus move as far, for the sprite's size, as in the
 * game.
 *
 * Opcodes that need more of the game around the actor (battle effects,
 * cameras, faders, damage numbers, target lists, jumps over the battle's
 * terrain, the pad, 3D model objects, effect sprites of sets only a battle
 * loads) are found by walking every animation's bytecode beforehand and,
 * in the sandbox's copy of the bundle, replaced by opcodes of the same
 * length that no VM handles: the actor's own frames, waits and moves play
 * as in the game, the battle's part is left out (xgSpriteAnimationFlags,
 * xgSpriteAnimationSkipped). Code that runs out of the bundle is not run.
 * Paired party bundles retain bank switches, limb resets, velocity helpers
 * and target jumps: the viewer owns both banks, transforms, parameter stack,
 * a target and flat terrain, and follows horizontal travel.
 */

#include <stdint.h>

#include "vgpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A field VRAM mapping slot (the field archive's 32 eight-byte records). */
typedef struct {
    int x, y;
    int fromTextureFile; /* record +6 != 0: the field's texture file holds it, not section 4 */
} XgSpriteSlot;

typedef struct {
    int animation, file;
    const uint8_t* data; int size;
} XgSpriteModelExtra;
typedef struct {
    int id;
    const uint8_t* model; int modelSize;
    const uint8_t* animation; int animationSize;
    const XgSpriteModelExtra* extras; int extraCount;
} XgSpriteModel;

typedef struct {
    const uint8_t* bundle; /* the AnimationBundle: count, animations, frames, palettes (offsets from its start) */
    int bundleSize;
    const uint8_t* companion; int companionSize; /* paired party battle bundle (normal <-> special) */
    int special; /* the selected bundle is the special half of the pair */
    int vramX, vramY;      /* the actor's VRAM place (pieces, or the sheet's page) */
    int clutX, clutY;      /* its CLUT: palette p goes to clutX + 16 p on the field, clutX in battle */
    int battle;            /* 0: as the field does; 1: as the battle does (its VM, scale, VRAM rules) */
    int nativeEffects; /* dedicated effects viewer: allow bounded native FX commands */
    const uint8_t* caster; int casterSize; /* owned preview caster/target for spell controllers */
    const uint8_t* fragments; int fragmentsSize; /* ability file auxiliary sprite bank */
    const XgSpriteModel* models; int modelCount; /* ability's 28 model/animation pairs and 2A clips */
    int subSprites;        /* the sub-sprite buffer the game gives (0: the bundle's count) */
    /* VRAM made ready before the actor is made (each may be absent): */
    const uint8_t* textureFile; int textureFileSize; /* 04/B9+2n */
    const uint8_t* tims; int timsSize;               /* field section 0, decompressed */
    const uint8_t* sheets; int sheetsSize;           /* field section 4, decompressed */
    const uint8_t* enemySheet; int enemySheetSize;   /* a battle enemy's resolved sheet */
    int enemyPalette;                              /* native config's CLUT selection */
    const XgSpriteSlot* slots; int slotCount;
    const uint8_t* effects; int effectsSize;         /* 2C/02 */
    const uint8_t* effectsTexture; int effectsTextureSize; /* 2C/01 */
} XgSpriteSetup;

/* Field uploads alone, through the same native decoders as a preview, for
 * comparing NPC textures without making or stepping actors. out holds
 * XG_VRAM_W * XG_VRAM_H halfwords. Returns 0 if preparation fails. */
int xgSpriteFieldVram(const XgSpriteSetup* setup, uint16_t* out);

typedef struct XgSprite XgSprite;

/* NULL on failure, with the reason in why. */
XgSprite* xgSpriteCreate(const XgSpriteSetup* setup, char* why, int whySize);
void xgSpriteDestroy(XgSprite* s);

/* What the bundle holds. */
int xgSpriteAnimationCount(const XgSprite* s);
int xgSpriteFrameCount(const XgSprite* s);   /* frames 1..count (0: none) */
int xgSpritePaletteCount(const XgSprite* s);
int xgSpriteColours(const XgSprite* s);      /* colours in one palette */
int xgSpritePrebaked(const XgSprite* s);     /* frames from a sheet in VRAM (else pieces sent each frame) */
int xgSpriteUnits(const XgSprite* s);        /* world (= screen) units a sprite pixel takes */
int xgSpriteBattle(const XgSprite* s);

/* An animation's facing tables: 1, 4 or 8 directions (the bytecode header's
 * bits 0-1: 0, 1, 2), with flags. */
#define XG_SPRITE_ANIM_UNSAFE 0x01 /* its code runs out of the bundle: not run */
#define XG_SPRITE_ANIM_EFFECTS 0x02 /* spawns effect sprites */
#define XG_SPRITE_ANIM_MOVES 0x04   /* moves the actor (speed, jumps) */
#define XG_SPRITE_ANIM_RANDOM 0x08  /* uses rand() (the game's own sequence, the sandbox's seed) */
#define XG_SPRITE_ANIM_SKIPS 0x10   /* battle-only opcodes stepped over (see the header) */
#define XG_SPRITE_ANIM_SCREEN 0x20  /* captures the owned scene for a screen effect */
int xgSpriteAnimationDirections(const XgSprite* s, int anim);
int xgSpriteAnimationFlags(const XgSprite* s, int anim);
/* First earlier entry using the same retail animation header, or -1. */
int xgSpriteAnimationAlias(const XgSprite* s, int anim);
/* The i-th (0..3) different battle-only opcode an animation steps over, -1. */
int xgSpriteAnimationSkipped(const XgSprite* s, int anim, int i);
/* The game's angle (4096 = 360 degrees) that picks facing d of an
 * animation with n directions (setSpriteActorAngle), and whether that
 * facing is a mirrored table. */
int xgSpriteDirectionAngle(int directions, int d);
int xgSpriteDirectionMirrored(int directions, int d);

/* A fresh world with the actor made as the game makes it, then animation
 * `anim` started facing d (or, xgSpriteShowFrame, frame `frame` held, flipped
 * if asked: setSpriteActorStaticFrame). Palette p of xgSpritePaletteCount.
 * An unsafe animation is not started (returns 0). */
void xgSpriteFacing(XgSprite* s, int anim, int direction);
int xgSpritePlay(XgSprite* s, int anim, int direction, int palette);
int xgSpriteShowFrame(XgSprite* s, int frame, int flip, int palette);

/* One drawn frame: the actor and its effects drawn as the game draws them,
 * then (advance) the VM's ticks for that frame: 2 on the field (its frames
 * last two vblanks), 1 in battle. Returns the primitives drawn. */
int xgSpriteStep(XgSprite* s, int advance);
int xgSpriteTicksPerStep(const XgSprite* s);
XgVgpu* xgSpriteGpu(XgSprite* s);

typedef struct {
    int frame;      /* the frame shown (0: none) */
    int flipX, flipY;
    int step;       /* the bytecode step (frame ops run) */
    int ended;      /* the animation ran its end opcode (or stopped) */
    int effects;    /* effect sprites alive */
    int subSprites; /* parts of the frame */
    int heapFull;   /* frames stopped: the effects filled the sandbox's heap */
    int nativeCommands, wobbles; /* dedicated effects sandbox diagnostics */
    uint32_t key;   /* a hash of the VM's state and the picture, for finding where an animation repeats */
} XgSpriteState;
void xgSpriteGetState(const XgSprite* s, XgSpriteState* out);

/* The bounds of the primitives drawn (screen units); 0 if none. */
int xgSpritePrimBounds(XgSprite* s, int* x0, int* y0, int* x1, int* y1);
/* Frame the party actor's poses without shrinking it to fit distant effects. */
int xgSpritePreviewBounds(XgSprite* s, int* x0, int* y0, int* x1, int* y1);
/* The bounds of every frame of the bundle, each held with no animation
 * (computed once, in a world of its own); 0 if no frame draws anything. */
int xgSpriteFrameBounds(XgSprite* s, int* x0, int* y0, int* x1, int* y1);

/* The primitives drawn as a picture of w x h sprite pixels whose top left is
 * at screen (x0, y0) (screen units), RGBA (alpha 0 where nothing is drawn
 * when bg is 0, else over the opaque colour bg = 0xAABBGGRR): the views'
 * per-pixel rules (view.frag) at one pixel per sprite pixel. */
void xgSpriteRaster(XgSprite* s, int x0, int y0, int w, int h, uint32_t bg, uint32_t* rgba);

/* The field the paused game is in (its map number's low 12 bits), -1 when
 * no field runs (the extras started on their own, or another mode). */
int xgSpriteGameField(void);

/* Validation: the game's rand() seed (outside a sandbox), which running
 * sprites must leave as it is. */
unsigned xgSpriteGameRandSeed(void);

/* Validation: the actor's place, matrix, parts (SubSprite) and subgroup
 * transforms on stderr, to compare with PCSX-Redux's RAM. */
void xgSpriteDebugDump(const XgSprite* s);

#ifdef __cplusplus
}
#endif

#endif /* PORT_EXTRAS_SPRITES_GAME_H */
