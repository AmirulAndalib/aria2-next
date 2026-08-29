#!/usr/bin/env python3

from __future__ import annotations

import getpass
import shutil
import subprocess
import sys
import time
import urllib.parse
from pathlib import Path


SUITE_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SUITE_ROOT))

from core.engine import EngineProcess
from core.report import run_validation
from core.rpc import RpcError
from core.runtime import RunDirectory, create_payload, free_port, sha256, wait_for_port
from core.services import ToxiproxyService


def validate(run: RunDirectory, engine_path: Path | None) -> dict[str, object]:
    sshd = Path(shutil.which("sshd") or "/usr/sbin/sshd")
    ssh_keygen = shutil.which("ssh-keygen")
    if not sshd.is_file() or not ssh_keygen:
        raise RuntimeError("OpenSSH server and ssh-keygen are required")

    server_root = run.fixtures / "sftp-server"
    server_root.mkdir(parents=True, exist_ok=True)
    payload = server_root / "payload.bin"
    expected = create_payload(payload, 4 * 1024 * 1024)
    host_key = server_root / "host_key"
    client_key = server_root / "client_key"
    subprocess.run(
        [ssh_keygen, "-q", "-t", "rsa", "-b", "2048", "-N", "", "-f", str(host_key)],
        check=True,
    )
    subprocess.run(
        [ssh_keygen, "-q", "-t", "rsa", "-b", "2048", "-m", "PEM", "-N", "", "-f", str(client_key)],
        check=True,
    )
    authorized_keys = server_root / "authorized_keys"
    authorized_keys.write_bytes(client_key.with_suffix(".pub").read_bytes())

    port = free_port()
    config = server_root / "sshd_config"
    config.write_text(
        "\n".join(
            (
                f"Port {port}",
                "ListenAddress 127.0.0.1",
                f"HostKey {host_key}",
                f"PidFile {server_root / 'sshd.pid'}",
                f"AuthorizedKeysFile {authorized_keys}",
                "PasswordAuthentication no",
                "KbdInteractiveAuthentication no",
                "PubkeyAuthentication yes",
                "UsePAM no",
                "StrictModes no",
                f"AllowUsers {getpass.getuser()}",
                "Subsystem sftp internal-sftp",
                "LogLevel VERBOSE",
                "",
            )
        ),
        encoding="utf-8",
    )
    sshd_log = (run.logs / "sftp.sshd.log").open("wb")
    server = subprocess.Popen(
        [str(sshd), "-D", "-e", "-f", str(config)],
        stdout=sshd_log,
        stderr=sshd_log,
    )
    try:
        wait_for_port(port)
        with ToxiproxyService(run, "sftp") as toxiproxy:
            proxy_port = toxiproxy.create_proxy("sftp_cut", port)
            toxiproxy.add_toxic(
                "sftp_cut", "first_64k", "limit_data", {"bytes": 65536}
            )
            engine = EngineProcess(run, "engine", engine_path)
            engine.start()
            try:
                remote_path = urllib.parse.quote(str(payload), safe="/")
                options = {
                    "sftp-user": getpass.getuser(),
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
            finally:
                engine.stop()
    finally:
        if server.poll() is None:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=3)
        sshd_log.close()

    return {
        "sha256": expected,
        "bytes": payload.stat().st_size,
        "durationSeconds": duration,
        "recoveryDurationSeconds": recovery_duration,
        "status": status.get("status"),
        "recoveryStatus": recovered_status.get("status"),
    }


if __name__ == "__main__":
    raise SystemExit(run_validation("sftp", validate))
