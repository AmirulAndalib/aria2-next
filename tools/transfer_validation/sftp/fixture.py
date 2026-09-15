"""Native SFTP servers with isolated fixture keys and loopback listeners."""

from __future__ import annotations

import getpass
import json
import os
import shutil
import subprocess
from pathlib import Path

from core.dependencies import sftpgo_server
from core.runtime import RunDirectory, free_port, process_options, wait_for_port


def generate_rsa_key(executable: str, path: Path, pem: bool = False) -> None:
    command = [executable, "-q", "-t", "rsa", "-b", "2048"]
    if pem:
        command.extend(("-m", "PEM"))
    command.extend(("-N", "", "-f", str(path)))
    subprocess.run(command, check=True, **process_options())


class SftpServer:
    def __init__(self, run: RunDirectory, root: Path, public_key: Path):
        self.port = free_port()
        self.username = "transfer-validation" if os.name == "nt" else getpass.getuser()
        self.root = root
        self.public_key = public_key
        self.log_path = run.logs / "sftp-server.log"
        self.log = None
        self.process: subprocess.Popen[bytes] | None = None

    def command(self) -> list[str]:
        if os.name == "nt":
            # Portable mode uses one in-memory virtual account, avoiding any
            # dependency on Windows service installation or system user access.
            config = self.root / "sftpgo.json"
            config.write_text(json.dumps({
                "sftpd": {"bindings": [{"port": self.port, "address": "127.0.0.1"}]},
                "telemetry": {"bind_port": 0},
            }), encoding="utf-8")
            return [str(sftpgo_server()), "portable", "--config-dir", str(self.root),
                    "--directory", str(self.root), "--username", self.username,
                    "--public-key", self.public_key.read_text(encoding="utf-8").strip(),
                    "--sftpd-port", str(self.port), "--log-file-path", str(self.log_path)]

        sshd = Path(shutil.which("sshd") or "/usr/sbin/sshd")
        keygen = shutil.which("ssh-keygen")
        if not sshd.is_file() or not keygen:
            raise RuntimeError("OpenSSH server and ssh-keygen are required")
        host_key = self.root / "host_key"
        generate_rsa_key(keygen, host_key)
        config = self.root / "sshd_config"
        config.write_text("\n".join((
            f"Port {self.port}", "ListenAddress 127.0.0.1",
            f'HostKey "{host_key}"', f'PidFile "{self.root / "sshd.pid"}"',
            f'AuthorizedKeysFile "{self.public_key}"', "PasswordAuthentication no",
            "KbdInteractiveAuthentication no", "PubkeyAuthentication yes", "UsePAM no",
            "StrictModes no", f"AllowUsers {self.username}",
            "Subsystem sftp internal-sftp", "LogLevel VERBOSE", "",
        )), encoding="utf-8")
        return [str(sshd), "-D", "-e", "-f", str(config)]

    def remote_path(self, path: Path) -> str:
        return "/" + path.relative_to(self.root).as_posix() if os.name == "nt" else path.as_posix()

    def __enter__(self) -> "SftpServer":
        command = self.command()
        self.log = self.log_path.with_suffix(".console.log").open("wb")
        try:
            self.process = subprocess.Popen(
                command, stdout=self.log, stderr=self.log, **process_options()
            )
            wait_for_port(self.port)
        except BaseException:
            self.__exit__()
            raise
        return self

    def __exit__(self, *_: object) -> None:
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        if self.log:
            self.log.close()
