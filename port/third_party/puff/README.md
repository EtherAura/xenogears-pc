# puff (vendored)

The in-app updater unpacks the release it installs, the Windows .zip and the
Linux .tar.gz, both deflate (RFC 1951), with puff: zlib's reference inflater
by Mark Adler (xg-4oq.14.4; port/src/update/update_archive.c reads the zip and
tar around it).

- Source: zlib 1.4, contrib/puff (puff version 2.3), from zlib-1.4.tar.gz
  (SHA-256 02dd159783677a749c04adbdd02b55b8a2bcc7e054f51b76b549199a916ad584,
  as zlib.net lists it; the same bytes as the GitHub release asset of
  madler/zlib v1.4).
- Licence: the zlib licence, in puff.h's header.
- Files: puff.c and puff.h. Nothing else.
- Unmodified.

To update: copy the same files from a newer zlib release and change the version
and digest above.
