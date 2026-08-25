aria2 - The ultra fast download utility
=======================================

Aria2 Next maintenance note
---------------------------

Aria2 Next is maintained by AnInsomniacy since 2026 as the ``aria2-next``
engine for Motrix Next and other consumers. BitTorrent uses
libtorrent-rasterbar 2.1. Maintenance focuses on reliability, current
dependency baselines, and reproducible cross-platform releases. CMake is the
only supported build system, and Ninja is the default generator.

Disclaimer
----------
This program comes with no warranty.
You must use this program at your own risk.

Introduction
------------

aria2-next downloads files over HTTP(S), SFTP, BitTorrent, Metalink, and
ED2K file links. BitTorrent v1, v2, and hybrid torrents are handled by
libtorrent-rasterbar 2.1. Metalink chunk checksums validate downloaded chunks.

Aria2 Next includes native ED2K/eMule support aligned with aMule's network
behavior. The maintained runtime, persistence, discovery, transfer, and
verification design is documented in ``docs/maintenance/ed2k-runtime.md``.

The maintained fork is located at https://github.com/AnInsomniacy/aria2-next.
It retains the maintained command-line, configuration, session, JSON-RPC, and
libaria2 surfaces without emulating removed BitTorrent internals.

See the upstream `aria2 Online Manual
<https://aria2.github.io/manual/en/html/>`_ to learn how to use aria2.

Features
--------

Here is a list of features:

* Command-line interface
* Download files through HTTP(S)/SFTP/BitTorrent/ED2K
* Segmented downloading
* Metalink version 4 (RFC 5854) support for HTTP/SFTP
* Metalink version 3.0 support for HTTP/SFTP
* Metalink/HTTP (RFC 6249) support
* libcurl HTTP/1.1, HTTP/2, HTTPS, and SFTP transport
* HTTP Proxy support
* HTTP BASIC authentication support
* HTTP Proxy authentication support
* Well-known environment variables for proxy: ``http_proxy``,
  ``https_proxy``, ``all_proxy`` and ``no_proxy``
* HTTP gzip, deflate content encoding support
* Verify peer using given trusted CA certificate in HTTPS
* Client certificate authentication in HTTPS
* Chunked transfer encoding support
* Load Cookies from the file using the Firefox3 format, Chromium/Google Chrome
  and the Mozilla/Firefox
  (1.x/2.x)/Netscape format.
* Save Cookies in the Mozilla/Firefox (1.x/2.x)/Netscape format.
* Custom HTTP Header support
* Persistent Connections support
* SFTP through HTTP Proxy
* Download/Upload speed throttling
* BitTorrent extensions: Fast extension, DHT, PEX, MSE/PSE,
  Multi-Tracker, UDP tracker
* BitTorrent `WEB-Seeding <http://getright.com/seedtorrent.html>`_.
  aria2 requests chunk more than piece size to reduce the request
  overhead. It also supports pipelined requests with piece size.
* BitTorrent Local Peer Discovery
* Rename/change the directory structure of BitTorrent downloads
  completely
* JSON-RPC (over HTTP and WebSocket)/XML-RPC interface
* Run as a daemon process
* Selective download in multi-file torrent/Metalink
* Chunk checksum validation in Metalink
* Can disable segmented downloading in Metalink
* Netrc support
* Configuration file support
* Download URIs found in a text file or stdin and the destination
  directory and output file name can be specified optionally
* Parameterized URI support
* IPv6 support with Happy Eyeballs
* Disk cache to reduce disk activity


Versioning
----------

Aria2 Next uses semantic versions. ``CMakeLists.txt`` is the version source of
truth, and release tags use ``v{PROJECT_VERSION}``. Official release artifacts
are built by the GitHub release workflow after a matching GitHub Release is
published.

How to get source code
----------------------

The canonical source location for this maintained fork is the Aria2 Next
repository that contains this file. The original upstream project is hosted at
https://github.com/aria2/aria2.

Clone this repository and build from the checkout root with CMake.

Dependency
----------


======================== ========================================
features                  dependency
======================== ========================================
Secure RPC               OpenSSL
HTTP/HTTPS/SFTP          libcurl with nghttp2 and libssh2
BitTorrent               None (OpenSSL is used when present)
ED2K                     None
Metalink                 Expat
gzip, deflate in HTTP    zlib
Async DNS                c-ares
Firefox3/Chromium cookie libsqlite3
XML-RPC                  Expat
JSON-RPC over WebSocket  None (bundled wslay)
======================== ========================================


