from __future__ import annotations

import hashlib
import ipaddress
import json
import shutil
import socket
import time
from datetime import datetime, timezone
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
BUILD_ROOT = REPOSITORY_ROOT / "build" / "transfer-validation"


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def non_loopback_ipv4() -> str:
    candidates: list[str] = []
    try:
        candidates.extend(
            str(entry[4][0])
            for entry in socket.getaddrinfo(
                socket.gethostname(), None, socket.AF_INET, socket.SOCK_STREAM
            )
        )
    except OSError:
        pass
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.connect(("192.0.2.1", 9))
            candidates.append(str(probe.getsockname()[0]))
    except OSError:
        pass
    benchmark = ipaddress.ip_network("198.18.0.0/15")
    for candidate in dict.fromkeys(candidates):
        address = ipaddress.ip_address(candidate)
        if not (
            address.is_loopback
            or address.is_unspecified
            or address.is_multicast
            or address.is_link_local
            or address in benchmark
        ):
            return candidate
    raise RuntimeError("A non-loopback IPv4 address is required")


def wait_for_port(port: int, timeout: float = 15.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.05)
    raise TimeoutError(f"Port {port} did not become ready within {timeout:g}s")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def create_payload(path: Path, size: int) -> str:
    path.parent.mkdir(parents=True, exist_ok=True)
    seed = hashlib.sha256(b"aria2-next-transfer-validation").digest()
    remaining = size
    counter = 0
    with path.open("wb") as output:
        while remaining:
            block = hashlib.sha256(seed + counter.to_bytes(8, "big")).digest()
            count = min(remaining, len(block))
            output.write(block[:count])
            remaining -= count
            counter += 1
    return sha256(path)


class RunDirectory:
    def __init__(self, protocol: str, keep_artifacts: bool):
        timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        self.root = BUILD_ROOT / "runs" / f"{timestamp}-{protocol}"
        self.protocol = protocol
        self.keep_artifacts = keep_artifacts
        self.fixtures = self.root / "fixtures"
        self.downloads = self.root / "downloads"
        self.state = self.root / "state"
        self.logs = self.root / "logs"
        for directory in (self.fixtures, self.downloads, self.state, self.logs):
            directory.mkdir(parents=True, exist_ok=True)

    def write_result(self, result: dict[str, object]) -> None:
        result["protocol"] = self.protocol
        result["runDirectory"] = str(self.root)
        (self.root / "result.json").write_text(
            json.dumps(result, indent=2) + "\n", encoding="utf-8"
        )

    def cleanup_payloads(self) -> None:
        if self.keep_artifacts:
            return
        for directory in (self.fixtures, self.downloads, self.state):
            shutil.rmtree(directory, ignore_errors=True)
