# Vendored Dependencies

This directory contains the curated source required to build aria2-next
without downloading library dependencies during configuration or compilation.

| Dependency | Version | Integration |
| --- | --- | --- |
| Boost | 1.91.0 | Header-only asynchronous networking used by the core and libtorrent |
| curl | 8.21.0 | Static HTTP, HTTPS, and SFTP transfer engine |
| doctest | 2.4.12 | Header-only unit test framework |
| Expat | 2.8.1 | Static XML parser |
| libssh2 | 1.11.1 | Static SFTP transport |
| libtorrent-rasterbar | 2.1.1 | Static BitTorrent engine |
| nghttp2 | 1.70.0 | Static HTTP/2 framing library used by curl |
| OpenSSL | 3.5.6 | Static TLS and cryptography library |
| spdlog | 1.17.0 | Header-only logging library |
| SQLite | 3.53.1 | Static persistent state database |
| wslay | 1.1.1 | Static WebSocket implementation |
| zlib | 1.3.2 | Static compression library |

The default CMake superbuild compiles these sources into an isolated prefix
under the selected build directory. It never searches Homebrew, system package
directories, or a network dependency provider for these libraries.

Vendored directories contain no nested Git repositories or submodules.
Dependency versions are updated manually together with
`packaging/dependencies.env`.

Vendored trees retain the upstream build files, source, headers, and license
material used by the maintained CMake configurations. Alternate build systems,
examples, tests, tools, unsupported crypto backends, and unsupported platform
ports are removed.

curl and nghttp2 contain only their library sources, public headers, CMake
integration, release metadata, and license material. Their command-line tools,
servers, examples, tests, fuzzers, and nested repository metadata are excluded.

Boost is a header subset for the maintained Boost.Asio and libtorrent
configuration. Disabled WebTorrent, I2P, and fallback cryptography include
trees are excluded.
