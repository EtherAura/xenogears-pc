/*
 * AV1 replacement videos (av1video.h): the frames of a WebM file's video
 * track (webm.c) through dav1d's documented API (dav1d.h: dav1d_send_data,
 * dav1d_get_picture, dav1d_flush), and its YUV pictures into RGBA with the
 * sequence header's matrix coefficients and range (Rec. ITU-R BT.601,
 * BT.709 and BT.2020 luma coefficients).
 */

#include "av1video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "webm.h"

#ifdef XG_HAVE_DAV1D
#include <dav1d/dav1d.h>

struct MovieVideo {
    Webm webm;
    Dav1dContext* dec;
    int next;       /* the next frame to send */
    Dav1dData data; /* its data not taken yet */
    uint8_t* buf;
    size_t cap;
};

int movieVideoAvailable(void) {
    return 1;
}

MovieVideo* movieVideoOpen(const char* path, char* err, size_t errCap) {
    MovieVideo* v = calloc(1, sizeof(*v));
    if (v == NULL) {
        snprintf(err, errCap, "out of memory");
        return NULL;
    }
    if (!webmOpen(&v->webm, path, err, errCap)) {
        free(v);
        return NULL;
    }
    if (strcmp(v->webm.codec, "V_AV1") != 0) {
        snprintf(err, errCap, "%s: the video is %s, not AV1 (V_AV1)", path, v->webm.codec);
        webmClose(&v->webm);
        free(v);
        return NULL;
    }
    Dav1dSettings s;
    dav1d_default_settings(&s);
    s.n_threads = 2;       /* the game, the audio and the GPU keep the rest */
    s.max_frame_delay = 1; /* a picture per frame sent */
    if (dav1d_open(&v->dec, &s) < 0) {
        snprintf(err, errCap, "%s: the AV1 decoder would not start", path);
        webmClose(&v->webm);
        free(v);
        return NULL;
    }
    return v;
}

void movieVideoClose(MovieVideo* v) {
    if (v == NULL) {
        return;
    }
    dav1d_data_unref(&v->data);
    dav1d_close(&v->dec);
    webmClose(&v->webm);
    free(v->buf);
    free(v);
}

int movieVideoFrames(const MovieVideo* v) {
    return v->webm.frames;
}

void movieVideoSize(const MovieVideo* v, int* w, int* h) {
    *w = v->webm.width;
    *h = v->webm.height;
}

int64_t movieVideoTime(const MovieVideo* v, int i) {
    return v->webm.frame[i].timeNs - v->webm.frame[0].timeNs;
}

