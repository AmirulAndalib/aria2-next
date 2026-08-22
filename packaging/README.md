# Packaging

This directory owns release packaging, cross-compilation helpers, platform package resources, and release dependency metadata.

`dependencies.env` is the authoritative dependency source for maintained release automation. It records versions, archive names, URLs, and SHA-256 hashes for downloaded release inputs.

## Layout

| Path | Purpose |
| --- | --- |
| `notes/` | Platform notes copied into binary packages |
| `docker/` | Linux runtime container image definition |
| `macos/` | macOS package resources |
| `scripts/` | Release packaging helpers |
| `dependencies.env` | Maintained dependency baseline and source archive hashes |

Supported packaging paths build this repository checkout through CMake. Third-party dependencies may use their own upstream build systems while they are being built as release dependencies.

Official release builds use `packaging/scripts/release-size-profile` to apply size-oriented compiler flags, per-function and per-data sections, and platform linker dead-code elimination. The profile is used by GitHub release jobs so portable artifacts keep the maintained dependency baseline without retaining avoidable unused code.

GitHub Release assets are bare executable binaries named `aria2-next-<version>-<platform>-<architecture>`, plus a SHA-256 checksum file. Source code and license material are provided by the GitHub release tag source archives.

Container images are published automatically after official release assets are uploaded and remain independently publishable through the manual Docker workflow. GitHub Container Registry receives `ghcr.io/aninsomniacy/aria2-next:v<version>` and `ghcr.io/aninsomniacy/aria2-next:latest`. The image is assembled from the selected GitHub Release Linux x86_64 and Linux ARM64 binaries instead of rebuilding aria2-next inside Docker. `PUID` and `PGID` are mandatory. The entrypoint assigns persistent mounts to those IDs and seeds the configuration and session state. Non-root IDs are recommended; UID or GID `0` remains available with an explicit startup warning.

`packaging/scripts/check-runtime-deps` and `packaging/scripts/size-audit` remain available for manual release inspection.

The release dependency boundary is platform-specific. Boost headers and libtorrent-rasterbar are built from the pinned source archives for every release target, and libtorrent is linked statically. Linux release binaries may use the system ELF loader, C/C++ runtime, and OpenSSL 3 runtime, while zlib, Expat, SQLite, c-ares, libssh2, and libtorrent must be linked into the executable. macOS release binaries use Apple Security framework trust evaluation and may link only Apple system libraries and frameworks at runtime; third-party dependencies must be linked into the executable. Windows release binaries use WinTLS for aria2, libssh2 WinCNG, and a statically linked OpenSSL build for libtorrent; they may link only Windows system DLLs at runtime. Android release binaries may link only Android system runtime libraries and must not require `libc++_shared.so`.

Maintained libtorrent builds explicitly enable DHT, peer encryption, protocol extensions, mutable torrents, and streaming priorities. I2P and WebTorrent are disabled because aria2-next does not expose those transports and the release artifacts do not ship their runtime dependencies.
