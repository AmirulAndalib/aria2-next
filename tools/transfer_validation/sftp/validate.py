#!/usr/bin/env python3

from __future__ import annotations

import shutil
import sys
import time
import urllib.parse
from pathlib import Path


SUITE_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SUITE_ROOT))

from core.engine import EngineProcess
from core.report import run_validation
from core.rpc import RpcError
from core.runtime import RunDirectory, create_payload, sha256
from core.services import ToxiproxyService
from sftp.fixture import SftpServer, generate_rsa_key


def wait_error(
    engine: EngineProcess, gid: str, timeout: float
) -> dict[str, object]:
    deadline = time.monotonic() + timeout
    last: dict[str, object] = {}
    while time.monotonic() < deadline:
        last = engine.rpc.call("aria2.tellStatus", [gid])
        if last.get("status") == "error":
            return last
        if last.get("status") == "complete":
            raise RuntimeError("Invalid SFTP credentials unexpectedly succeeded")
        time.sleep(0.1)
    raise TimeoutError(f"Invalid SFTP credentials did not fail: {last}")


def validate(run: RunDirectory, engine_path: Path | None) -> dict[str, object]:
    ssh_keygen = shutil.which("ssh-keygen")
    if not ssh_keygen:
        raise RuntimeError("ssh-keygen is required")
    server_root = run.fixtures / "sftp-server"
    server_root.mkdir(parents=True, exist_ok=True)
    payload = server_root / "payload.bin"
    expected = create_payload(payload, 4 * 1024 * 1024)
    client_key = server_root / "client_key"
    rejected_key = server_root / "rejected_key"
    generate_rsa_key(ssh_keygen, client_key, pem=True)
    generate_rsa_key(ssh_keygen, rejected_key, pem=True)
    with SftpServer(run, server_root, client_key.with_suffix(".pub")) as server:
        port = server.port
        with ToxiproxyService(run, "sftp") as toxiproxy:
            proxy_port = toxiproxy.create_proxy("sftp_cut", port)
            toxiproxy.add_toxic(
                "sftp_cut", "first_64k", "limit_data", {"bytes": 65536}
            )
            engine = EngineProcess(run, "engine", engine_path)
            engine.start()
            try:
                remote_path = urllib.parse.quote(server.remote_path(payload), safe="/")
                options = {
                    "sftp-user": server.username,
                    "private-key": str(client_key),
                    "auto-file-renaming": "false",
                    "allow-overwrite": "true",
                    "max-tries": "4",
                    "retry-wait": "1",
                }
                started = time.monotonic()
                gid = engine.add_uri(
                    f"sftp://127.0.0.1:{port}{remote_path}",
                    {**options, "out": "sftp.bin"},
                )
                status = engine.rpc.wait_complete(gid, 30)
                duration = round(time.monotonic() - started, 3)
                downloaded = engine.download_dir / "sftp.bin"
                if sha256(downloaded) != expected:
                    raise RuntimeError("SFTP payload digest mismatch")

                started = time.monotonic()
                gid = engine.add_uri(
                    f"sftp://127.0.0.1:{proxy_port}{remote_path}",
                    {**options, "out": "sftp-recovered.bin"},
                )
                time.sleep(0.5)
                toxiproxy.remove_toxic("sftp_cut", "first_64k")
                try:
                    recovered_status = engine.rpc.wait_complete(gid, 30)
                except RpcError as error:
                    raise RuntimeError(
                        "SFTP transport interruption was not retried"
                    ) from error
                recovery_duration = round(time.monotonic() - started, 3)
                recovered = engine.download_dir / "sftp-recovered.bin"
                if sha256(recovered) != expected:
                    raise RuntimeError("Recovered SFTP payload digest mismatch")

                gid = engine.add_uri(
                    f"sftp://127.0.0.1:{port}{remote_path}",
                    {
                        **options,
                        "sftp-user": f"{server.username}-invalid",
                        "private-key": str(rejected_key),
                        "out": "sftp-rejected.bin",
                    },
                )
                rejected_status = wait_error(engine, gid, 10)
                if rejected_status.get("errorCode") != "24":
                    raise RuntimeError(
                        "Invalid SFTP credentials returned an unexpected error: "
                        f"{rejected_status}"
                    )
            finally:
                engine.stop()

    return {
        "sha256": expected,
        "bytes": payload.stat().st_size,
        "durationSeconds": duration,
        "recoveryDurationSeconds": recovery_duration,
        "status": status.get("status"),
        "recoveryStatus": recovered_status.get("status"),
        "authenticationError": rejected_status.get("errorCode"),
    }


if __name__ == "__main__":
    raise SystemExit(run_validation("sftp", validate))
