from __future__ import annotations

import json
import subprocess
import urllib.request
from pathlib import Path
from typing import Any

from .dependencies import caddy_server, toxiproxy_server, wiremock_jar
from .runtime import RunDirectory, free_port, wait_for_port


def post_json(url: str, value: dict[str, object]) -> dict[str, object]:
    request = urllib.request.Request(
        url,
        data=json.dumps(value).encode(),
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(request, timeout=5) as response:
        content = response.read()
    return json.loads(content) if content else {}


class WireMockService:
    def __init__(self, run: RunDirectory, name: str):
        self.run = run
        self.name = name
        self.port = free_port()
        self.https_port = free_port()
        self.root = run.fixtures / f"{name}-wiremock"
        (self.root / "mappings").mkdir(parents=True, exist_ok=True)
        (self.root / "__files").mkdir(parents=True, exist_ok=True)
        self.log = (run.logs / f"{name}.wiremock.log").open("wb")
        self.process: subprocess.Popen[bytes] | None = None

    @property
    def base_url(self) -> str:
        return f"http://127.0.0.1:{self.port}"

    @property
    def https_base_url(self) -> str:
        return f"https://127.0.0.1:{self.https_port}"

    def start(self) -> None:
        command = [
            "java",
            "-jar",
            str(wiremock_jar()),
            "--port",
            str(self.port),
            "--https-port",
            str(self.https_port),
            "--bind-address",
            "127.0.0.1",
            "--root-dir",
            str(self.root),
            "--disable-banner",
            "--async-response-enabled",
            "true",
        ]
        self.process = subprocess.Popen(command, stdout=self.log, stderr=self.log)
        wait_for_port(self.port, 20)
        wait_for_port(self.https_port, 20)

    def file(self, name: str, source: Path) -> None:
        destination = self.root / "__files" / name
        destination.write_bytes(source.read_bytes())

    def stub(self, mapping: dict[str, object]) -> None:
        post_json(f"{self.base_url}/__admin/mappings", mapping)

    def stop(self) -> None:
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        self.log.close()

    def __enter__(self) -> "WireMockService":
        self.start()
        return self

    def __exit__(self, *_: Any) -> None:
        self.stop()


class CaddyService:
    def __init__(self, run: RunDirectory, name: str, root: Path):
        self.run = run
        self.name = name
        self.root = root
        self.port = free_port()
        self.log = (run.logs / f"{name}.caddy.log").open("wb")
        self.process: subprocess.Popen[bytes] | None = None

    @property
    def base_url(self) -> str:
        return f"http://127.0.0.1:{self.port}"

    def start(self) -> None:
        command = [
            str(caddy_server()),
            "file-server",
            "--listen",
            f"127.0.0.1:{self.port}",
            "--root",
            str(self.root),
        ]
        self.process = subprocess.Popen(command, stdout=self.log, stderr=self.log)
        wait_for_port(self.port)

    def stop(self) -> None:
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        self.log.close()

    def __enter__(self) -> "CaddyService":
        self.start()
        return self

    def __exit__(self, *_: Any) -> None:
        self.stop()


class ToxiproxyService:
    def __init__(self, run: RunDirectory, name: str):
        self.run = run
        self.name = name
        self.api_port = free_port()
        self.log = (run.logs / f"{name}.toxiproxy.log").open("wb")
        self.process: subprocess.Popen[bytes] | None = None

    @property
    def api_url(self) -> str:
        return f"http://127.0.0.1:{self.api_port}"

    def start(self) -> None:
        command = [
            str(toxiproxy_server()),
            "-host",
            "127.0.0.1",
            "-port",
            str(self.api_port),
            "-seed",
            "1",
        ]
        self.process = subprocess.Popen(command, stdout=self.log, stderr=self.log)
        wait_for_port(self.api_port)

    def create_proxy(self, name: str, upstream_port: int) -> int:
        listen_port = free_port()
        post_json(
            f"{self.api_url}/proxies",
            {
                "name": name,
                "listen": f"127.0.0.1:{listen_port}",
                "upstream": f"127.0.0.1:{upstream_port}",
                "enabled": True,
            },
        )
        wait_for_port(listen_port)
        return listen_port

    def add_toxic(
        self,
        proxy: str,
        name: str,
        toxic_type: str,
        attributes: dict[str, object],
        stream: str = "downstream",
    ) -> None:
        post_json(
            f"{self.api_url}/proxies/{proxy}/toxics",
            {
                "name": name,
                "type": toxic_type,
                "stream": stream,
                "toxicity": 1.0,
                "attributes": attributes,
            },
        )

    def remove_toxic(self, proxy: str, toxic: str) -> None:
        request = urllib.request.Request(
            f"{self.api_url}/proxies/{proxy}/toxics/{toxic}", method="DELETE"
        )
        with urllib.request.urlopen(request, timeout=5):
            pass

    def stop(self) -> None:
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        self.log.close()

    def __enter__(self) -> "ToxiproxyService":
        self.start()
        return self

    def __exit__(self, *_: Any) -> None:
        self.stop()
