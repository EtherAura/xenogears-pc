# Monocypher (vendored)

The in-app updater checks the signature on each release's update manifest
with Ed25519 (RFC 8032) from Monocypher (xg-4oq.14.2).

- Source: https://monocypher.org, release 4.0.2
  (monocypher-4.0.2.tar.gz, SHA-512
  bf275d4c53ff94af6cdc723a4e002e9f080f4d1436c86c76bb37870b34807f1d7b32331d8ff8a1aeb369e946f3769021e03e63efac25b82efc5abf54dc084714,
  the same bytes as the GitHub release asset of LoupVaillant/Monocypher 4.0.2).
- Licence: CC0-1.0 or BSD-2-Clause, the user's choice (LICENCE.md). The port
  takes it under CC0.
- Files: src/monocypher.c and .h, src/optional/monocypher-ed25519.c and .h
  (Ed25519 with SHA-512; Monocypher's own EdDSA uses BLAKE2b). Nothing else.
- Unmodified.

To update: copy the same files from a newer release and change the version
and digest above.
