/*
 * RetroArch shader presets (.slangp) and shaders (.slang), read from their
 * files (shaderchain.h): no device here, so the parsing is tested on its own
 * (test/slangptest.c). Written from the format as libretro documents it
 * ("Slang shader specification" and the preset keys); RetroArch's code is
 * not used.
 *
 * A preset is "key = value" lines (values may be quoted, # starts a comment);
 * #reference "other.slangp" reads another preset first, whose keys this
 * file's then override. Paths are relative to the file that names them.
 * A shader is GLSL 4.50 with RetroArch's lines: #include "file" (expanded
 * here), #pragma stage vertex|fragment (the lines before the first stage go
 * to both), #pragma name, #pragma format and
 * #pragma parameter NAME "description" initial minimum maximum [step].
 */

#include "shaderchain.h"

#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 16

/* ---- small text helpers ---- */

static void fail(SlangPreset* p, const char* fmt, ...) {
    if (p->error[0] != '\0') {
        return; /* the first error says the most */
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(p->error, sizeof(p->error), fmt, ap);
    va_end(ap);
}

#include "slang_builtin.inc"

#define BUILTIN "builtin:"

static char* readFile(const char* path, size_t* size) {
    size_t n = 0;
    if (strncmp(path, BUILTIN, strlen(BUILTIN)) == 0) {
        /* one of the port's own (slang_builtin.inc) */
        for (size_t i = 0; i < sizeof(slangBuiltins) / sizeof(slangBuiltins[0]); i++) {
            if (strcmp(path + strlen(BUILTIN), slangBuiltins[i].name) == 0) {
                n = strlen(slangBuiltins[i].text);
                char* text = malloc(n + 1);
                if (text != NULL) {
                    memcpy(text, slangBuiltins[i].text, n + 1);
                }
                if (size != NULL) {
                    *size = n;
                }
                return text;
            }
        }
        return NULL;
    }
    char* data = SDL_LoadFile(path, &n);
    if (data == NULL) {
        return NULL;
    }
    char* text = malloc(n + 1);
    if (text != NULL) {
        memcpy(text, data, n);
        text[n] = '\0';
    }
    SDL_free(data);
    if (size != NULL) {
        *size = n;
    }
    return text;
}

/* The directory part of path, with its separator ("" for none). */
static void dirOf(const char* path, char* out, size_t cap) {
    if (strncmp(path, BUILTIN, strlen(BUILTIN)) == 0) {
        snprintf(out, cap, "%s", BUILTIN); /* the built-in presets name each other */
        return;
    }
    const char* slash = strrchr(path, '/');
    const char* back = strrchr(path, '\\');
    if (back != NULL && (slash == NULL || back > slash)) {
        slash = back;
    }
    size_t n = slash != NULL ? (size_t)(slash - path + 1) : 0;
    n = n < cap - 1 ? n : cap - 1;
    memcpy(out, path, n);
    out[n] = '\0';
}

static int absolutePath(const char* p) {
    return p[0] == '/' || p[0] == '\\' || (isalpha((unsigned char)p[0]) && p[1] == ':');
}

/* dir + rel (rel alone when absolute), cut at cap. */
static void joinPath(const char* dir, const char* rel, char* out, size_t cap) {
    size_t n = 0;
    if (!absolutePath(rel)) {
        size_t d = strlen(dir);
        n = d < cap - 1 ? d : cap - 1;
        memcpy(out, dir, n);
    }
    size_t r = strlen(rel);
    r = r < cap - 1 - n ? r : cap - 1 - n;
    memcpy(out + n, rel, r);
    out[n + r] = '\0';
}

static char* trim(char* s) {
    while (isspace((unsigned char)*s)) {
        s++;
    }
    char* e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) {
        *--e = '\0';
    }
    return s;
}

/* A value without its quotes. */
static char* unquote(char* s) {
    s = trim(s);
    size_t n = strlen(s);
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
        s[n - 1] = '\0';
        s++;
    }
    return s;
}

