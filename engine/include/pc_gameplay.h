#ifndef PC_GAMEPLAY_H
#define PC_GAMEPLAY_H

/* The caller owns the fractional countdown tick in its overlay, so reset
 * and extras sandboxes keep independent encounter clocks. No random draws. */
int pcGameplayEncounterTicks(int* fraction);
/* The 60 Hz world map (worldmap60, XG_WORLDMAP60; engine/src/worldmap/w_80070CFC.c). */
int pcGameplayWorldmap60(void);
/* The 60 Hz battle (battle60, XG_BATTLE60; port/src/game/battle/gearpose.c). */
int pcGameplayBattle60(void);
/* The Enhanced 60 Hz field's picture oracle (xg-hbc.19.3; engine/src/field/f_80072140.c):
 * the drawing code adds what it draws into a slot's hash (XG_SWEEP_PICTURE FP lines). */
enum { PC_FIELD_PIC_FADERS, PC_FIELD_PIC_PARTICLES, PC_FIELD_PIC_DIALOGS, PC_FIELD_PIC_DISTORT, PC_FIELD_PIC_GEARS,
       PC_FIELD_PIC_TRANSITION, PC_FIELD_PIC_SCROLL, PC_FIELD_PIC_TASKS, PC_FIELD_PIC_COUNT };
void pcFieldPicAdd(int slot, int value);
/* The Enhanced 60 Hz field's off-tick frames: the dialogue text step skips them. */
void pcGameplaySetDialogStepSkip(int skip);
/* ... and a frame between previewing the next page's clear draws no text lines (xg-hbc.19.5). */
void pcGameplaySetDialogHideLines(int on);
int pcGameplayDialogHideLines(void);
/* A freshly loaded sBattleEntity for an enemy slot; never saved party data. */
void pcGameplayEnemyStats(void* entity);
void pcGameplayEnemyStatsForType(void* entity, int set, int type);
void pcGameplayEnemyLoaded(void* entity, int slot, int set, int type);
void pcGameplayEnemyCopied(int dest, int source);
/* Enemy script units: kind 0 byte stats, 1 word stats, 2 Gear HP;
 * field is the VM field ID (Gear HP: 0 max, 1 current). Party is unchanged. */
int pcGameplayEnemyScriptValue(int slot, int kind, int field, int value, int read);
/* The battle class for rewards: the field/world-map countdown rolled
 * config (an index into battleConfigs); a battle loads requestedBattleConfig
 * (chained: requested by battle event op 0x24). */
void pcGameplayRandomBattle(int config);
void pcGameplayBattleStart(int config, int chained);
/* Final awards, after native EXP sharing/bonuses or gold eligibility. */
unsigned int pcGameplayExperience(unsigned int amount);
unsigned int pcGameplayGold(unsigned int amount);
/* One eligible attack's deathblow practice: the native adds increment
 * (learn rate + accessory bonus) to progress (below 65000) for character
 * (m56_battleCommandLoadout) and combo counter (0..6); returns the new
 * progress, the setting's share of increment with a carried remainder. */
unsigned int pcGameplayDeathblowPractice(unsigned int character, unsigned int combo, unsigned int progress,
                                         unsigned int increment);
/* Dialogue settings. The printer started text (twoByteLead: dialogFontVar0)
 * in window; extra printer steps for window this frame; the field (context
 * 0) or battle (1) page waiting for confirm at play time now (vsyncs): 1
 * when auto-advance presses. */
void pcGameplayDialogueText(const void* window, const unsigned char* text, int twoByteLead);
int pcGameplayDialogueSteps(const void* window, int confirmHeld);
int pcGameplayDialogueAuto(int context, int waiting, unsigned int now);
/* The targeted enemy's HP and statuses (port/src/game/battle/enemyinfo.c). */
int pcGameplayEnemyInfo(void);
/* Retry a lost battle: the checkpoint as a battle starts (battleMain), the
 * Retry / Game over choice when its loop ends in a wipe the game would end
 * in its game over, and the restart where that game over would start
 * (battle_boot.c): 1 when the battle starts again
 * (port/src/game/main/retry.c, port/src/game/battle/retryprompt.c). */
void pcRetryCheckpoint(void);
void pcBattleRetryOffer(void);
void pcBattleRetryDraw(void);
int pcRetryRestore(void);
/* One shared opt-in for corrections to original-game defects. */
int pcGameplayOriginalBugFixes(void);
/* A field channel's looping sound effect, which a menu's sounds cut, starts
 * again after the menu (port/src/game/field/sfxkeep.c): OP_C4SubSub1 notes
 * each start; openFieldMenu takes the mask before the menu, restores after. */
void pcFieldSfxPlayed(int id, int volume, int pan, int channel);
unsigned int pcFieldSfxLoopingMask(void);
void pcFieldSfxRestore(unsigned int mask);
/* Original-game bug fixes in a field's script as it is decompressed
 * (port/src/game/field/scriptfix.c; code: the routines' bytecode). */
void pcFieldScriptFixes(int field, unsigned char* code, int size);
/* A field music instance just created and started (updateMusicState2). */
void pcFieldMusicStarted(int music, void* instance);
/* A freshly read dialogue TIM, before loadTimToVram changes its header. */
void pcGameplayPortraitTim(void* tim, unsigned int bytes, unsigned int file);
/* Gauntlets (F1 > Gameplay > Gauntlet; port/src/system/gauntlet.h). The
 * platform keeps the run: its state (0 none, 1 waiting for the field or the
 * world map, 2 running), its fights (a field's 16 battle configs, the config
 * index, the party, Gears or not), the current one and the recovery. */
int pcGauntletState(void);
int pcGauntletTakeStart(void); /* 1: the first fight starts now */
int pcGauntletFight(int index, unsigned char* table, unsigned char* party, int* gear); /* config, -1 past the end */
int pcGauntletFightMusic(int index); /* the theme the story plays before it, -1 none */
int pcGauntletCurrent(void);
void pcGauntletNext(void);
void pcGauntletRetried(void);
int pcGauntletRecovery(void);
int pcGauntletQuitting(void);
void pcGauntletFinish(int completed);
/* The game's side (port/src/game/main/gauntlet.c): the first fight from the
 * field or the world map (1 when it starts), each battle's set-up
 * (battleMain), its outcome before finishBattle (battle/gauntlet.c passes
 * the battle's state) and the next mode where battle_boot picks it (-1: the
 * game's own). */
int pcGauntletStartBattle(void);
void pcGauntletBattleSetup(void);
int pcGauntletOver(int running, int eventLoss, int nativeChain, unsigned int knockedOut);
int pcGauntletBattleEnd(void);
void pcGauntletBattleOverHook(void);
void pcGauntletFieldTick(void);
int pcGauntletGameRoster(unsigned char* ids, unsigned char* hasGear, unsigned char* names, unsigned char* party);

#endif
