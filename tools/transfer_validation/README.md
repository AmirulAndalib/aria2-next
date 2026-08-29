# Aria2 Next Transfer Validation Suite

This directory contains a manually invoked validation suite for the maintained transfer engines. It is independent from CTest and is never built into the aria2-next executable.

Each protocol module can run on its own:

```bash
python3 tools/transfer_validation/http/validate.py
python3 tools/transfer_validation/sftp/validate.py
python3 tools/transfer_validation/bittorrent/validate.py
python3 tools/transfer_validation/ed2k/validate.py
python3 tools/transfer_validation/metalink/validate.py
```

Run every module sequentially with:

```bash
tools/transfer_validation/run all
```

The suite uses the public CLI and JSON-RPC interfaces. It does not include engine internals or protocol implementations. HTTP behavior is provided by WireMock, transport interruption by Toxiproxy, SFTP by OpenSSH, torrent creation by the bundled libtorrent API, and ED2K hashing by OpenSSL.

Generated state and payloads live under `build/transfer-validation`. Successful payloads are removed automatically. Reports and compact logs remain available for inspection. Pass `--keep-artifacts` to a protocol module when payload inspection is required.

The dependency lock currently contains verified macOS ARM64 artifacts. Other hosts fail with an explicit unsupported-platform message until their release artifacts and hashes are added deliberately.