.. note::

  HTTP, HTTPS, and SFTP client security is provided by libcurl. Windows
  builds use libcurl's Schannel backend and the native certificate store.
  OpenSSL 3.0 or newer provides secure RPC server TLS and cryptographic
  primitives on every platform.

You can disable BitTorrent and Metalink support with
``-DARIA2_ENABLE_BITTORRENT=OFF`` and ``-DARIA2_ENABLE_METALINK=OFF``.

To enable async DNS support, you need c-ares 1.34.5 or newer.

* c-ares: https://c-ares.org/

How to build
------------

aria2-next is written in C++17 with a small amount of C11; any current
GCC, Clang, or llvm-mingw toolchain works.

Source builds require CMake 3.25+, Ninja, Make, Perl, and a C11/C++17 platform
toolchain. All maintained library source is bundled under ``third_party/``.
The default superbuild compiles an isolated static dependency stack without
network access or system development packages.
Install the documentation toolchain if you want to build the manual and man
page::

    $ python3 -m pip install 'sphinx>=8.2,<9' 'sphinx-rtd-theme>=3.0,<4'

The quickest local build uses the default preset::

    $ cmake --preset default
    $ cmake --build --preset default
    $ ctest --preset default

A plain CMake invocation is also supported::

    $ cmake -S . -B build/default -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
    $ cmake --build build/default
    $ ctest --test-dir build/default --output-on-failure

The executable is located at ``build/default/aria2-next`` when using the default
preset.

The CMake configure step validates the vendored dependency stack and enables
the maintained feature set.

Aria2 Next checks HTTPS server certificates through libcurl by default. Windows
uses Schannel and the native certificate store. Other platforms use the trust
configuration of their maintained libcurl TLS backend. A PEM CA file supplied
with ``--ca-certificate`` replaces the default trust configuration for that
transfer.

By default, the bash completion file named ``aria2-next`` is installed to the
default documentation directory. To change that directory, set
``-DARIA2_BASH_COMPLETION_DIR=/path/to/directory``.

Unit tests use the bundled doctest framework and always build; CTest runs the
tests executable. Run a subset with
``build/default/aria2_tests --test-case='UtilTest1.*'``.

See `Cross-compiling Windows binary`_ to create a Windows binary.
See `Cross-compiling Android binary`_ to create an Android binary.

Cross-compiling Windows binary
------------------------------

