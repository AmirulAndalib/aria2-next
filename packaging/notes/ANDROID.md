# Android ARM64 Release Note

The Android ARM64 release asset is a native aria2-next command-line executable, not an Android application package.

The binary is built with the Android NDK and statically linked release dependencies recorded in `packaging/dependencies.env` in the source tree. The official release binary is checked before packaging so it does not require `libc++_shared.so`.

Example use from an Android shell environment:

```sh
chmod 755 ./aria2-next
./aria2-next --version
./aria2-next https://example.com/file.iso
```

The binary honors `SSL_CERT_FILE` and `SSL_CERT_DIR`, then loads the Termux CA bundle from `$PREFIX/etc/tls/cert.pem`. Use `--ca-certificate` for an explicit CA bundle. Configure `--async-dns` or `--async-dns-server` when the shell does not expose usable DNS defaults.
