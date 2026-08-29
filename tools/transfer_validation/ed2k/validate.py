#!/usr/bin/env python3

from __future__ import annotations

import re
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path


SUITE_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SUITE_ROOT))

from core.engine import EngineProcess
from core.report import run_validation
from core.runtime import RunDirectory, create_payload, sha256


def md4(path: Path) -> str:
    openssl = shutil.which("openssl")
    if not openssl:
        raise RuntimeError("OpenSSL is required for ED2K fixture hashing")
    commands = (
        [openssl, "dgst", "-provider", "legacy", "-md4", str(path)],
        [openssl, "dgst", "-md4", str(path)],
    )
    for command in commands:
        completed = subprocess.run(command, text=True, capture_output=True)
        if completed.returncode == 0:
            match = re.search(r"([0-9a-fA-F]{32})\s*$", completed.stdout)
            if match:
                return match.group(1).upper()
    raise RuntimeError("OpenSSL could not calculate an MD4 digest")


def wait_sharing(engine: EngineProcess, gid: str, timeout: float) -> dict[str, object]:
    deadline = time.monotonic() + timeout
    last: dict[str, object] = {}
    while time.monotonic() < deadline:
        last = engine.rpc.call("aria2.tellStatus", [gid])
        if last.get("seeder") == "true":
            return last
        time.sleep(0.1)
    raise TimeoutError(f"ED2K seed did not enter sharing state: {last}")


def wait_download(engine: EngineProcess, gid: str, timeout: float) -> dict[str, object]:
    deadline = time.monotonic() + timeout
    last: dict[str, object] = {}
    while time.monotonic() < deadline:
        last = engine.rpc.call("aria2.tellStatus", [gid])
        if last.get("status") == "error":
            raise RuntimeError(
                f"ED2K task failed: {last.get('errorCode')} {last.get('errorMessage')}"
            )
        total = int(str(last.get("totalLength", "0")))
        completed = int(str(last.get("completedLength", "0")))
        if total > 0 and completed == total:
            return last
        time.sleep(0.1)
    ed2k = last.get("ed2k", {})
    if isinstance(ed2k, dict) and ed2k.get("peerCount") == "0":
        raise RuntimeError(
            "ED2K sources from the file link were not admitted to the peer queue"
        )
    raise TimeoutError(f"ED2K task did not complete: {last}")


def validate(run: RunDirectory, engine_path: Path | None) -> dict[str, object]:
    seed = EngineProcess(run, "seed", engine_path)
    leecher = EngineProcess(run, "leecher", engine_path)
    payload = seed.download_dir / "payload.bin"
    expected = create_payload(payload, 1024 * 1024)
    ed2k_hash = md4(payload)
    nodes = run.fixtures / "empty-nodes.dat"
    nodes.write_bytes(struct.pack("<III", 0, 2, 0))
    link = (
        f"ed2k://|file|payload.bin|{payload.stat().st_size}|{ed2k_hash}|"
        f"sources,127.0.0.1:{seed.ed2k_port}|/"
    )

    seed.start()
    leecher.start()
    try:
        common = {
            "auto-file-renaming": "false",
            "allow-overwrite": "true",
            "ed2k-node-list": str(nodes),
            "seed-time": "10",
        }
        seed_gid = seed.add_uri(link, {**common, "dir": str(seed.download_dir)})
        seed.rpc.wait_complete(seed_gid, 45)
        seed_status = wait_sharing(seed, seed_gid, 15)

        started = time.monotonic()
        leecher_gid = leecher.add_uri(
            link, {**common, "dir": str(leecher.download_dir)}
        )
        leecher_status = wait_download(leecher, leecher_gid, 15)
        duration = round(time.monotonic() - started, 3)
        downloaded = leecher.download_dir / "payload.bin"
        if sha256(downloaded) != expected:
            raise RuntimeError("ED2K payload digest mismatch")
    finally:
        leecher.stop()
        seed.stop()

    return {
        "sha256": expected,
        "ed2k": ed2k_hash,
        "bytes": payload.stat().st_size,
        "durationSeconds": duration,
        "seedStatus": seed_status.get("status"),
        "leecherStatus": leecher_status.get("status"),
    }


if __name__ == "__main__":
    raise SystemExit(run_validation("ed2k", validate))
