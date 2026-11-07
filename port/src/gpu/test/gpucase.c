/*
 * Run a GPU operation list through the GPU layer and write what it reads
 * back: the port's side of the black-box GPU measurements (the same lists go
 * to the reference emulators through a PS-X EXE that executes them).
 *
 *   gpucase OPS.bin OUT.bin
 *
 * OPS.bin is little-endian 32-bit words, operations until a 0 word:
 *   0x01nnnnnn w1..wn          n words to GP0
 *   0x02nnnnnn w1..wn          n words to GP0 (DMA on the console)
 *   0x06nnnnnn w1..wn          n words to GP0 (data, no wait on the console)
 *   0x03000000 xy wh count dst read a VRAM rectangle and append it to OUT.bin
 *                              (dst 0: read and drop); GP0(C0h) on the
 *                              console, so the texture cache empties
 *   0x04000000 w               a GP1 command: GP1(00h) resets the GP0 state,
 *                              the others are not modelled
 *   0x05nnnnnn                 a delay: ignored
 *
 * XG_GPUCASE_UPSCALE=N runs with the upscaled copy at N times (its native
 * VRAM must not change).
 */

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "psyq.h"
#include "gpu/gpu.h"
#include "gpu/gpu_internal.h"

int gpuInit(SDL_Window* window);
void gpuShutdown(void);

static void readRect(int x, int y, int w, int h, FILE* out) {
    VPiece pieces[4];
    int n = vramWrap(x, y, w, h, pieces);
    uint16_t* buf = calloc((size_t)w * h, 2);
    for (int i = 0; i < n; i++) {
        vramSyncRect(pieces[i].r);
    }
    for (int i = 0; i < n; i++) {
        VPiece* pc = &pieces[i];
        for (int row = pc->r.y0; row <= pc->r.y1; row++) {
            memcpy(buf + (size_t)(row - pc->r.y0 + pc->dy) * w + pc->dx, &vramShadow[row][pc->r.x0],
                   (size_t)(pc->r.x1 - pc->r.x0 + 1) * 2);
        }
    }
    fwrite(buf, 2, (size_t)w * h, out);
    free(buf);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: gpucase OPS.bin OUT.bin\n");
        return 2;
    }
    FILE* f = fopen(argv[1], "rb");
    if (f == NULL) {
        perror(argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint32_t* ops = malloc((size_t)size + 4);
    size_t words = fread(ops, 4, (size_t)size / 4, f);
    fclose(f);
    ops[words] = 0;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("gpucase", 320, 240, SDL_WINDOW_VULKAN);
    if (!gpuInit(window)) {
        return 1;
    }
    ResetGraph(0);
    const char* up = getenv("XG_GPUCASE_UPSCALE");
    if (up != NULL && atoi(up) > 1) {
        hiresSetUpscale(atoi(up));
        gvkHiConfigure(atoi(up), hiresColor());
    }
    FILE* out = fopen(argv[2], "wb");
    if (out == NULL) {
        perror(argv[2]);
        return 1;
    }
    size_t i = 0;
    while (i < words && ops[i] != 0) {
        uint32_t op = ops[i] >> 24, n = ops[i] & 0xFFFFFF;
        i++;
        switch (op) {
        case 1:
        case 2:
        case 6:
            gp0Write(&ops[i], (int)n);
            i += n;
            break;
        case 3: {
            int x = (int)(ops[i] & 0x3FF), y = (int)((ops[i] >> 16) & 0x1FF);
            int w = (int)(((ops[i + 1] & 0xFFFF) - 1) & 0x3FF) + 1;
            int h = (int)(((ops[i + 1] >> 16) - 1) & 0x1FF) + 1;
            if (ops[i + 3] != 0) {
                readRect(x, y, w, h, out);
            }
            gp0InvalidateTexture();
            i += 4;
            break;
        }
        case 4:
            if ((ops[i] >> 24) == 0) {
                gp0Reset();
            }
            i += 1;
            break;
        case 5:
            break;
        default:
            fprintf(stderr, "gpucase: unknown operation %08x\n", ops[i - 1]);
            i = words;
            break;
        }
    }
    fclose(out);
    gpuShutdown();
    return 0;
}
