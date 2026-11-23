/* The update manifest and its signature (update/update_manifest.c), disc-free:
 * Monocypher's Ed25519 on RFC 8032's test vectors 1-3; a manifest made and
 * signed by packaging/update_manifest.py with a FIXTURE key (its seed is public,
 * SHA-256 of "xenogears-pc update manifest test key: public, trusted by no
 * build"): its signature verifies, one changed byte of the manifest or the
 * signature does not, another key's id is not tried, and the build's own keys
 * (the release key) do not trust it; the manifest's fields, and manifests
 * refused; CalVer order; this build's platform name.
 *
 *     updatemanifesttest [MANIFEST SIGNATURE]
 *
 * Given a release's two files (xenogears-pc-update.txt and its .sig), it also
 * checks them as the app would: signed by a key this build trusts, and a
 * manifest it accepts. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "monocypher-ed25519.h"
#include "update/update_manifest.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void hex(const char* s, uint8_t* out, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        sscanf(s + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

/* RFC 8032 section 7.1, TEST 1-3: public key, message, signature. */
static const char* const RFC[3][3] = {
    {"d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
     "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
    {"3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
     "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"},
    {"fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
     "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"},
};

/* FIXTURE: packaging/update_manifest.py make and sign (six labelled stand-in assets, not a release). */
static const char MANIFEST[] =
    "# xenogears-pc's update manifest: what the in-app updater reads (port/src/update/update_manifest.h).\n"
    "format=1\n"
    "version=2026.11.27\n"
    "page=https://github.com/EtherAura/xenogears-pc/releases/tag/v2026.11.27\n"
    "download=https://github.com/EtherAura/xenogears-pc/releases/download/v2026.11.27/\n"
    "updates=off\n"
    "asset=tarball linux-x86_64 53 154628ca365a54c235257b1a6291d7e0d64fc4746d9e3901e27549dd4ffb488f00743347567dfa3d11eac4e92427b71e1b11254beeffae49909f8dfa795608f4 xenogears-pc-2026.11.27-linux-x86_64.tar.gz\n"
    "asset=deb linux-x86_64 43 e3d3c18f25517db6eb8578fe68e17de298dc3f1a94df15758babbe27ca4e59e6203afd122fc95c1e168dd441a3e9881911d8e4690c703decf5946de64ac4b513 xenogears-pc_2026.11.27_amd64.deb\n"
    "asset=zip windows-x64 49 f9d0ef0dd95fcbab4d136d37a34414df060b066c3e620accae9cf4d550070318f64340a8532ce4958d8745e3f54994b402b70ee6b18ac294b81346b1b9f20f89 xenogears-pc-2026.11.27-windows-x64.zip\n"
    "asset=setup windows-x64 55 c08b1a87df250ee2e45cdefe5e63d2f250f5c1088c9395816bfcd903ae1494b8126581f2af8f66aea9f1fa2dc3879051cb509c0f675e52a995de12e038a67473 xenogears-pc-2026.11.27-windows-x64-setup.exe\n"
    "asset=android android-arm64-v8a 55 7d890831a5d06535c409173e12db500b5f8c07805513480d8c1a7b760934746138ff8f54d1bbd497d4d4aa4ab722f246bbceb3b11ca8a3fc096e72eb015482fb xenogears-pc-2026.11.27-android-arm64-v8a.apk\n"
    "asset=android android-x86_64 52 ca9a3a11fea0adcd2e64b3c1951694aa9b925018eb0845a9a64ccff6c4790760e386a3c452b73d9ac24283f4ccfb7003dfe12a28b4be4508374a665554dbe164 xenogears-pc-2026.11.27-android-x86_64.apk\n";
static const char SIGNATURE[] =
    "ed25519 d3e48762aa9fecf4 q/qz+uzf3pFA7gTgXofMb4f2BU/rtXZ2Bp5R8ubujXldJ5QrhUdrq8MZS29iiqTHIPC7czVa/uczxD5rl7r+Cw==\n";
static const char* const FIXTURE_KEY = "d3e48762aa9fecf41f07ca7fd729500385c2c62a7e9bf36c461bb0680ea52196";

static int parses(const char* text, const char* needle) {
    UpdateManifest m;
    char why[160] = "";
    int ok = updateManifestParse(text, strlen(text), &m, why, sizeof why);
    if (needle == NULL) return ok;
    if (ok || strstr(why, needle) == NULL) printf("  refusal was: %s\n", why);
    return !ok && strstr(why, needle) != NULL;
}

/* A whole file, up to max bytes and a terminating NUL; 0 when it is missing or longer. */
static size_t slurp(const char* path, char* out, size_t max) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) return 0;
    size_t n = fread(out, 1, max + 1, f);
    fclose(f);
    if (n > max) return 0;
    out[n] = '\0';
    return n;
}

