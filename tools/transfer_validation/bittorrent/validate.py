#!/usr/bin/env python3

from __future__ import annotations

import subprocess
import sys
import time
from pathlib import Path


SUITE_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SUITE_ROOT))

from core.engine import EngineProcess
from core.report import run_validation
from core.runtime import BUILD_ROOT, REPOSITORY_ROOT, RunDirectory, create_payload, sha256


def build_creator() -> Path:
    source = Path(__file__).resolve().parent
    build = BUILD_ROOT / "helpers" / "bittorrent"
    prefix = REPOSITORY_ROOT / "build/default/dependencies"
    boost = REPOSITORY_ROOT / "third_party/boost"
    subprocess.run(
        [
            "cmake",
            "-S",
            str(source),
            "-B",
            str(build),
            "-G",
            "Ninja",
            f"-DCMAKE_PREFIX_PATH={prefix}",
            f"-DBoost_INCLUDE_DIR={boost}",
            "-DBoost_NO_BOOST_CMAKE=ON",
            "-DCMAKE_BUILD_TYPE=Release",
        ],
        check=True,
    )
    subprocess.run(["cmake", "--build", str(build)], check=True)
    return build / "aria2_next_create_torrent"


def wait_seeding(engine: EngineProcess, gid: str, timeout: float) -> dict[str, object]:
    deadline = time.monotonic() + timeout
    last: dict[str, object] = {}
    while time.monotonic() < deadline:
        last = engine.rpc.call("aria2.tellStatus", [gid])
        bittorrent = last.get("bittorrent", {})
        if last.get("seeder") == "true" or (
            isinstance(bittorrent, dict) and bittorrent.get("state") == "seeding"
        ):
            return last
        time.sleep(0.1)
    raise TimeoutError(f"Seed task did not enter seeding state: {last}")


def validate(run: RunDirectory, engine_path: Path | None) -> dict[str, object]:
    seed = EngineProcess(run, "seed", engine_path)
    leecher = EngineProcess(run, "leecher", engine_path)
    payload = seed.download_dir / "payload.bin"
    expected = create_payload(payload, 4 * 1024 * 1024)
    torrent = run.fixtures / "payload.torrent"
    subprocess.run([str(build_creator()), str(payload), str(torrent)], check=True)

    isolated_bt = ["--bt-interface=127.0.0.1", "--disable-ipv6=true"]
    seed.start(isolated_bt)
    leecher.start(isolated_bt)
    try:
        common = {
            "auto-file-renaming": "false",
            "allow-overwrite": "true",
            "check-integrity": "true",
            "seed-time": "10",
        }
        seed_gid = seed.add_torrent(torrent, {**common, "dir": str(seed.download_dir)})
        seed.rpc.wait_complete(seed_gid, 45)
        seed_status = wait_seeding(seed, seed_gid, 15)
        seed_session = seed.rpc.call("aria2.getBtSessionStatus")
        endpoints = seed_session.get("listenEndpoints", [])
        if not endpoints:
            raise RuntimeError(f"BitTorrent seed has no listen endpoint: {seed_session}")
        seed_endpoint = str(endpoints[0])

        started = time.monotonic()
        leecher_gid = leecher.add_torrent(
            torrent, {**common, "dir": str(leecher.download_dir)}
        )
        peer_result = leecher.rpc.call(
            "aria2.addBtPeers",
            [leecher_gid, [seed_endpoint]],
        )
        try:
            leecher_status = leecher.rpc.wait_complete(leecher_gid, 45)
        except Exception as error:
            raise RuntimeError(
                f"{error}; seedSession={seed_session}; "
                f"leecherSession={leecher.rpc.call('aria2.getBtSessionStatus')}"
            ) from error
        duration = round(time.monotonic() - started, 3)
        downloaded = leecher.download_dir / "payload.bin"
        if sha256(downloaded) != expected:
            raise RuntimeError("BitTorrent payload digest mismatch")
    finally:
        leecher.stop()
        seed.stop()

    return {
        "sha256": expected,
        "bytes": payload.stat().st_size,
        "durationSeconds": duration,
        "peerResult": peer_result,
        "listenEndpoint": seed_endpoint,
        "seedStatus": seed_status.get("status"),
        "leecherStatus": leecher_status.get("status"),
    }


if __name__ == "__main__":
    raise SystemExit(run_validation("bittorrent", validate))
