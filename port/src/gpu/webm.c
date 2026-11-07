/*
 * The video track of a WebM/Matroska file (webm.h). EBML (RFC 8794): each
 * element is an ID (a variable-length integer keeping its length marker), a
 * data size (a variable-length integer without it; all ones: unknown) and
 * the data. Matroska (RFC 9559): the EBML header, then a Segment holding
 * Info (TimestampScale), Tracks (TrackEntry: TrackNumber, TrackType 1 =
 * video, CodecID, CodecPrivate, DefaultDuration, Video: PixelWidth and
 * PixelHeight) and Clusters (Timestamp, then SimpleBlocks, or BlockGroups
 * with a Block and, for frames that are not keyframes, a ReferenceBlock).
 * A block: its track number (a variable-length integer), a signed 16-bit
 * timestamp relative to its cluster's, a flags byte (SimpleBlock bit 7:
 * keyframe; bits 1-2: lacing, not taken here), the frame.
 */

#include "webm.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    ID_EBML = 0x1A45DFA3, ID_DOCTYPE = 0x4282, ID_SEGMENT = 0x18538067, ID_INFO = 0x1549A966,
    ID_TIMESTAMP_SCALE = 0x2AD7B1, ID_TRACKS = 0x1654AE6B, ID_TRACK_ENTRY = 0xAE, ID_TRACK_NUMBER = 0xD7,
    ID_TRACK_TYPE = 0x83, ID_CODEC_ID = 0x86, ID_CODEC_PRIVATE = 0x63A2, ID_DEFAULT_DURATION = 0x23E383,
    ID_VIDEO = 0xE0, ID_PIXEL_WIDTH = 0xB0, ID_PIXEL_HEIGHT = 0xBA, ID_CONTENT_ENCODINGS = 0x6D80,
    ID_CLUSTER = 0x1F43B675, ID_CLUSTER_TIMESTAMP = 0xE7, ID_SIMPLE_BLOCK = 0xA3, ID_BLOCK_GROUP = 0xA0,
    ID_BLOCK = 0xA1, ID_REFERENCE_BLOCK = 0xFB, ID_CUES = 0x1C53BB6B, ID_TAGS = 0x1254C367,
    ID_CHAPTERS = 0x1043A770, ID_ATTACHMENTS = 0x1941A469, ID_SEEK_HEAD = 0x114D9B74,
};

typedef struct {
    uint32_t id;
    uint64_t size;
    int unknown;   /* size unknown (all ones) */
    int64_t start; /* of the data */
} Element;

typedef struct {
    int track;
    WebmFrame f;
} Found;

typedef struct {
    SDL_IOStream* io;
    int64_t fileSize;
    char* err;
    size_t errCap;
    uint64_t scale;
    Found* found;
    int count, cap;
} Parser;

static int fail(Parser* p, const char* what) {
    if (p->err[0] == '\0') {
        snprintf(p->err, p->errCap, "%s", what);
    }
    return 0;
}

/* A variable-length integer: with its marker (IDs) or without (sizes);
 * *len its bytes, *ones whether every value bit is 1. */
static int vint(SDL_IOStream* io, int marker, uint64_t* value, int* len, int* ones) {
    uint8_t b;
    if (SDL_ReadIO(io, &b, 1) != 1) {
        return 0;
    }
    int n = 1;
    uint8_t mask = 0x80;
    while (n <= 8 && !(b & mask)) {
        mask >>= 1;
        n++;
    }
    if (n > 8) {
        return 0;
    }
    uint64_t v = marker ? b : (uint64_t)(b & (mask - 1));
    int allOnes = (b & (mask - 1)) == (uint8_t)(mask - 1);
    for (int i = 1; i < n; i++) {
        uint8_t c;
        if (SDL_ReadIO(io, &c, 1) != 1) {
            return 0;
        }
        v = v << 8 | c;
        allOnes &= c == 0xFF;
    }
    *value = v;
    if (len != NULL) *len = n;
    if (ones != NULL) *ones = allOnes;
    return 1;
}

