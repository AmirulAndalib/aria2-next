#!/usr/bin/env python3

from __future__ import annotations

import base64
import hashlib
import json
import shutil
import sys
import time
import urllib.request
from pathlib import Path

SUITE_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SUITE_ROOT))

from core.engine import EngineProcess
from core.report import run_validation
from core.runtime import RunDirectory, create_payload, sha256
from core.services import CaddyService, ToxiproxyService, WireMockService


def validate(run: RunDirectory, engine_path: Path | None) -> dict[str, object]:
    payload = run.fixtures / "payload.bin"
    expected = create_payload(payload, 16 * 1024 * 1024)
    with payload.open("rb") as source:
        block = source.read(1024 * 1024)
        source.seek(4 * 1024 * 1024)
        response_body = source.read(1024 * 1024)
    (run.fixtures / "empty.bin").touch()
    (run.fixtures / "tiny.bin").write_bytes(block[:60651])
    large = run.fixtures / "large.bin"
    large_hash = hashlib.sha256()
    with large.open("wb") as output:
        for index in range(320):
            chunk = hashlib.sha256(index.to_bytes(8, "big")).digest() + block[32:]
            output.write(chunk)
            large_hash.update(chunk)

    results: dict[str, object] = {}
    with CaddyService(run, "http", run.fixtures) as caddy, WireMockService(
        run, "http"
    ) as wiremock, ToxiproxyService(run, "http") as proxy:
        wiremock.stub({
            "priority": 10,
            "request": {"method": "GET", "urlPath": "/payload.bin"},
            "response": {"proxyBaseUrl": caddy.base_url},
        })
        begin, end = 4 * 1024 * 1024, 5 * 1024 * 1024
        requested = f"bytes={begin}-{end - 1}"
        missing = f"bytes={end - 65536}-{end - 1}"
        for case in ("short", "tail"):
            body = response_body[:-65536] if case == "short" else response_body
            response = {
                "status": 206,
                "base64Body": base64.b64encode(body).decode(),
                "headers": {
                    "Content-Range":
                        f"bytes {begin}-{begin + len(body) - 1}/{payload.stat().st_size}"
                },
            }
            if case == "tail":
                response["chunkedDribbleDelay"] = {
                    "numberOfChunks": 16, "totalDuration": 8000,
                }
            wiremock.stub({
                "priority": 1,
                "request": {
                    "method": "GET", "url": f"/payload.bin?case={case}",
                    "headers": {"Range": {"equalTo": requested}},
                },
                "response": response,
            })
        wiremock.stub({
            "priority": 1,
            "request": {
                "method": "GET", "url": "/payload.bin?case=delayed",
                "headers": {"Range": {"matches": "bytes=[1-9][0-9]*-.*"}},
            },
            "response": {
                "proxyBaseUrl": caddy.base_url, "fixedDelayMilliseconds": 3000,
            },
        })
        for code in (429, 503):
            wiremock.stub({
                "priority": 1,
                "scenarioName": f"retry-{code}",
                "requiredScenarioState": "Started",
                "newScenarioState": "Recovered",
                "request": {
                    "method": "GET", "url": f"/payload.bin?case=retry-{code}",
                    "headers": {"Range": {"equalTo": requested}},
                },
                "response": {
                    "status": code, "headers": {"Retry-After": "1"},
                },
            })

        engine = EngineProcess(run, "engine", engine_path)
        session = run.state / "download.session"
        engine.start([f"--save-session={session}"])
        options = {
            "stream-max-connections": "64",
            "max-tries": "4",
            "retry-wait": "1",
            "auto-file-renaming": "false",
        }

        def check(
            name: str, url: str, overrides: dict[str, str] | None = None,
            digest: str = expected,
        ) -> str:
            started = time.monotonic()
            gid = engine.add_uri(
                url, {**options, "out": name, **(overrides or {})}
            )
            engine.rpc.wait_complete(gid, 30)
            if sha256(engine.download_dir / name) != digest:
                raise RuntimeError(f"Payload digest mismatch: {name}")
            results[name] = round(time.monotonic() - started, 3)
            return gid

        try:
            for name in ("empty.bin", "tiny.bin"):
                check(name, f"{caddy.base_url}/{name}",
                      digest=sha256(run.fixtures / name))
            check("single.bin", f"{caddy.base_url}/payload.bin",
                  {"stream-max-connections": "1"})
            check("tls.bin", f"{wiremock.https_base_url}/payload.bin",
                  {"check-certificate": "false"})
            for case in ("short", "delayed", "tail", "retry-429", "retry-503"):
                check(f"{case}.bin",
                      f"{wiremock.base_url}/payload.bin?case={case}")

            # Native throttling keeps the 256-way transfer observable without
            # relying on an external server or a multi-gigabyte download.
            limited_port = proxy.create_proxy("fanout", caddy.port)
            proxy.add_toxic("fanout", "bandwidth", "bandwidth", {"rate": 2048})
            check("large.bin", f"http://127.0.0.1:{limited_port}/large.bin",
                  {"stream-max-connections": "256"}, large_hash.hexdigest())

            cut_port = proxy.create_proxy("cut", caddy.port)
            proxy.add_toxic("cut", "first_64k", "limit_data", {"bytes": 65536})
            gid = engine.add_uri(
                f"http://127.0.0.1:{cut_port}/payload.bin",
                {**options, "out": "cut.bin"},
            )
            time.sleep(0.5)
            proxy.remove_toxic("cut", "first_64k")
            engine.rpc.wait_complete(gid, 30)
            if sha256(engine.download_dir / "cut.bin") != expected:
                raise RuntimeError("Interrupted transfer digest mismatch")

            # Keep only the request evidence needed to detect lost or repeated
            # work; do not duplicate WireMock response bodies in the report.
            with urllib.request.urlopen(
                f"{wiremock.base_url}/__admin/requests"
            ) as response:
                requests = [
                    {"url": e["request"]["url"],
                     "range": e["request"]["headers"].get("Range")}
                    for e in json.load(response)["requests"]
                ]
            (run.logs / "http-requests.json").write_text(
                json.dumps(requests, indent=2)
            )

            def ranges(case):
                return [r["range"] for r in requests
                        if r["url"] == f"/payload.bin?case={case}"]

            short = ranges("short")
            if short.count(requested) != 1 or short.count(missing) != 1:
                raise RuntimeError("Short response did not request only its missing suffix")
            for code in (429, 503):
                if ranges(f"retry-{code}").count(requested) != 2:
                    raise RuntimeError(f"HTTP {code} retry was not exercised exactly once")
            tail = [tuple(map(int, value.removeprefix("bytes=").split("-")))
                    for value in ranges("tail") if value]
            if not any(begin < first <= last < end for first, last in tail):
                raise RuntimeError("Slow tail work was not reassigned")

            gid = engine.add_uri(
                f"{caddy.base_url}/payload.bin",
                {**options, "out": "resume-é-下载.bin", "max-download-limit": "4M"},
            )
            time.sleep(0.2)
            engine.rpc.call("aria2.pause", [gid])
            paused = engine.rpc.wait_status(gid, "paused")
            if not 0 < int(paused["completedLength"]) < int(paused["totalLength"]):
                raise RuntimeError("Pause did not retain partial progress")
            engine.rpc.call("aria2.saveSession")
            engine.stop()
            shutil.copyfile(engine.engine_log, run.logs / "before-restart.engine.log")
            engine.start([f"--save-session={session}", f"--input-file={session}"])
            restored = engine.rpc.wait_status(gid, "paused")
            if any(restored[k] != paused[k] for k in ("totalLength", "completedLength")):
                raise RuntimeError("Paused progress changed across restart")
            engine.rpc.call("aria2.unpause", [gid])
            engine.rpc.wait_complete(gid, 30)
            if sha256(engine.download_dir / "resume-é-下载.bin") != expected:
                raise RuntimeError("Resumed transfer digest mismatch")

            gids = [
                engine.add_uri(
                    f"{caddy.base_url}/payload.bin",
                    {**options, "out": f"remove-{i}.bin", "max-download-limit": "1M"},
                ) for i in range(3)
            ]
            time.sleep(0.2)
            engine.rpc.call("aria2.forcePauseAll")
            for gid in gids:
                engine.rpc.wait_status(gid, "paused")
                engine.rpc.call("aria2.forceRemove", [gid])
            engine.rpc.call("aria2.purgeDownloadResult")
            if engine.rpc.call("aria2.tellActive") or engine.rpc.call(
                "aria2.tellWaiting", [0, 10]
            ):
                raise RuntimeError("Removed transfers remain in the engine")
            check("after-remove.bin", f"{caddy.base_url}/payload.bin")
            results["restartAndRemoval"] = "passed"
        finally:
            engine.stop()

    return {"sha256": expected, "runs": results}


if __name__ == "__main__":
    raise SystemExit(run_validation("http", validate))
