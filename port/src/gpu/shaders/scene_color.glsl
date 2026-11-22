// The scene effects' last step (dof.comp, light.comp): a pixel they changed,
// back into the copy's colour mode. The copy is 8:8:8; in the two 15-bit
// modes (vk.c HIRES_COLOR_DITHER_1X, _DITHER_HI) everything drawn in it is
// 5-bit colour, so an effect's colour is taken to 5 bits as hires.frag takes
// a primitive the PS1 dithers: the PS1's 4x4 dither offsets (-4..3, psx-spx
// "GPU Dithering") added to the 8-bit value, then the top five bits, each
// read back as 8 bits by repeating its top three. Mode 1 picks the offset by
// the PS1 pixel, mode 2 by the copy's own pixel; ditherOrigin is the
// target's first PS1 pixel in the game's frame (smooth motion's private
// canvas, as its draws), 0 for the copy itself. True colour and raw stay
// 8-bit.

const int SCENE_DITHER[16] = int[16](-4, 0, -3, 1, 2, -2, 3, -1, -3, 1, -4, 0, 3, -1, 2, -2);

uvec3 sceneColorMode(uvec3 o, ivec2 p, int n, int mode, ivec2 ditherOrigin) {
    if (mode != 1 && mode != 2) return o;
    ivec2 dp = mode == 1 ? p / n + ditherOrigin : p + ditherOrigin * n;
    ivec3 f = clamp((ivec3(o) + SCENE_DITHER[(dp.y & 3) * 4 + (dp.x & 3)]) >> 3, 0, 31);
    return uvec3((f << 3) | (f >> 2));
}