static int element(Parser* p, Element* e) {
    uint64_t id, size;
    int idLen, ones;
    if (!vint(p->io, 1, &id, &idLen, NULL) || idLen > 4 || !vint(p->io, 0, &size, NULL, &ones)) {
        return 0;
    }
    e->id = (uint32_t)id;
    e->size = size;
    e->unknown = ones;
    e->start = SDL_TellIO(p->io);
    return 1;
}

static int64_t endOf(Parser* p, const Element* e, int64_t parentEnd) {
    if (e->unknown) {
        return parentEnd;
    }
    int64_t end = e->start + (int64_t)e->size;
    return end > parentEnd ? parentEnd : end;
}

static uint64_t readUint(Parser* p, const Element* e) {
    uint64_t v = 0;
    for (uint64_t i = 0; i < e->size && i < 8; i++) {
        uint8_t c = 0;
        SDL_ReadIO(p->io, &c, 1);
        v = v << 8 | c;
    }
    return v;
}

static int isTopLevel(uint32_t id) {
    return id == ID_CLUSTER || id == ID_CUES || id == ID_TAGS || id == ID_CHAPTERS || id == ID_ATTACHMENTS ||
           id == ID_INFO || id == ID_TRACKS || id == ID_SEEK_HEAD;
}

static int addFrame(Parser* p, int track, uint64_t offset, uint32_t size, int key, int64_t timeNs) {
    if (p->count == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 1024;
        Found* n = realloc(p->found, (size_t)p->cap * sizeof(Found));
        if (n == NULL) {
            return fail(p, "out of memory indexing frames");
        }
        p->found = n;
    }
    Found* f = &p->found[p->count++];
    f->track = track;
    f->f.offset = offset;
    f->f.size = size;
    f->f.key = key;
    f->f.timeNs = timeNs;
    return 1;
}

/* A block's header at the element's start: track, time, flags; its frame. */
static int block(Parser* p, const Element* e, int64_t clusterTime, int simple, int key) {
    uint64_t track;
    int len;
    uint8_t rest[3];
    if (!vint(p->io, 0, &track, &len, NULL) || SDL_ReadIO(p->io, rest, 3) != 3 || e->size < (uint64_t)len + 3) {
        return fail(p, "a block is cut short");
    }
    int16_t rel = (int16_t)(rest[0] << 8 | rest[1]);
    uint8_t flags = rest[2];
    if (flags & 0x06) {
        return fail(p, "laced blocks are not supported");
    }
    uint64_t size = e->size - (uint64_t)len - 3;
    if (size > 0xFFFFFFFFu) {
        return fail(p, "a block is too large");
    }
    int isKey = simple ? (flags & 0x80) != 0 : key;
    return addFrame(p, (int)track, (uint64_t)SDL_TellIO(p->io), (uint32_t)size, isKey,
                    (clusterTime + rel) * (int64_t)p->scale);
}

static int blockGroup(Parser* p, const Element* g, int64_t clusterTime) {
    int64_t end = endOf(p, g, p->fileSize);
    Element blk = {0};
    int haveBlock = 0, key = 1;
    while (SDL_TellIO(p->io) < end) {
        Element e;
        if (!element(p, &e)) {
            return fail(p, "a block group is cut short");
        }
        if (e.id == ID_BLOCK) {
            blk = e;
            haveBlock = 1;
        } else if (e.id == ID_REFERENCE_BLOCK) {
            key = 0;
        }
        SDL_SeekIO(p->io, endOf(p, &e, end), SDL_IO_SEEK_SET);
    }
    if (!haveBlock) {
        return 1;
    }
    SDL_SeekIO(p->io, blk.start, SDL_IO_SEEK_SET);
    int ok = block(p, &blk, clusterTime, 0, key);
    SDL_SeekIO(p->io, end, SDL_IO_SEEK_SET);
    return ok;
}

