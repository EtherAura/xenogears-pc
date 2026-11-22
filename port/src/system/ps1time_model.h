/* The PS1 time model's costs (xg-wv9t), in CPU cycles (33.8688 MHz). Written by
 * tools/ps1time_fit.py from Beetle PSX VSync logs (tools/beetle.py --vsync-log) and the
 * port's replays of them (XG_PS1TIME_LOG); do not edit by hand. Fitted on 17 runs:
 *   b-d2, b-menu9, b-ng, b-s01, b-s10, b-s11, b-s12, b-s2, b-s3, b-s4, b-s5, b-s6, b-s7, b-s8, b-s9, b-wmb, b-wmw
 */
#define PS1TIME_MODEL_SAFE_POINT 213.617 /* per platform call (a safe point) */
#define PS1TIME_MODEL_OTHER 55.863 /* per GTE command of a function not listed */
#define PS1TIME_MODEL_PRIM 243.888 /* per primitive linked into an ordering table */
/* per stretch of work that ends at a VSync call, by game mode (bootGame's 0-7) */
static const double ps1timeModelMode[8] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
/* per GTE command run by the function (include/gte_pc.h's __func__; "-": LIBGTE's own),
 * sorted by name (strcmp) */
static const struct {
    const char* fn;
    double cycles;
} ps1timeModelFn[] = {
    {"-", 0.000},
    {"FT3_FAKE", 57.459},
    {"GT3_FAKE", 57.831},
    {"NormalClip", 0.000},
    {"RotAverage4", 69.368},
    {"RotTransPers", 0.000},
    {"RotTransPers4", 209.613},
    {"drawBattleHorizonBackdrop", 100.589},
    {"isProjectedTriangleOnScreen", 338.704},
    {"normalize", 539.875},
    {"prim5_5", 76.985},
    {"primD_1", 0.000},
    {"quadsAverage", 57.060},
    {"quadsDepth", 57.209},
    {"quadsFog", 7.529},
    {"renderCharShadows", 447.559},
    {"renderFieldCharacterSprites", 256.428},
    {"renderObjects", 0.000},
    {"trisAverage", 31.684},
    {"trisDepth", 33.066},
};
/* the GPU, per queued job: a base, per primitive and per pixel by kind (ps1time.h's
 * PT_ order), per semi-transparent and per shaded textured pixel */
#define PS1TIME_MODEL_GPU_JOB 0.0
static const double ps1timeModelGpuCount[13] = {0};
static const double ps1timeModelGpuPixel[13] = {0};
#define PS1TIME_MODEL_GPU_SEMI 0.0
#define PS1TIME_MODEL_GPU_SHADED 0.0
