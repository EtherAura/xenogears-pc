#ifndef PORT_MDEC_MDEC_H
#define PORT_MDEC_MDEC_H

/*
 * The MDEC, the PS1's macroblock decoder (port/src/mdec).
 *
 * mdec.c is the decoder: its commands, run-length decoding, dequantisation,
 * the IDCT and the colour conversion, with no notion of time. libpress.c puts
 * it behind LIBPRESS (DecDCTin/out/Reset and friends), with DMA0/DMA1
 * completing at about the hardware's speed and their callbacks delivered as
 * interrupts (system/hw.h). vlc.c is LIBPRESS's bitstream decoder
 * (DecDCTvlc), which turns STR frames into the MDEC's input.
 *
 * mdec.c and vlc.c depend on nothing else, so tests can link them on their
 * own.
 */

#include <stdint.h>

/* MDEC1 status bits (psx-spx "MDEC I/O Ports"). */
#define MDEC_ST_OUT_EMPTY 0x80000000u
#define MDEC_ST_IN_FULL 0x40000000u
#define MDEC_ST_BUSY 0x20000000u
#define MDEC_ST_DREQ0 0x10000000u
#define MDEC_ST_DREQ1 0x08000000u

/* MDEC1 control bits. */
#define MDEC_CTL_RESET 0x80000000u
#define MDEC_CTL_DMA0 0x40000000u
#define MDEC_CTL_DMA1 0x20000000u

/* mdec.c */
void mdecControl(uint32_t value);    /* MDEC1 write: reset, DMA enables */
uint32_t mdecControlBits(void);
uint32_t mdecStatus(void);           /* MDEC1 read (the output FIFO is reported empty) */

/* A command word written to MDEC0. The parameters follow with mdecFeed. */
void mdecCommand(uint32_t cmd);

/* Parameter words for the current command (what DMA0 sends). Table
 * uploads take theirs at once; a decode keeps a pointer and reads it as
 * macroblocks are decoded, so the words must stay valid until then. */
void mdecFeed(const uint32_t* words, uint32_t count);

int mdecBusy(void);                  /* the command still expects parameter words */
uint32_t mdecFedLeft(void);          /* fed words not consumed yet */
int mdecDecoding(void);              /* the current command is a decode */

/* Bytes of one macroblock in the current decode's depth: 16x16 pixels in
 * colour (15 or 24 bpp), 8x8 in monochrome (4 or 8 bpp). */
uint32_t mdecMacroblockSize(void);

/* Decode the next macroblock into `out` as DMA1 lays it out in RAM (rows of
 * 16 pixels, or 8 in monochrome). Returns its size, or 0 when the fed input
 * ends first (a partly read block is kept for the next mdecFeed). */
uint32_t mdecDecode(uint8_t* out);

/* Read FE00h halfwords waiting at a block start (padding the MDEC skips
 * without producing output), as the hardware does while it waits. */
void mdecSkipPadding(void);

/* The tables as uploaded (quant tables, then the 64 IDCT scale halfwords). */
void mdecGetTables(uint8_t iq[2][64], int16_t scale[64]);

/* ---- a world of its own ---- */

/* An MDEC of a world of its own, for the extras (the gallery plays movies
 * with the game's own video player, port/src/extras): LIBPRESS's tables,
 * callbacks and DMA transfers (libpress.c), the decoder (mdec.c) and the
 * VLC decodes in progress (vlc.c), all its own; DecDCTReset leaves LIBETC
 * alone there (its ResetCallback is the game's). Swap it in only inside a
 * world of its own (port/src/system/world.h): its transfers complete on that
 * world's queue and clock. mdecWorldSwap puts it in place and, called again,
 * back; the game's MDEC is not touched. */
#include <stddef.h>
typedef struct MdecWorld MdecWorld;
MdecWorld* mdecWorldCreate(void);
void mdecWorldDestroy(MdecWorld* w);
void mdecWorldSwap(MdecWorld* w);

/* Each file's part (mdecWorldCreate keeps them). */
size_t mdecDecoderPartSize(void); /* mdec.c */
void mdecDecoderPartInit(void* blob);
void mdecDecoderPartSwap(void* blob);
size_t vlcPartSize(void);         /* vlc.c */
void vlcPartInit(void* blob);
void vlcPartSwap(void* blob);

#endif /* PORT_MDEC_MDEC_H */