In this section, we describe how to build a Windows binary using a
mingw-w64 (http://mingw-w64.org/doku.php) cross-compiler on Debian
Linux. The MinGW (http://www.mingw.org/) may not be able to build
aria2.

After compiling and installing dependency libraries, cross-compile with a CMake
toolchain file or explicit ``CMAKE_SYSTEM_NAME``, compiler, prefix, and
``PKG_CONFIG_LIBDIR`` settings. The maintained release workflow is the reference
Windows cross-build implementation. It assumes the following libraries have been
built for cross-compilation:

* c-ares
* expat
* sqlite3
* zlib
* libssh2

Some environment variables can be adjusted to change build settings:

``HOST``
  cross-compile to build programs to run on ``HOST``. It defaults to
  ``i686-w64-mingw32``. To build a 64bit binary, specify
  ``x86_64-w64-mingw32``.

``PREFIX``
  Prefix to the directory where dependent libraries are installed.  It
  defaults to ``/usr/local/$HOST``. ``-I$PREFIX/include`` will be
  added to ``CPPFLAGS``. ``-L$PREFIX/lib`` will be added to
  ``LDFLAGS``. ``$PREFIX/lib/pkgconfig`` will be set to
  ``PKG_CONFIG_LIBDIR``.

For example, a 64-bit Windows build uses ``-DCMAKE_SYSTEM_NAME=Windows`` with
``x86_64-w64-mingw32-gcc`` and ``x86_64-w64-mingw32-g++``. If you want an
installable libaria2 build, enable ``-DARIA2_ENABLE_LIBARIA2=ON`` and prepare
matching shared or static external libraries.

Cross-compiling Android binary
------------------------------

In this section, we describe how to build Android binary using Android
NDK cross-compiler on Debian Linux.

The maintained Android NDK baseline is recorded in
``packaging/dependencies.env``.

The maintained release workflow is the reference Android cross-build
implementation. It assumes the following libraries have been built for
cross-compilation:

* c-ares
* openssl
* expat
* zlib
* libssh2

Build the dependency libraries as static libraries and install them under a
single Android prefix. Then configure aria2 with CMake using the Android NDK
toolchain variables. The maintained release workflow reads the NDK baseline
from ``packaging/dependencies.env`` and passes
``CMAKE_SYSTEM_NAME=Android``, ``CMAKE_ANDROID_NDK``,
``CMAKE_ANDROID_ARCH_ABI=arm64-v8a``, ``CMAKE_SYSTEM_VERSION``,
``CMAKE_PREFIX_PATH``, and ``PKG_CONFIG_LIBDIR``.

Official Android releases are published as bare ARM64 executable assets named
``aria2-next-<version>-android-arm64``.

Building documentation
----------------------

`Sphinx <http://sphinx-doc.org/>`_ is used to build the documentation.
Install the documentation dependencies first::

    $ python3 -m pip install 'sphinx>=8.2,<9' 'sphinx-rtd-theme>=3.0,<4'

aria2 man pages will be built when you run ``make`` if they are not
up-to-date.  You can also build an HTML version of the aria2 man page by
``make html`` from the relevant ``docs/manual/<language>`` directory.
The HTML version manual is also available
`online <https://aria2.github.io/manual/en/html/>`_.

BitTorrent
-----------

About file names
~~~~~~~~~~~~~~~~
The file name of the downloaded file is determined as follows:

single-file mode
    If "name" key is present in .torrent file, the file name is the value
    of "name" key. Otherwise, the file name is the base name of .torrent
    file appended by ".file". For example, .torrent file is
    "tests.torrent", then file name is "tests.torrent.file".  The
    directory to store the downloaded file can be specified by -d
    option.

multi-file mode
    The complete directory/file structure mentioned in .torrent file
    is created.  The directory to store the top directory of
    downloaded files can be specified by -d option.

Before download starts, a complete directory structure is created if
needed. By default, aria2 opens at most 100 files mentioned in
.torrent file, and directly writes to and reads from these files.
The number of files to open simultaneously can be controlled by
``--bt-max-open-files`` option.

DHT
~~~

libtorrent provides mainline-compatible IPv4 and IPv6 DHT. DHT and UDP
trackers use the UDP side of ``--listen-port``. aria2-next atomically
checkpoints the native routing table below ``--state-dir`` and restores it
before bootstrapping a new session.

UDP tracker
~~~~~~~~~~~

UDP trackers remain available when DHT is disabled.

Other things should be noted
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* ``-o`` option is used to change the file name of .torrent file itself,
  not a file name of a file in .torrent file. For this purpose, use
  ``--index-out`` option instead.
* The default TCP and UDP listen port is 6881.
* libtorrent attempts UPnP and NAT-PMP port mapping.
* The default maximum number of peers per torrent is 55.

Metalink
--------

The current Metalink implementation supports HTTP(S) and SFTP.
Other protocols in Metalink documents are ignored. Both Metalink4 (RFC 5854) and
Metalink version 3.0 documents are supported.

For checksum verification, md5, sha-1, sha-224, sha-256, sha-384, and
sha-512 are supported. If multiple hash algorithms are provided, aria2
uses a stronger one. If whole file checksum verification fails, aria2
doesn't retry the download and just exits with a non-zero return code.

The supported user preferences are version, language, location,
protocol, and os.

If chunk checksums are provided in the Metalink file, aria2 automatically
validates chunks of data during download. This behavior can be turned
off by a command-line option.

If a signature is included in a Metalink file, aria2 saves it as a file
after the completion of the download.  The file name is download
file name + ".sig". If the same file already exists, the signature file is
not saved.

In Metalink4, a multi-file torrent could appear in metalink:metaurl
element.  Since aria2 cannot download 2 same torrents at the same
time, aria2 groups files in metalink:file element which has the same
BitTorrent metaurl, and downloads them from a single BitTorrent swarm.
This is a basically multi-file torrent download with file selection, so
the adjacent files which are not in Metalink document but share the same
piece with the selected file are also created.

If relative URI is specified in metalink:url or metalink:metaurl
element, aria2 uses the URI of Metalink file as base URI to resolve
the relative URI. If relative URI is found in the Metalink file which is
read from the local disk, aria2 uses the value of ``--metalink-base-uri``
option as base URI. If this option is not specified, the relative URI
will be ignored.

Metalink/HTTP
-------------

The current implementation only uses rel=duplicate links.  aria2
understands Digest header fields and check whether it matches the
digest value from other sources. If it differs, drop the connection.
aria2 also uses this digest value to perform checksum verification
after the download is finished. aria2 recognizes geo value. To tell aria2
which location you prefer, you can use ``--metalink-location`` option.

netrc
-----

netrc support is enabled by default for HTTP(S)/SFTP through libcurl. To disable
netrc support, specify -n command-line option.  Your .netrc file
should have correct permissions(600).

WebSocket
---------

The WebSocket server embedded in aria2 implements the specification
defined in RFC 6455. The supported protocol version is 13.

libaria2
--------

The libaria2 is a C++ library that offers aria2 functionality to the
client code. Currently, libaria2 is not built by default. To enable
libaria2, use the ``-DARIA2_ENABLE_LIBARIA2=ON`` CMake option. See libaria2
documentation to know how to use API.

References
----------

* `aria2 Online Manual <https://aria2.github.io/manual/en/html/>`_
* https://github.com/AnInsomniacy/aria2-next
* `RFC 1738 Uniform Resource Locators (URL) <http://tools.ietf.org/html/rfc1738>`_
* `RFC 2616 Hypertext Transfer Protocol -- HTTP/1.1 <http://tools.ietf.org/html/rfc2616>`_
* `RFC 3986 Uniform Resource Identifier (URI): Generic Syntax <http://tools.ietf.org/html/rfc3986>`_
* `RFC 4038 Application Aspects of IPv6 Transition <http://tools.ietf.org/html/rfc4038>`_
* `RFC 5854 The Metalink Download Description Format <http://tools.ietf.org/html/rfc5854>`_
* `RFC 6249 Metalink/HTTP: Mirrors and Hashes <http://tools.ietf.org/html/rfc6249>`_
* `RFC 6265 HTTP State Management Mechanism <http://tools.ietf.org/html/rfc6265>`_
* `RFC 6266 Use of the Content-Disposition Header Field in the Hypertext Transfer Protocol (HTTP) <http://tools.ietf.org/html/rfc6266>`_
* `RFC 6455 The WebSocket Protocol <http://tools.ietf.org/html/rfc6455>`_
* `RFC 6555 Happy Eyeballs: Success with Dual-Stack Hosts <http://tools.ietf.org/html/rfc6555>`_

* `The BitTorrent Protocol Specification <http://www.bittorrent.org/beps/bep_0003.html>`_
* `BitTorrent: DHT Protocol <http://www.bittorrent.org/beps/bep_0005.html>`_
* `BitTorrent: Fast Extension <http://www.bittorrent.org/beps/bep_0006.html>`_
* `BitTorrent: IPv6 Tracker Extension <http://www.bittorrent.org/beps/bep_0007.html>`_
* `BitTorrent: Extension for Peers to Send Metadata Files <http://www.bittorrent.org/beps/bep_0009.html>`_
* `BitTorrent: Extension Protocol <http://www.bittorrent.org/beps/bep_0010.html>`_
* `BitTorrent: Multitracker Metadata Extension <http://www.bittorrent.org/beps/bep_0012.html>`_
* `BitTorrent: UDP Tracker Protocol for BitTorrent <http://www.bittorrent.org/beps/bep_0015.html>`_
  and `BitTorrent udp-tracker protocol specification <http://www.rasterbar.com/products/libtorrent/udp_tracker_protocol.html>`_.
* `BitTorrent: WebSeed - HTTP/FTP Seeding (GetRight style) <http://www.bittorrent.org/beps/bep_0019.html>`_
* `BitTorrent: Private Torrents <http://www.bittorrent.org/beps/bep_0027.html>`_
* `BitTorrent: BitTorrent DHT Extensions for IPv6 <http://www.bittorrent.org/beps/bep_0032.html>`_
* `BitTorrent: Message Stream Encryption <http://wiki.vuze.com/w/Message_Stream_Encryption>`_
* `Kademlia: A Peer-to-peer Information System Based on the  XOR Metric <https://pdos.csail.mit.edu/~petar/papers/maymounkov-kademlia-lncs.pdf>`_