/* A preset value: the text inside its quotes, or up to a # comment. */
static char* presetValue(char* s) {
    s = trim(s);
    if (s[0] == '"') {
        char* end = strchr(s + 1, '"');
        if (end != NULL) {
            *end = '\0';
        }
        return s + 1;
    }
    char* hash = strchr(s, '#');
    if (hash != NULL) {
        *hash = '\0';
    }
    return trim(s);
}

static int truthy(const char* v) {
    return strcmp(v, "true") == 0 || strcmp(v, "1") == 0;
}

/* ---- the preset's keys ---- */

typedef struct {
    char key[96];
    char value[1024];
    char dir[1024]; /* of the file that set it */
} Key;

typedef struct {
    Key* keys;
    int count, cap;
} Keys;

static void setKey(Keys* k, const char* key, const char* value, const char* dir) {
    Key* e = NULL;
    for (int i = 0; i < k->count; i++) {
        if (strcmp(k->keys[i].key, key) == 0) {
            e = &k->keys[i];
            break;
        }
    }
    if (e == NULL) {
        if (k->count == k->cap) {
            k->cap = k->cap ? k->cap * 2 : 64;
            k->keys = realloc(k->keys, (size_t)k->cap * sizeof(Key));
        }
        e = &k->keys[k->count++];
        snprintf(e->key, sizeof(e->key), "%s", key);
    }
    snprintf(e->value, sizeof(e->value), "%s", value);
    snprintf(e->dir, sizeof(e->dir), "%s", dir);
}

static const Key* findKey(const Keys* k, const char* key) {
    for (int i = 0; i < k->count; i++) {
        if (strcmp(k->keys[i].key, key) == 0) {
            return &k->keys[i];
        }
    }
    return NULL;
}

static const char* keyValue(const Keys* k, const char* fmt, int n) {
    char key[96];
    snprintf(key, sizeof(key), fmt, n);
    const Key* e = findKey(k, key);
    return e != NULL ? e->value : NULL;
}

static int readPreset(SlangPreset* p, Keys* k, const char* path, int depth) {
    if (depth > MAX_DEPTH) {
        fail(p, "%s: #reference nested too deeply", path);
        return 0;
    }
    char* text = readFile(path, NULL);
    if (text == NULL) {
        fail(p, "cannot read %s", path);
        return 0;
    }
    char dir[1024];
    dirOf(path, dir, sizeof(dir));
    int ok = 1;
    char* line = text;
    while (line != NULL && ok) {
        char* next = strpbrk(line, "\r\n");
        if (next != NULL) {
            *next++ = '\0';
        }
        char* s = trim(line);
        if (strncmp(s, "#reference", 10) == 0) {
            /* the referenced preset's keys first, this file's over them */
            char ref[1024];
            joinPath(dir, unquote(s + 10), ref, sizeof(ref));
            ok = readPreset(p, k, ref, depth + 1);
        } else {
            char* eq = strchr(s, '=');
            if (s[0] != '#' && s[0] != '\0' && eq != NULL) {
                *eq = '\0';
                setKey(k, trim(s), presetValue(eq + 1), dir);
            }
        }
        line = next;
    }
    free(text);
    return ok;
}

/* ---- a shader's source ---- */

typedef struct {
    char* data;
    size_t len, cap;
} Buf;

