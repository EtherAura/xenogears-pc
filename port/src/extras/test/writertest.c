/*
 * The extras' writers (writers.h) on known data, for checking the files with
 * outside tools (an image viewer, a WAV reader, Khronos glTF-Validator or
 * Blender):
 *
 *   cmake --build <build> --target extras_writertest
 *   <build>/extras_writertest <dir>
 *
 * writes <dir>/test.png (a 64 x 48 gradient with a clear corner),
 * <dir>/test.wav (a second of a 440 Hz tone, stereo, 44.1 kHz) and
 * <dir>/test.glb (a textured quad and a coloured triangle under a parent node,
 * with a two-second animation of each kind: translation, rotation, scale,
 * plus a STEP scale channel, two morph targets on the triangle and a STEP
 * weights channel).
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../writers.h"

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : ".";
    char path[1024];
    int failed = 0;

    static uint32_t rgba[48][64];
    for (int y = 0; y < 48; y++) {
        for (int x = 0; x < 64; x++) {
            rgba[y][x] = x < 8 && y < 8 ? 0 : (uint32_t)(x * 4) | (uint32_t)(y * 5) << 8 | 0x80u << 16 | 0xFFu << 24;
        }
    }
    snprintf(path, sizeof(path), "%s/test.png", dir);
    if (!xgPngWrite(path, &rgba[0][0], 64, 48)) {
        fprintf(stderr, "png: %s\n", xgWriterError());
        failed++;
    }

    static int16_t pcm[44100 * 2];
    for (int i = 0; i < 44100; i++) {
        pcm[i * 2] = pcm[i * 2 + 1] = (int16_t)(sin(i * 2 * M_PI * 440 / 44100) * 12000);
    }
    snprintf(path, sizeof(path), "%s/test.wav", dir);
    if (!xgWavWrite(path, pcm, 44100, 2, 44100)) {
        fprintf(stderr, "wav: %s\n", xgWriterError());
        failed++;
    }

    XgGltf* g = xgGltfCreate();
    size_t pngSize;
    uint8_t* png = xgPngEncode(&rgba[0][0], 64, 48, &pngSize);
    int textured = png != NULL ? xgGltfAddTexturedMaterial(g, "picture", png, pngSize, 0) : -1;
    int coloured = xgGltfAddColourMaterial(g, "colours");
    free(png);
    const float quad[] = {-1, -1, 0, 1, -1, 0, -1, 1, 0, 1, 1, 0};
    const float quadUv[] = {0, 1, 1, 1, 0, 0, 1, 0};
    const uint32_t quadIdx[] = {0, 1, 2, 1, 3, 2};
    const float tri[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const float triCol[] = {1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1};
    const uint32_t triIdx[] = {0, 1, 2};
    int m0 = xgGltfAddMesh(g, "quad", textured, quad, quadUv, NULL, 4, quadIdx, 6);
    int m1 = xgGltfAddMesh(g, "triangle", coloured, tri, NULL, triCol, 3, triIdx, 3);
    const float up[3] = {0, 1.5f, 0};
    int root = xgGltfAddNode(g, "root", -1, -1, NULL, NULL, NULL);
    int n0 = xgGltfAddNode(g, "quad", m0, root, NULL, NULL, NULL);
    int n1 = xgGltfAddNode(g, "triangle", m1, root, up, NULL, NULL);
    const float delta0[9] = {0, 0, 0, 1, 0, 0, 0, 0, 0};
    const float delta1[9] = {0, 0, 0, 0, 0, 0, 0, 1, 0};
    const float initial[2] = {0.25f, 0.5f};
    if (xgGltfAddMorphTarget(g, m1, 0, delta0, 3) < 0 ||
        xgGltfAddMorphTarget(g, m1, 0, delta1, 3) < 0 ||
        !xgGltfSetMorphWeights(g, m1, initial, 2)) failed++;
    int a = xgGltfAddAnimation(g, "test");
    const float times[3] = {0, 1, 2};
    const float moves[9] = {0, 0, 0, 0, 1, 0, 0, 0, 0};
    const float turns[12] = {0, 0, 0, 1, 0, 0.7071068f, 0, 0.7071068f, 0, 1, 0, 0};
    const float sizes[9] = {1, 1, 1, 2, 2, 2, 1, 1, 1};
    const float weights[6] = {0, 0, 1, 0, 0, 1};
    if (m0 < 0 || m1 < 0 || n0 < 0 || n1 < 0 || a < 0 || xgGltfAddChannel(g, a, root, 't', times, moves, 3) < 0 ||
        xgGltfAddChannel(g, a, n0, 'r', times, turns, 3) < 0 || xgGltfAddChannel(g, a, n1, 's', times, sizes, 3) < 0 ||
        xgGltfAddWeightsChannel(g, a, n1, times, weights, 3, 2) < 0 ||
        xgGltfAddStepChannel(g, a, n0, 's', times, sizes, 3) < 0) {
        fprintf(stderr, "gltf: could not build the scene\n");
        failed++;
    }
    const float badTimes[3] = {0, 0, 2};
    const float badWeights[6] = {0, 0, NAN, 0, 0, 1};
    if (xgGltfAddMorphTarget(g, m1, 0, delta0, 2) >= 0 ||
        xgGltfSetMorphWeights(g, m1, initial, 1) ||
        xgGltfAddWeightsChannel(g, a, n0, times, weights, 3, 2) >= 0 ||
        xgGltfAddWeightsChannel(g, a, n1, badTimes, weights, 3, 2) >= 0 ||
        xgGltfAddWeightsChannel(g, a, n1, times, badWeights, 3, 2) >= 0) {
        fprintf(stderr, "gltf: invalid morph data accepted\n");
        failed++;
    }
    snprintf(path, sizeof(path), "%s/test.glb", dir);
    if (!xgGltfWriteGlb(g, path)) {
        fprintf(stderr, "glb: %s\n", xgWriterError());
        failed++;
    }
    xgGltfDestroy(g);
    printf("writertest: %s (%s)\n", failed ? "FAILED" : "written", dir);
    return failed;
}