static void releaseFiles(const char* manifestPath, const char* sigPath) {
    static char text[UPDATE_MANIFEST_MAX + 1], sig[UPDATE_SIGNATURE_MAX + 1];
    size_t n = slurp(manifestPath, text, UPDATE_MANIFEST_MAX);
    size_t sn = slurp(sigPath, sig, UPDATE_SIGNATURE_MAX);
    char why[160] = "";
    UpdateManifest m;
    CHECK(n > 0 && sn > 0);
    int trusted = updateSignatureTrusted((const uint8_t*)text, n, sig, sn, why, sizeof why);
    printf("%s: %s\n", sigPath, trusted ? "signed by a key this build trusts" : why);
    CHECK(trusted == 1);
    int parsed = updateManifestParse(text, n, &m, why, sizeof why);
    if (parsed) {
        printf("%s: version %s, updates %s, %d asset(s)\n", manifestPath, m.version, m.updates ? "on" : "off",
               m.assets);
    } else {
        printf("%s: %s\n", manifestPath, why);
    }
    CHECK(parsed == 1);
}

int main(int argc, char** argv) {
    if (argc != 1 && argc != 3) {
        printf("usage: updatemanifesttest [MANIFEST SIGNATURE]\n");
        return 2;
    }
    if (argc == 3) releaseFiles(argv[1], argv[2]);

    /* Monocypher's Ed25519 is RFC 8032's. */
    for (int t = 0; t < 3; t++) {
        uint8_t key[32], msg[2], sig[64];
        size_t n = strlen(RFC[t][1]) / 2;
        hex(RFC[t][0], key, 32);
        hex(RFC[t][1], msg, n);
        hex(RFC[t][2], sig, 64);
        CHECK(crypto_ed25519_check(sig, key, msg, n) == 0);
        sig[63] ^= 0x01;
        CHECK(crypto_ed25519_check(sig, key, msg, n) != 0);
    }

    /* The fixture's signature. */
    uint8_t keys[2][32];
    hex(RFC[0][0], keys[0], 32); /* another key first: only the one with the line's id is tried */
    hex(FIXTURE_KEY, keys[1], 32);
    size_t n = strlen(MANIFEST), sn = strlen(SIGNATURE);
    char why[160] = "";
    CHECK(updateSignatureCheck((const uint8_t*)MANIFEST, n, SIGNATURE, sn, keys, 2, why, sizeof why) == 1);
    CHECK(updateSignatureCheck((const uint8_t*)MANIFEST, n, SIGNATURE, sn, keys, 1, why, sizeof why) == 0);
    CHECK(strstr(why, "no key this build trusts") != NULL);
    CHECK(updateSignatureTrusted((const uint8_t*)MANIFEST, n, SIGNATURE, sn, why, sizeof why) == 0);
    CHECK(updateTrustedKeyCount() == 1 && strstr(why, "no key this build trusts") != NULL);
    CHECK(updateSignatureCheck((const uint8_t*)MANIFEST, n, SIGNATURE, sn, keys, 0, why, sizeof why) == 0);
    CHECK(strstr(why, "trusts no release key yet") != NULL);
    {
        char text[sizeof MANIFEST];
        memcpy(text, MANIFEST, sizeof MANIFEST);
        char* off = strstr(text, "updates=off");
        CHECK(off != NULL);
        if (off != NULL) memcpy(off, "updates=on ", 11); /* the switch flipped: the same length */
        CHECK(updateSignatureCheck((const uint8_t*)text, n, SIGNATURE, sn, keys, 2, why, sizeof why) == 0);
        CHECK(strstr(why, "does not match") != NULL);
        CHECK(updateSignatureCheck((const uint8_t*)MANIFEST, n - 1, SIGNATURE, sn, keys, 2, why, sizeof why) == 0);
    }
    {
        char sig[sizeof SIGNATURE];
        memcpy(sig, SIGNATURE, sizeof SIGNATURE);
        sig[30] = sig[30] == 'A' ? 'B' : 'A'; /* one base64 digit of the signature */
        CHECK(updateSignatureCheck((const uint8_t*)MANIFEST, n, sig, sn, keys, 2, why, sizeof why) == 0);
        char two[2 * sizeof SIGNATURE + 64];
        snprintf(two, sizeof two, "ed25519 0123456789abcdef %.88s\n%s", sig + 25, SIGNATURE); /* a later key's line */
        CHECK(updateSignatureCheck((const uint8_t*)MANIFEST, n, two, strlen(two), keys, 2, why, sizeof why) == 1);
        snprintf(two, sizeof two, "minisign whatever\n%s", SIGNATURE); /* another scheme: passed over */
        CHECK(updateSignatureCheck((const uint8_t*)MANIFEST, n, two, strlen(two), keys, 2, why, sizeof why) == 1);
        CHECK(updateSignatureCheck((const uint8_t*)MANIFEST, n, "\n\n", 2, keys, 2, why, sizeof why) == 0);
        CHECK(strstr(why, "no signature") != NULL);
        CHECK(updateSignatureCheck((const uint8_t*)MANIFEST, n, "", 0, keys, 2, why, sizeof why) == 0);
    }

    /* The fixture's fields. */
    UpdateManifest m;
    CHECK(updateManifestParse(MANIFEST, n, &m, why, sizeof why) == 1);
    CHECK(m.format == 1 && strcmp(m.version, "2026.11.27") == 0 && m.updates == 0 && m.assets == 6);
    CHECK(strcmp(m.page, "https://github.com/EtherAura/xenogears-pc/releases/tag/v2026.11.27") == 0);
    CHECK(strcmp(m.download, "https://github.com/EtherAura/xenogears-pc/releases/download/v2026.11.27/") == 0);
    const UpdateAsset* a = updateManifestAsset(&m, RELEASE_TARBALL, "linux-x86_64");
    CHECK(a != NULL && strcmp(a->name, "xenogears-pc-2026.11.27-linux-x86_64.tar.gz") == 0);
    CHECK(a != NULL && a->size == strlen("fixture: xenogears-pc-2026.11.27-linux-x86_64.tar.gz\n"));
    {
        uint8_t want[64];
        const char* body = "fixture: xenogears-pc-2026.11.27-linux-x86_64.tar.gz\n";
        crypto_sha512(want, (const uint8_t*)body, strlen(body));
        CHECK(a != NULL && memcmp(a->sha512, want, 64) == 0);
    }
    CHECK(updateManifestAsset(&m, RELEASE_DEB, "linux-x86_64") != NULL);
    CHECK(updateManifestAsset(&m, RELEASE_ZIP, "windows-x64") != NULL);
    CHECK(updateManifestAsset(&m, RELEASE_SETUP, "windows-x64") != NULL);
    CHECK(updateManifestAsset(&m, RELEASE_ANDROID, "android-arm64-v8a") != NULL);
    CHECK(updateManifestAsset(&m, RELEASE_ANDROID, "android-x86_64") != NULL);
    CHECK(updateManifestAsset(&m, RELEASE_ANDROID, "linux-x86_64") == NULL);
    CHECK(updateManifestAsset(&m, RELEASE_ARCH, "linux-x86_64") == NULL);

    /* Manifests: what is read, what is refused. */
    const char* base = "format=1\nversion=2026.12.3\npage=https://example.org/r\nupdates=on\n";
    CHECK(parses(base, NULL));
    CHECK(parses("format=1\r\nversion=2026.12.3\r\npage=https://example.org/r\r\nupdates=off\r\n", NULL));
    CHECK(parses("# a comment\nformat=1\nchannel=beta\nversion=2026.12.3\npage=https://example.org/r\nupdates=on\n",
                 NULL)); /* a later format's key */
    CHECK(parses("format=2\nversion=2026.12.3\npage=https://example.org/r\nupdates=on\n", "format 2"));
    CHECK(parses("version=2026.12.3\npage=https://example.org/r\nupdates=on\n", "no format line"));
    CHECK(parses("format=1\nversion=v2026.12.3\npage=https://example.org/r\nupdates=on\n", "not a release's"));
    CHECK(parses("format=1\nversion=2026.12.3\nversion=2026.12.4\npage=https://example.org/r\nupdates=on\n",
                 "given twice"));
    CHECK(parses("format=1\nversion=2026.12.3\npage=http://example.org/r\nupdates=on\n", "https"));
    CHECK(parses("format=1\nversion=2026.12.3\npage=https://example.org/r x\nupdates=on\n", "https"));
    CHECK(parses("format=1\nversion=2026.12.3\npage=https://example.org/r\nupdates=yes\n", "not on or off"));
    CHECK(parses("format=1\nversion=2026.12.3\npage=https://example.org/r\n", "no updates line"));
    CHECK(parses("format=1\nversion=2026.12.3\npage=https://example.org/r\nupdates=on\njunk\n", "key=value"));
    CHECK(parses("format=1\nversion=2026.12.3\npage=https://example.org/r\nupdates=on\x01\n", "not a text file"));
    {
        char text[2048];
        const char* sha = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"
                          "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";
        snprintf(text, sizeof text, "%sasset=zip windows-x64 10 %s x.zip\n", base, sha);
        CHECK(parses(text, "no download line"));
        snprintf(text, sizeof text, "%sdownload=https://example.org/d/\nasset=zip windows-x64 10 %s x.zip\n", base, sha);
        CHECK(parses(text, NULL));
        snprintf(text, sizeof text, "%sdownload=https://example.org/d/\nasset=flatpak linux 10 %s x\n", base, sha);
        CHECK(parses(text, "unknown kind"));
        snprintf(text, sizeof text, "%sdownload=https://example.org/d/\nasset=zip windows-x64 010 %s x.zip\n", base, sha);
        CHECK(parses(text, "size"));
        snprintf(text, sizeof text, "%sdownload=https://example.org/d/\nasset=zip windows-x64 10 %.127s x.zip\n", base, sha);
        CHECK(parses(text, "SHA-512"));
        snprintf(text, sizeof text, "%sdownload=https://example.org/d/\nasset=zip windows-x64 10 %s ../x.zip\n", base, sha);
        CHECK(parses(text, "file name"));
        snprintf(text, sizeof text, "%sdownload=https://example.org/d/\nasset=zip windows-x64 10 %s x.zip y\n", base, sha);
        CHECK(parses(text, "more than five"));
        snprintf(text, sizeof text, "%sdownload=https://example.org/d/\nasset=zip windows-x64 10 %s a.zip\n"
                 "asset=zip windows-x64 11 %s b.zip\n", base, sha, sha);
        CHECK(parses(text, "two assets"));
    }

    /* Release versions in order; this build's platform. */
    CHECK(updateVersionCompare("2026.11.27", "2026.11.27") == 0);
    CHECK(updateVersionCompare("2026.11.9", "2026.11.10") < 0);
    CHECK(updateVersionCompare("2026.12.0", "2026.11.99") > 0);
    CHECK(updateVersionCompare("2027.01.0", "2026.12.5") > 0);
#if defined(__linux__) && defined(__x86_64__) && !defined(__ANDROID__)
    CHECK(strcmp(updatePlatform(), "linux-x86_64") == 0);
#elif defined(_WIN32)
    CHECK(strcmp(updatePlatform(), "windows-x64") == 0);
#endif

    if (failures) {
        printf("updatemanifesttest: %d failure(s)\n", failures);
        return 1;
    }
    printf("updatemanifesttest: ok\n");
    return 0;
}
