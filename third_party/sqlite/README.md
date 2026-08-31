# SQLite source provenance

Corax vendors the official SQLite 3.53.4 amalgamation. This keeps storage features identical across supported platforms and allows clean, offline builds.

- Upstream archive: `https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip`
- Archive SHA3-256: `628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e`
- SQLite source ID: `2026-07-24 19:02:57 bf7c7f30031888f4e796e429ab3978879485813aaca6f641c7b33e4e09459bcc`
- `sqlite3.c` SHA3-256: `67f423e9ebbbdc473cbc4772c872ee6b89f31fde4ed0279a5c25d5f65c043a16`
- `sqlite3.h` SHA3-256: `89d0de498b1012938bb78dc18b68fea52e26f7f7b6db6347d126a2e58b86a05c`
- License status: SQLite is dedicated to the public domain. See `https://www.sqlite.org/copyright.html`.

The authoritative version and compile-option record is [`dependencies.lock.json`](../../dependencies.lock.json).

## Update procedure

1. Select a stable release from the official SQLite download page.
2. Download the versioned amalgamation archive from `sqlite.org`.
3. Verify the published SHA3-256 archive digest before extraction.
4. Replace only `sqlite3.c` and `sqlite3.h`.
5. Update the hashes, source ID, and version in this file and `dependencies.lock.json`.
6. Review security and behavior changes, then run every storage, migration, project-lifecycle, and platform test.
7. Confirm the runtime feature probe still reports the required options.

Do not edit the amalgamation. Put Corax-specific behavior in `corax_storage_sqlite`.