/* A cluster's blocks; with an unknown size it ends at the next top-level element. */
static int cluster(Parser* p, const Element* c, int64_t segEnd) {
    int64_t end = endOf(p, c, segEnd);
    int64_t clusterTime = 0;
    while (SDL_TellIO(p->io) < end) {
        int64_t at = SDL_TellIO(p->io);
        Element e;
        if (!element(p, &e)) {
            return c->unknown ? 1 : fail(p, "a cluster is cut short");
        }
        if (c->unknown && isTopLevel(e.id)) {
            SDL_SeekIO(p->io, at, SDL_IO_SEEK_SET);
            return 1;
        }
        int64_t eEnd = endOf(p, &e, end);
        if (e.id == ID_CLUSTER_TIMESTAMP) {
            clusterTime = (int64_t)readUint(p, &e);
        } else if (e.id == ID_SIMPLE_BLOCK) {
            if (!block(p, &e, clusterTime, 1, 0)) {
                return 0;
            }
        } else if (e.id == ID_BLOCK_GROUP) {
            if (!blockGroup(p, &e, clusterTime)) {
                return 0;
            }
        }
        if (e.unknown && e.id != ID_BLOCK_GROUP) {
            return fail(p, "an element of unknown size inside a cluster");
        }
        SDL_SeekIO(p->io, eEnd, SDL_IO_SEEK_SET);
    }
    return 1;
}

static int tracks(Parser* p, Webm* w, const Element* t) {
    int64_t end = endOf(p, t, p->fileSize);
    while (SDL_TellIO(p->io) < end) {
        Element e;
        if (!element(p, &e)) {
            return fail(p, "the track list is cut short");
        }
        int64_t eEnd = endOf(p, &e, end);
        if (e.id == ID_TRACK_ENTRY && w->track == 0) {
            int number = 0, type = 0, width = 0, height = 0, encoded = 0;
            char codec[32] = {0};
            uint8_t* priv = NULL;
            size_t privSize = 0;
            int64_t duration = 0;
            while (SDL_TellIO(p->io) < eEnd) {
                Element c;
                if (!element(p, &c)) {
                    free(priv);
                    return fail(p, "a track entry is cut short");
                }
                int64_t cEnd = endOf(p, &c, eEnd);
                switch (c.id) {
                case ID_TRACK_NUMBER: number = (int)readUint(p, &c); break;
                case ID_TRACK_TYPE: type = (int)readUint(p, &c); break;
                case ID_DEFAULT_DURATION: duration = (int64_t)readUint(p, &c); break;
                case ID_CONTENT_ENCODINGS: encoded = 1; break;
                case ID_CODEC_ID: {
                    size_t n = c.size < sizeof(codec) - 1 ? (size_t)c.size : sizeof(codec) - 1;
                    SDL_ReadIO(p->io, codec, n);
                    codec[n] = '\0';
                    break;
                }
                case ID_CODEC_PRIVATE:
                    if (c.size > 0 && c.size < (1u << 20)) {
                        free(priv);
                        priv = malloc((size_t)c.size);
                        privSize = priv != NULL ? SDL_ReadIO(p->io, priv, (size_t)c.size) : 0;
                    }
                    break;
                case ID_VIDEO: {
                    while (SDL_TellIO(p->io) < cEnd) {
                        Element v;
                        if (!element(p, &v)) break;
                        int64_t vEnd = endOf(p, &v, cEnd);
                        if (v.id == ID_PIXEL_WIDTH) width = (int)readUint(p, &v);
                        if (v.id == ID_PIXEL_HEIGHT) height = (int)readUint(p, &v);
                        SDL_SeekIO(p->io, vEnd, SDL_IO_SEEK_SET);
                    }
                    break;
                }
                default: break;
                }
                SDL_SeekIO(p->io, cEnd, SDL_IO_SEEK_SET);
            }
            if (type == 1 && number > 0) {
                if (encoded) {
                    free(priv);
                    return fail(p, "the video track is compressed or encrypted (ContentEncodings)");
                }
                w->track = number;
                w->width = width;
                w->height = height;
                w->defaultDurationNs = duration;
                snprintf(w->codec, sizeof(w->codec), "%s", codec);
                w->codecPrivate = priv;
                w->codecPrivateSize = privSize;
                priv = NULL;
            }
            free(priv);
        }
        SDL_SeekIO(p->io, eEnd, SDL_IO_SEEK_SET);
    }
    return 1;
}

