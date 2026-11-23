#ifndef PORT_SYSTEM_UPDATE_MANIFEST_H
#define PORT_SYSTEM_UPDATE_MANIFEST_H

/* A release's update manifest and its signature (xg-4oq.14.2): what the in-app
 * updater reads to learn of the latest release and to check what it downloads.
 *
 * Every release carries two assets under fixed names, so that
 * https://github.com/EtherAura/xenogears-pc/releases/latest/download/<name>
 * always gives the latest release's: xenogears-pc-update.txt and
 * xenogears-pc-update.txt.sig. packaging/update_manifest.py makes and signs
 * them. The manifest is key=value lines (release.h's form; # starts a comment;
 * keys of later formats are passed over):
 *
 *   format=1
 *   version=2026.11.27
 *   page=https://github.com/EtherAura/xenogears-pc/releases/tag/v2026.11.27
 *   download=https://github.com/EtherAura/xenogears-pc/releases/download/v2026.11.27/
 *   updates=off
 *   asset=tarball linux-x86_64 17360884 <SHA-512, 128 hex digits> xenogears-pc-2026.11.27-linux-x86_64.tar.gz
 *
 * updates is the switch: off until a release's save states load in every
 * other canonical release's, both ways (xg-4oq.14.7); while it is off the app
 * offers nothing. Each asset line is a kind (release.h's words), a platform
 * (updatePlatform), the size in bytes, the SHA-512 and the file name, which is
 * fetched from download.
 *
 * The .sig file is one line per signature, "ed25519 <key id> <signature>": the
 * key id is the public key's first 8 bytes in hex, the signature Ed25519
 * (RFC 8032) over the manifest's exact bytes, in base64. A manifest is trusted
 * when one line verifies with a key this build trusts; a release that changes
 * keys signs with the old and the new one. */

#include <stddef.h>
#include <stdint.h>

#include "system/release.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UPDATE_MANIFEST_NAME "xenogears-pc-update.txt"
#define UPDATE_SIGNATURE_NAME "xenogears-pc-update.txt.sig"
#define UPDATE_MANIFEST_MAX 65536 /* bytes; a longer file is not a manifest */
#define UPDATE_SIGNATURE_MAX 4096
#define UPDATE_ASSETS_MAX 16

typedef struct {
    ReleaseKind kind;
    char platform[32];
    uint64_t size;
    uint8_t sha512[64];
    char name[128];
} UpdateAsset;

typedef struct {
    int format;
    char version[16];
    char page[256];
    char download[256];
    int updates; /* the switch (xg-4oq.14.7) */
    int assets;
    UpdateAsset asset[UPDATE_ASSETS_MAX];
} UpdateManifest;

/* Reads a manifest's text (n bytes, no NUL needed). 1 when it is one; 0 with a reason in why. */
int updateManifestParse(const char* text, size_t n, UpdateManifest* m, char* why, size_t whySize);
/* The asset for a kind and platform, or NULL. */
const UpdateAsset* updateManifestAsset(const UpdateManifest* m, ReleaseKind kind, const char* platform);

/* 1 when one line of sig (sigSize bytes) is a valid Ed25519 signature of text by one of keys; 0 with a
 * reason. updateSignatureTrusted checks against the keys this build trusts. */
int updateSignatureCheck(const uint8_t* text, size_t n, const char* sig, size_t sigSize,
                         const uint8_t (*keys)[32], int keyCount, char* why, size_t whySize);
int updateSignatureTrusted(const uint8_t* text, size_t n, const char* sig, size_t sigSize, char* why,
                           size_t whySize);
/* How many keys this build trusts (0 until the release key exists: then nothing is trusted). */
int updateTrustedKeyCount(void);

/* This build's platform as asset lines name it: linux-x86_64, windows-x64, android-arm64-v8a,
 * android-x86_64; "" elsewhere. */
const char* updatePlatform(void);
/* Release versions compared (both releaseVersionValid): <0, 0, >0. */
int updateVersionCompare(const char* a, const char* b);

#ifdef __cplusplus
}
#endif
#endif