static void put(Buf* b, const char* s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        b->cap = (b->len + n + 1) * 2;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static int expand(SlangPreset* p, const char* path, Buf* out, int depth) {
    if (depth > MAX_DEPTH) {
        fail(p, "%s: #include nested too deeply", path);
        return 0;
    }
    char* text = readFile(path, NULL);
    if (text == NULL) {
        fail(p, "cannot read %s", path);
        return 0;
    }
    char dir[1024];
    dirOf(path, dir, sizeof(dir));
    int ok = 1;
    int lineNo = 0;
    char* s = text;
    while (*s != '\0' && ok) {
        char* end = strchr(s, '\n');
        size_t n = end != NULL ? (size_t)(end - s) : strlen(s);
        lineNo++;
        const char* t = s;
        while (t < s + n && (*t == ' ' || *t == '\t')) {
            t++;
        }
        if (strncmp(t, "#include", 8) == 0) {
            char name[1024];
            const char* q1 = memchr(t, '"', (size_t)(s + n - t));
            const char* q2 = q1 != NULL ? memchr(q1 + 1, '"', (size_t)(s + n - q1 - 1)) : NULL;
            if (q2 == NULL) {
                fail(p, "%s: #include without a quoted file name", path);
                ok = 0;
                break;
            }
            char rel[1024];
            snprintf(rel, sizeof(rel), "%.*s", (int)(q2 - q1 - 1), q1 + 1);
            joinPath(dir, rel, name, sizeof(name));
            /* compile errors name the file and line (loadShader turns on
             * GL_GOOGLE_cpp_style_line_directive) */
            char mark[1200];
            int m = snprintf(mark, sizeof(mark), "#line 1 \"%s\"\n", name);
            put(out, mark, (size_t)m);
            ok = expand(p, name, out, depth + 1);
            m = snprintf(mark, sizeof(mark), "#line %d \"%s\"\n", lineNo + 1, path);
            put(out, mark, (size_t)m);
        } else {
            put(out, s, n);
            put(out, "\n", 1);
        }
        s = end != NULL ? end + 1 : s + n;
    }
    free(text);
    return ok;
}

static SlangParam* findParam(SlangPreset* p, const char* name) {
    for (int i = 0; i < p->params; i++) {
        if (strcmp(p->param[i].name, name) == 0) {
            return &p->param[i];
        }
    }
    return NULL;
}

/* #pragma parameter NAME "description" initial minimum maximum [step] */
static void pragmaParameter(SlangPreset* p, const char* line) {
    char name[SLANG_NAME] = {0}, desc[128] = {0};
    const char* s = line;
    while (isspace((unsigned char)*s)) {
        s++;
    }
    int n = 0;
    while (*s != '\0' && !isspace((unsigned char)*s) && n < SLANG_NAME - 1) {
        name[n++] = *s++;
    }
    const char* q1 = strchr(s, '"');
    const char* q2 = q1 != NULL ? strchr(q1 + 1, '"') : NULL;
    if (q2 == NULL) {
        return;
    }
    snprintf(desc, sizeof(desc), "%.*s", (int)(q2 - q1 - 1), q1 + 1);
    float v[4] = {0, 0, 1, 0};
    int got = sscanf(q2 + 1, "%f %f %f %f", &v[0], &v[1], &v[2], &v[3]);
    if (got < 3 || name[0] == '\0') {
        return;
    }
    SlangParam* e = findParam(p, name);
    if (e != NULL) {
        return; /* a parameter several passes declare is one */
    }
    if (p->params == SLANG_MAX_PARAMS) {
        return;
    }
    e = &p->param[p->params++];
    snprintf(e->name, sizeof(e->name), "%s", name);
    snprintf(e->desc, sizeof(e->desc), "%s", desc);
    e->def = e->value = v[0];
    e->min = v[1];
    e->max = v[2];
    e->step = got >= 4 ? v[3] : 0;
}

/* The shader's text split into its stages, the pass's #pragma name/format
 * taken, the parameters added to the preset. */
static int loadShader(SlangPreset* p, SlangPass* pass) {
    Buf all = {0};
    if (!expand(p, pass->path, &all, 0)) {
        free(all.data);
        return 0;
    }
    if (all.data == NULL || strstr(all.data, "#version") == NULL) {
        /* RetroArch's .slang shaders are GLSL, #version 450 first; a .slang
         * file without one is taken for the Slang shading language. */
        fail(p, "%s: no #version: a Slang-language shader? This build reads RetroArch's GLSL .slang shaders only "
                "(no Slang compiler)", pass->path);
        free(all.data);
        return 0;
    }
    Buf stage[2] = {{0}, {0}};
    int current = -1; /* -1: both */
    char* s = all.data;
    while (s != NULL && *s != '\0') {
        char* end = strchr(s, '\n');
        size_t n = end != NULL ? (size_t)(end - s) : strlen(s);
        char line[2048];
        snprintf(line, sizeof(line), "%.*s", (int)(n < sizeof(line) - 1 ? n : sizeof(line) - 1), s);
        char* t = trim(line);
        int keep = 1;
        if (strncmp(t, "#pragma", 7) == 0) {
            char* w = trim(t + 7);
            if (strncmp(w, "stage", 5) == 0) {
                char* st = trim(w + 5);
                current = strncmp(st, "vertex", 6) == 0 ? 0 : strncmp(st, "fragment", 8) == 0 ? 1 : -2;
                if (current == -2) {
                    fail(p, "%s: unknown #pragma stage %s", pass->path, st);
                    free(all.data);
                    free(stage[0].data);
                    free(stage[1].data);
                    return 0;
                }
                keep = 0;
            } else if (strncmp(w, "name", 4) == 0 && isspace((unsigned char)w[4])) {
                if (pass->alias[0] == '\0') {
                    snprintf(pass->alias, sizeof(pass->alias), "%s", trim(w + 4));
                }
                keep = 0;
            } else if (strncmp(w, "format", 6) == 0) {
                snprintf(pass->format, sizeof(pass->format), "%s", trim(w + 6));
                keep = 0;
            } else if (strncmp(w, "parameter", 9) == 0) {
                pragmaParameter(p, w + 9);
                keep = 0;
            }
        }
        /* A removed line, or one of the other stage, stays as an empty
         * one, and #line marks go to both: compile errors keep the files'
         * line numbers. */
        int mark = strncmp(t, "#line", 5) == 0;
        for (int k = 0; k < 2; k++) {
            if (keep && (mark || current == -1 || current == k)) {
                put(&stage[k], s, n);
            }
            put(&stage[k], "\n", 1);
        }
        s = end != NULL ? end + 1 : NULL;
    }
    free(all.data);
    if (stage[0].data == NULL || stage[1].data == NULL || strstr(stage[0].data, "void main") == NULL ||
        strstr(stage[1].data, "void main") == NULL) {
        fail(p, "%s: no #pragma stage vertex and fragment, each with main()", pass->path);
        free(stage[0].data);
        free(stage[1].data);
        return 0;
    }
    for (int k = 0; k < 2; k++) {
        /* #line with file names: the extension, after #version */
        char* v = strstr(stage[k].data, "#version");
        char* eol = v != NULL ? strchr(v, '\n') : NULL;
        if (eol != NULL) {
            int line = 1;
            for (const char* c = stage[k].data; c < v; c++) {
                line += *c == '\n';
            }
            char ext[1200];
            int m = snprintf(ext, sizeof(ext), "#extension GL_GOOGLE_cpp_style_line_directive : require\n#line %d \"%s\"\n",
                             line + 1, pass->path);
            Buf b = {0};
            put(&b, stage[k].data, (size_t)(eol + 1 - stage[k].data));
            put(&b, ext, (size_t)m);
            put(&b, eol + 1, strlen(eol + 1));
            free(stage[k].data);
            stage[k] = b;
        }
    }
    pass->vertex = stage[0].data;
    pass->fragment = stage[1].data;
    return 1;
}

/* ---- the preset ---- */

static int parseWrap(const char* v) {
    if (v == NULL) {
        return SLANG_WRAP_BORDER; /* the format's default */
    }
    return strcmp(v, "clamp_to_edge") == 0 ? SLANG_WRAP_EDGE
           : strcmp(v, "repeat") == 0      ? SLANG_WRAP_REPEAT
           : strcmp(v, "mirrored_repeat") == 0 ? SLANG_WRAP_MIRROR
                                               : SLANG_WRAP_BORDER;
}

static int parseScaleType(const char* v) {
    return strcmp(v, "viewport") == 0   ? SLANG_SCALE_VIEWPORT
           : strcmp(v, "absolute") == 0 ? SLANG_SCALE_ABSOLUTE
           : strcmp(v, "original") == 0 ? SLANG_SCALE_ORIGINAL
                                        : SLANG_SCALE_SOURCE;
}

static int endsWith(const char* s, const char* tail) {
    size_t a = strlen(s), b = strlen(tail);
    return a >= b && SDL_strcasecmp(s + a - b, tail) == 0;
}

static void passDefaults(SlangPass* pass) {
    memset(pass, 0, sizeof(*pass));
    pass->filter = -1;
    pass->wrap = SLANG_WRAP_BORDER;
    pass->scaleType[0] = pass->scaleType[1] = SLANG_SCALE_SOURCE;
    pass->scale[0] = pass->scale[1] = 1.0f;
}

int slangLoad(SlangPreset* p, const char* path) {
    memset(p, 0, sizeof(*p));
    if (path == NULL || path[0] == '\0') {
        fail(p, "no preset");
        return 0;
    }
    if (endsWith(path, ".slang")) {
        /* One shader: one pass at the window's size. */
        p->passes = 1;
        passDefaults(&p->pass[0]);
        snprintf(p->pass[0].path, sizeof(p->pass[0].path), "%s", path);
        if (!loadShader(p, &p->pass[0])) {
            slangFree(p);
            return 0;
        }
        return 1;
    }
    if (!endsWith(path, ".slangp")) {
        fail(p, "%s: not a .slangp preset or .slang shader (.glslp and .cgp are other formats)", path);
        return 0;
    }
    Keys k = {0};
    if (!readPreset(p, &k, path, 0)) {
        free(k.keys);
        return 0;
    }
    const Key* count = findKey(&k, "shaders");
    p->passes = count != NULL ? atoi(count->value) : 0;
    if (p->passes <= 0 || p->passes > SLANG_MAX_PASSES) {
        fail(p, "%s: \"shaders\" is %s (1 to %d passes)", path, count != NULL ? count->value : "missing",
             SLANG_MAX_PASSES);
        free(k.keys);
        p->passes = 0;
        return 0;
    }
    for (int i = 0; i < p->passes; i++) {
        SlangPass* pass = &p->pass[i];
        passDefaults(pass);
        char key[96];
        snprintf(key, sizeof(key), "shader%d", i);
        const Key* sh = findKey(&k, key);
        if (sh == NULL) {
            fail(p, "%s: no %s", path, key);
            break;
        }
        joinPath(sh->dir, sh->value, pass->path, sizeof(pass->path));
        const char* v;
        if ((v = keyValue(&k, "alias%d", i)) != NULL) {
            snprintf(pass->alias, sizeof(pass->alias), "%.*s", (int)sizeof(pass->alias) - 1, v);
        }
        if ((v = keyValue(&k, "filter_linear%d", i)) != NULL) {
            pass->filter = truthy(v);
        }
        pass->wrap = parseWrap(keyValue(&k, "wrap_mode%d", i));
        if ((v = keyValue(&k, "mipmap_input%d", i)) != NULL) {
            pass->mipmap = truthy(v);
        }
        if ((v = keyValue(&k, "float_framebuffer%d", i)) != NULL) {
            pass->floatFb = truthy(v);
        }
        if ((v = keyValue(&k, "srgb_framebuffer%d", i)) != NULL) {
            pass->srgbFb = truthy(v);
        }
        if ((v = keyValue(&k, "frame_count_mod%d", i)) != NULL) {
            pass->frameCountMod = atoi(v);
        }
        const char* both = keyValue(&k, "scale_type%d", i);
        const char* tx = keyValue(&k, "scale_type_x%d", i);
        const char* ty = keyValue(&k, "scale_type_y%d", i);
        if (both != NULL || tx != NULL || ty != NULL) {
            pass->scaleSet = 1;
            pass->scaleType[0] = parseScaleType(tx != NULL ? tx : both != NULL ? both : "source");
            pass->scaleType[1] = parseScaleType(ty != NULL ? ty : both != NULL ? both : "source");
            const char* s = keyValue(&k, "scale%d", i);
            const char* sx = keyValue(&k, "scale_x%d", i);
            const char* sy = keyValue(&k, "scale_y%d", i);
            pass->scale[0] = sx != NULL ? (float)atof(sx) : s != NULL ? (float)atof(s) : 1.0f;
            pass->scale[1] = sy != NULL ? (float)atof(sy) : s != NULL ? (float)atof(s) : 1.0f;
        }
        if (!loadShader(p, pass)) {
            break;
        }
    }
    const Key* textures = findKey(&k, "textures");
    if (p->error[0] == '\0' && textures != NULL && textures->value[0] != '\0') {
        char list[1024];
        snprintf(list, sizeof(list), "%s", textures->value);
        for (char* name = strtok(list, ";"); name != NULL; name = strtok(NULL, ";")) {
            name = trim(name);
            if (name[0] == '\0') {
                continue;
            }
            if (p->textures == SLANG_MAX_TEXTURES) {
                fail(p, "%s: more than %d textures", path, SLANG_MAX_TEXTURES);
                break;
            }
            SlangTexture* t = &p->texture[p->textures];
            snprintf(t->name, sizeof(t->name), "%s", name);
            const Key* file = findKey(&k, name);
            if (file == NULL) {
                fail(p, "%s: texture %s has no file", path, name);
                break;
            }
            joinPath(file->dir, file->value, t->path, sizeof(t->path));
            char key[160];
            snprintf(key, sizeof(key), "%s_linear", name);
            const Key* e = findKey(&k, key);
            t->linear = e != NULL ? truthy(e->value) : 0;
            snprintf(key, sizeof(key), "%s_wrap_mode", name);
            e = findKey(&k, key);
            t->wrap = parseWrap(e != NULL ? e->value : NULL);
            snprintf(key, sizeof(key), "%s_mipmap", name);
            e = findKey(&k, key);
            t->mipmap = e != NULL ? truthy(e->value) : 0;
            p->textures++;
        }
    }
    /* The preset's values for the shaders' parameters ("parameters" lists
     * them; a parameter's key may also appear on its own). */
    for (int i = 0; i < p->params; i++) {
        const Key* e = findKey(&k, p->param[i].name);
        if (e != NULL) {
            p->param[i].value = p->param[i].def = (float)atof(e->value);
        }
    }
    free(k.keys);
    if (p->error[0] != '\0') {
        slangFree(p);
        return 0;
    }
    return 1;
}

void slangFree(SlangPreset* p) {
    for (int i = 0; i < SLANG_MAX_PASSES; i++) {
        free(p->pass[i].vertex);
        free(p->pass[i].fragment);
        p->pass[i].vertex = p->pass[i].fragment = NULL;
    }
    p->passes = 0;
}

void slangApplyParams(SlangPreset* p, const char* text) {
    if (text == NULL) {
        return;
    }
    char buf[4096];
    snprintf(buf, sizeof(buf), "%s", text);
    for (char* item = strtok(buf, ";"); item != NULL; item = strtok(NULL, ";")) {
        char* eq = strchr(item, '=');
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        SlangParam* e = findParam(p, trim(item));
        if (e != NULL) {
            float v = (float)atof(eq + 1);
            e->value = v < e->min ? e->min : v > e->max ? e->max : v;
        }
    }
}

void slangParamsText(const SlangPreset* p, char* out, size_t cap) {
    size_t n = 0;
    out[0] = '\0';
    for (int i = 0; i < p->params && n + 1 < cap; i++) {
        const SlangParam* e = &p->param[i];
        if (e->value != e->def) {
            int w = snprintf(out + n, cap - n, "%s%s=%g", n ? ";" : "", e->name, (double)e->value);
            if (w < 0 || (size_t)w >= cap - n) {
                out[n] = '\0';
                break;
            }
            n += (size_t)w;
        }
    }
}