int webmOpen(Webm* w, const char* path, char* err, size_t errCap) {
    memset(w, 0, sizeof(*w));
    err[0] = '\0';
    SDL_IOStream* io = SDL_IOFromFile(path, "rb");
    if (io == NULL) {
        snprintf(err, errCap, "cannot open %s", path);
        return 0;
    }
    Parser p = {io, SDL_GetIOSize(io), err, errCap, 1000000, NULL, 0, 0};
    Element e;
    int ok = element(&p, &e) && e.id == ID_EBML;
    if (ok) {
        char doc[16] = {0};
        int64_t end = endOf(&p, &e, p.fileSize);
        while (SDL_TellIO(io) < end) {
            Element c;
            if (!element(&p, &c)) break;
            if (c.id == ID_DOCTYPE) {
                size_t n = c.size < sizeof(doc) - 1 ? (size_t)c.size : sizeof(doc) - 1;
                SDL_ReadIO(io, doc, n);
            }
            SDL_SeekIO(io, endOf(&p, &c, end), SDL_IO_SEEK_SET);
        }
        ok = strcmp(doc, "webm") == 0 || strcmp(doc, "matroska") == 0;
    }
    if (!ok) {
        fail(&p, "not a WebM or Matroska file");
    }
    ok = ok && element(&p, &e) && e.id == ID_SEGMENT;
    if (!ok) {
        fail(&p, "no Segment");
    }
    int64_t segEnd = ok ? endOf(&p, &e, p.fileSize) : 0;
    while (ok && SDL_TellIO(io) < segEnd) {
        Element c;
        if (!element(&p, &c)) {
            break; /* a file cut short: the frames found so far */
        }
        int64_t cEnd = endOf(&p, &c, segEnd);
        if (c.id == ID_INFO) {
            while (SDL_TellIO(io) < cEnd) {
                Element i;
                if (!element(&p, &i)) break;
                if (i.id == ID_TIMESTAMP_SCALE) p.scale = readUint(&p, &i);
                SDL_SeekIO(io, endOf(&p, &i, cEnd), SDL_IO_SEEK_SET);
            }
        } else if (c.id == ID_TRACKS) {
            ok = tracks(&p, w, &c);
        } else if (c.id == ID_CLUSTER) {
            ok = cluster(&p, &c, segEnd);
            continue; /* where the cluster ended */
        } else if (c.unknown) {
            break;
        }
        SDL_SeekIO(io, cEnd, SDL_IO_SEEK_SET);
    }
    if (ok && w->track == 0) {
        ok = fail(&p, "no video track");
    }
    if (ok) {
        w->frame = malloc((size_t)(p.count ? p.count : 1) * sizeof(WebmFrame));
        for (int i = 0; w->frame != NULL && i < p.count; i++) {
            if (p.found[i].track == w->track) {
                w->frame[w->frames++] = p.found[i].f;
            }
        }
        if (w->frames == 0) {
            ok = fail(&p, "the video track has no frames");
        }
    }
    free(p.found);
    if (!ok) {
        free(w->frame);
        free(w->codecPrivate);
        SDL_CloseIO(io);
        memset(w, 0, sizeof(*w));
        return 0;
    }
    w->io = io;
    return 1;
}

size_t webmRead(Webm* w, int i, uint8_t** buf, size_t* cap) {
    if (w->io == NULL || i < 0 || i >= w->frames) {
        return 0;
    }
    const WebmFrame* f = &w->frame[i];
    if (*cap < f->size) {
        uint8_t* n = realloc(*buf, f->size);
        if (n == NULL) {
            return 0;
        }
        *buf = n;
        *cap = f->size;
    }
    if (SDL_SeekIO((SDL_IOStream*)w->io, (Sint64)f->offset, SDL_IO_SEEK_SET) < 0 ||
        SDL_ReadIO((SDL_IOStream*)w->io, *buf, f->size) != f->size) {
        return 0;
    }
    return f->size;
}

void webmClose(Webm* w) {
    if (w->io != NULL) {
        SDL_CloseIO((SDL_IOStream*)w->io);
    }
    free(w->frame);
    free(w->codecPrivate);
    memset(w, 0, sizeof(*w));
}
