# Aria2 Next Transfer Validation Suite

This directory contains a manually invoked validation suite for the maintained transfer engines. It is independent from CTest and is never built into the aria2-next executable.

Each protocol module can run on its own:

```bash
python3 tools/transfer_validation/http/validate.py
python3 tools/transfer_validation/sftp/validate.py
python3 tools/transfer_validation/bittorrent/validate.py
python3 tools/transfer_validation/ed2k/validate.py
python3 tools/transfer_validation/metalink/validate.py
python3 tools/transfer_validation/media/validate.py
```

Run every module sequentially with:

```bash
tools/transfer_validation/run all
```

The suite uses the public CLI and JSON-RPC interfaces. It does not include engine internals or protocol implementations. HTTP behavior is provided by WireMock, transport interruption by Toxiproxy, SFTP by OpenSSH, torrent creation by the bundled libtorrent API, and ED2K hashing by OpenSSL.

Media validation uses FFmpeg/ffprobe as local fixture generators and independent
decoding oracles, Caddy for media resources, and WireMock for failure responses.
It covers HLS, DASH, byte ranges, AES-128, track selection, multi-period remuxing,
live recording, interrupted/restarted tasks, cache corruption, and removal.
The FFmpeg executables are not engine runtime dependencies.

Generated state and payloads live under `build/transfer-validation`. Successful payloads are removed automatically. Reports and compact logs remain available for inspection. Pass `--keep-artifacts` to a protocol module when payload inspection is required.

Failed runs retain their payloads and state. HTTP validation checks single and
64-connection downloads, a throttled 320 MiB workload configured for 256
connections, HTTPS, empty files, short ranges, delayed headers, slow tails,
429/503 retries, tiny-gap recovery with an explicit retry wait, progress-gated
tail assistance, interrupted connections, Unicode paths, paused restart, and
batch removal. Conditional requests cover ETag and date validators, unquoted
ETags with inconsistent CDN modification dates, per-request timestamps,
cookie-authenticated redirects with conditional ranges, ignored
ranges, changed resources, and protected existing files. Compact request evidence verifies that faults were exercised and
short responses retrieve only their missing suffix. Successful transfers require
completed RPC state, nondecreasing sampled progress, and matching SHA-256 hashes.
Redirect validation also covers single-use entry points, serialized endpoint
refresh after expiration and overload, and cross-origin credential boundaries.
A dual-stack fixture uses Toxiproxy to slow the IPv6 body without preventing
connection establishment; validation requires evidence that the slow path was
actually exercised before IPv4 completed the download. A second fixture delays
the IPv4 response by 2.5 seconds while keeping its payload fast, guarding against
selecting an address family before both paths have supplied a useful sample.
Another fixture reduces the incumbent path's bandwidth after initial progress
and checks that a healthy alternate remains available to finish the download.
BitTorrent and ED2K checks separately wait for content completion because sharing
tasks can remain active. These are bounded regression scenarios, not a guarantee
against every network or server behavior. No public download service is used.

The dependency lock contains verified macOS ARM64 and Windows x64 Caddy and
Toxiproxy artifacts. HTTP validation also requires Java 17 or newer for
WireMock; Java 21 LTS is suitable. Windows uses native executables and hides
service console windows. Other protocol modules still require their own
platform dependencies. Unpinned hosts fail explicitly.