int movieVideoFrameAt(const MovieVideo* v, int64_t t) {
    int lo = 0, hi = v->webm.frames - 1, at = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (movieVideoTime(v, mid) <= t) {
            at = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return at;
}

/* The luma coefficients (Kr, Kb) of a matrix; unknown: BT.601, what
 * ffmpeg's RGB to YUV conversion uses when nothing is set (a video tagged
 * BT.709, e.g. ffmpeg -colorspace bt709, gets BT.709). */
static void coefficients(const Dav1dPicture* p, float* kr, float* kb) {
    int m = p->seq_hdr != NULL ? (int)p->seq_hdr->mtrx : DAV1D_MC_UNKNOWN;
    if (m == DAV1D_MC_UNKNOWN) {
        m = DAV1D_MC_BT601;
    }
    switch (m) {
    case DAV1D_MC_BT470BG:
    case DAV1D_MC_BT601:
        *kr = 0.299f, *kb = 0.114f;
        break;
    case DAV1D_MC_BT2020_NCL:
    case DAV1D_MC_BT2020_CL:
        *kr = 0.2627f, *kb = 0.0593f;
        break;
    case DAV1D_MC_SMPTE240:
        *kr = 0.212f, *kb = 0.087f;
        break;
    default:
        *kr = 0.2126f, *kb = 0.0722f;
        break;
    }
}

static uint8_t clamp8(int v) {
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

static uint8_t* toRgba(const Dav1dPicture* p) {
    int w = p->p.w, h = p->p.h, hbd = p->p.bpc > 8;
    int mono = p->p.layout == DAV1D_PIXEL_LAYOUT_I400;
    int ssx = p->p.layout == DAV1D_PIXEL_LAYOUT_I420 || p->p.layout == DAV1D_PIXEL_LAYOUT_I422;
    int ssy = p->p.layout == DAV1D_PIXEL_LAYOUT_I420;
    int full = p->seq_hdr != NULL && p->seq_hdr->color_range;
    float kr, kb;
    coefficients(p, &kr, &kb);
    /* to 8-bit: Y' = (Y - y0) * ys, Cb, Cr = (C - 128) * cs (16.16 fixed point) */
    float sh = hbd ? 1.0f / (float)(1 << (p->p.bpc - 8)) : 1.0f;
    float ys = (full ? 1.0f : 255.0f / 219.0f) * sh, cs = (full ? 1.0f : 255.0f / 224.0f) * sh;
    int y0 = full ? 0 : 16 << (p->p.bpc - 8), c0 = 128 << (p->p.bpc - 8);
    int kY = (int)(ys * 65536.0f);
    int kRv = (int)(2.0f * (1.0f - kr) * cs * 65536.0f);
    int kBu = (int)(2.0f * (1.0f - kb) * cs * 65536.0f);
    int kGu = (int)(2.0f * kb * (1.0f - kb) / (1.0f - kr - kb) * cs * 65536.0f);
    int kGv = (int)(2.0f * kr * (1.0f - kr) / (1.0f - kr - kb) * cs * 65536.0f);
    uint8_t* out = malloc((size_t)w * h * 4);
    if (out == NULL) {
        return NULL;
    }
    for (int y = 0; y < h; y++) {
        const uint8_t* yr = (const uint8_t*)p->data[0] + (ptrdiff_t)y * p->stride[0];
        const uint8_t* ur = mono ? NULL : (const uint8_t*)p->data[1] + (ptrdiff_t)(y >> ssy) * p->stride[1];
        const uint8_t* vr = mono ? NULL : (const uint8_t*)p->data[2] + (ptrdiff_t)(y >> ssy) * p->stride[1];
        uint8_t* o = out + (size_t)y * w * 4;
        for (int x = 0; x < w; x++) {
            int Y, U = c0, V = c0;
            if (hbd) {
                Y = ((const uint16_t*)yr)[x];
                if (!mono) {
                    U = ((const uint16_t*)ur)[x >> ssx];
                    V = ((const uint16_t*)vr)[x >> ssx];
                }
            } else {
                Y = yr[x];
                if (!mono) {
                    U = ur[x >> ssx];
                    V = vr[x >> ssx];
                }
            }
            int yy = (Y - y0) * kY, u = U - c0, v = V - c0;
            o[0] = clamp8((yy + kRv * v + 32768) >> 16);
            o[1] = clamp8((yy - kGu * u - kGv * v + 32768) >> 16);
            o[2] = clamp8((yy + kBu * u + 32768) >> 16);
            o[3] = 255;
            o += 4;
        }
    }
    return out;
}

uint8_t* movieVideoDecode(MovieVideo* v, int i, int* w, int* h, char* err, size_t errCap) {
    if (i < 0 || i >= v->webm.frames) {
        snprintf(err, errCap, "frame %d is not in the video", i);
        return NULL;
    }
    /* out of order, or a keyframe between: start again from the keyframe before i */
    int key = i;
    while (key > 0 && !v->webm.frame[key].key) {
        key--;
    }
    if (i < v->next || key > v->next) {
        dav1d_data_unref(&v->data);
        dav1d_flush(v->dec);
        v->next = key;
    }
    for (int guard = 0; guard < 100000; guard++) {
        if (v->data.sz == 0 && v->next < v->webm.frames) {
            size_t n = webmRead(&v->webm, v->next, &v->buf, &v->cap);
            uint8_t* d = n > 0 ? dav1d_data_create(&v->data, n) : NULL;
            if (d == NULL) {
                snprintf(err, errCap, "frame %d cannot be read", v->next);
                return NULL;
            }
            memcpy(d, v->buf, n);
            v->data.m.timestamp = v->next;
            v->next++;
        }
        if (v->data.sz > 0) {
            int r = dav1d_send_data(v->dec, &v->data);
            if (r < 0 && r != DAV1D_ERR(EAGAIN)) {
                dav1d_data_unref(&v->data);
                snprintf(err, errCap, "frame %d: the AV1 data is damaged (%d)", v->next - 1, r);
                return NULL;
            }
        }
        Dav1dPicture pic;
        memset(&pic, 0, sizeof(pic));
        int r = dav1d_get_picture(v->dec, &pic);
        if (r == 0) {
            int64_t t = pic.m.timestamp;
            uint8_t* rgba = NULL;
            if (t == i) {
                rgba = toRgba(&pic);
                *w = pic.p.w;
                *h = pic.p.h;
            }
            dav1d_picture_unref(&pic);
            if (t == i) {
                if (rgba == NULL) snprintf(err, errCap, "out of memory");
                return rgba;
            }
            if (t > i) {
                break; /* passed it (a frame without a picture) */
            }
        } else if (r != DAV1D_ERR(EAGAIN)) {
            snprintf(err, errCap, "frame %d: the AV1 decoder failed (%d)", i, r);
            return NULL;
        } else if (v->data.sz == 0 && v->next >= v->webm.frames) {
            break; /* the end, drained */
        }
    }
    snprintf(err, errCap, "frame %d gave no picture", i);
    return NULL;
}

#else /* no dav1d in this build */

int movieVideoAvailable(void) {
    return 0;
}

MovieVideo* movieVideoOpen(const char* path, char* err, size_t errCap) {
    snprintf(err, errCap, "%s: this build has no AV1 decoder (dav1d)", path);
    return NULL;
}

void movieVideoClose(MovieVideo* v) {
    (void)v;
}

int movieVideoFrames(const MovieVideo* v) {
    (void)v;
    return 0;
}

void movieVideoSize(const MovieVideo* v, int* w, int* h) {
    (void)v;
    *w = *h = 0;
}

int64_t movieVideoTime(const MovieVideo* v, int i) {
    (void)v;
    (void)i;
    return 0;
}

int movieVideoFrameAt(const MovieVideo* v, int64_t t) {
    (void)v;
    (void)t;
    return -1;
}

uint8_t* movieVideoDecode(MovieVideo* v, int i, int* w, int* h, char* err, size_t errCap) {
    (void)v;
    (void)i;
    (void)w;
    (void)h;
    snprintf(err, errCap, "this build has no AV1 decoder (dav1d)");
    return NULL;
}

#endif
