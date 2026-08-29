from __future__ import annotations

import hashlib
import json
import os
import platform
import shutil
import tarfile
import urllib.request
from pathlib import Path

from .runtime import BUILD_ROOT


LOCK_PATH = Path(__file__).resolve().parents[1] / "dependencies.lock"


def platform_key() -> str:
    system = platform.system().lower()
    machine = platform.machine().lower()
    if machine in ("arm64", "aarch64"):
        machine = "arm64"
    elif machine in ("x86_64", "amd64"):
        machine = "amd64"
    return f"{system}-{machine}"


def _lock() -> dict[str, object]:
    return json.loads(LOCK_PATH.read_text(encoding="utf-8"))


def _download(url: str, destination: Path, expected_sha256: str) -> Path:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists() and _sha256(destination) == expected_sha256:
        return destination
    temporary = destination.with_suffix(destination.suffix + ".download")
    with urllib.request.urlopen(url, timeout=60) as response, temporary.open(
        "wb"
    ) as output:
        while block := response.read(1024 * 1024):
            output.write(block)
    actual = _sha256(temporary)
    if actual != expected_sha256:
        temporary.unlink(missing_ok=True)
        raise RuntimeError(
            f"Dependency checksum mismatch for {destination.name}: {actual}"
        )
    temporary.replace(destination)
    return destination


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def wiremock_jar() -> Path:
    metadata = _lock()["wiremock"]
    assert isinstance(metadata, dict)
    return _download(
        str(metadata["url"]),
        BUILD_ROOT / "dependencies" / str(metadata["filename"]),
        str(metadata["sha256"]),
    )


def toxiproxy_server() -> Path:
    metadata = _lock()["toxiproxy"]
    assert isinstance(metadata, dict)
    platforms = metadata["platforms"]
    assert isinstance(platforms, dict)
    key = platform_key()
    if key not in platforms:
        raise RuntimeError(f"Toxiproxy is not pinned for {key}")
    entry = platforms[key]
    assert isinstance(entry, dict)
    binary = _download(
        str(entry["url"]),
        BUILD_ROOT / "dependencies" / str(entry["filename"]),
        str(entry["sha256"]),
    )
    os.chmod(binary, 0o755)
    return binary


def caddy_server() -> Path:
    metadata = _lock()["caddy"]
    assert isinstance(metadata, dict)
    platforms = metadata["platforms"]
    assert isinstance(platforms, dict)
    key = platform_key()
    if key not in platforms:
        raise RuntimeError(f"Caddy is not pinned for {key}")
    entry = platforms[key]
    assert isinstance(entry, dict)
    archive = _download(
        str(entry["url"]),
        BUILD_ROOT / "dependencies" / str(entry["filename"]),
        str(entry["sha256"]),
    )
    binary = BUILD_ROOT / "dependencies" / "caddy"
    if not binary.exists():
        with tarfile.open(archive, "r:gz") as package:
            member = package.getmember("caddy")
            source = package.extractfile(member)
            if source is None:
                raise RuntimeError("Caddy archive does not contain its executable")
            with binary.open("wb") as output:
                shutil.copyfileobj(source, output)
    os.chmod(binary, 0o755)
    return binary
