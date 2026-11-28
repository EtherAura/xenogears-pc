// The display copy has 24 colour bits, mask and coverage. Its remaining
// six bits carry the strength of an explicitly emissive pixel.
const uint HDR_SHIFT = 26u;
const uint HDR_MASK = 63u << HDR_SHIFT;

uint hdrEmission(float strength) {
    return uint(clamp(strength, 0.0, 1.0) * 63.0 + 0.5) << HDR_SHIFT;
}

float hdrStrength(uint pixel) {
    return float(pixel >> HDR_SHIFT) / 63.0;
}

float hdrPeak(vec3 color) {
    return max(color.r, max(color.g, color.b));
}
