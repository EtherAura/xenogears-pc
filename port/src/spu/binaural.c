/* Three front-stage directions from the game's voice pans. The side
 * responses are mirror images, so filtering their sum and difference needs
 * only three convolutions for both ears together. */
#include "binaural.h"
#include "../../third_party/mit-kemar/horizontal.h"

void spuBinauralMix(SpuHeadphones* h, int enabled, int32_t dry[2], int32_t center) {
    unsigned i = (h->cursor - 1) & (BINAURAL_TAPS - 1);
    h->cursor = i;
    h->enabled = enabled;
    int32_t left = dry[0] - center, right = dry[1] - center;
    h->front[i] = h->front[i + BINAURAL_TAPS] = center;
    h->sum[i] = h->sum[i + BINAURAL_TAPS] = left + right;
    h->difference[i] = h->difference[i + BINAURAL_TAPS] = left - right;

    int64_t mid = 0, side = 0;
    for (unsigned k = 0; k < BINAURAL_TAPS; k++) {
        mid += (int64_t)h->front[i + k] * kemarFront[k] + (int64_t)h->sum[i + k] * kemarSum[k];
        side += (int64_t)h->difference[i + k] * kemarDifference[k];
    }

    /* Q15 filter coefficients and the half-sum/half-difference of the ears.
     * The 24 voices can exceed 16 bits before the game's final clamp. */
    int32_t ears[2] = {(int32_t)((mid + side) >> 16), (int32_t)((mid - side) >> 16)};
    if (enabled && h->blend < BINAURAL_FADE) h->blend++;
    if (!enabled && h->blend > 0) h->blend--;
    for (int ear = 0; ear < 2; ear++)
        dry[ear] += (int32_t)(((int64_t)(ears[ear] - dry[ear]) * h->blend) / BINAURAL_FADE);
}
