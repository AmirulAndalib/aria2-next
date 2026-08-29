#!/usr/bin/env python3

from __future__ import annotations

import sys
import time
from pathlib import Path


SUITE_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SUITE_ROOT))

from core.engine import EngineProcess
from core.report import run_validation
from core.runtime import RunDirectory, create_payload, sha256
from core.services import CaddyService, ToxiproxyService, WireMockService


def standard_mapping(path: str, body_file: str) -> dict[str, object]:
    return {
        "request": {"method": "GET", "urlPath": path},
        "response": {
            "status": 200,
            "bodyFileName": body_file,
            "headers": {
                "Content-Type": "application/octet-stream",
                "ETag": '"transfer-validation"',
            },
        },
    }


def validate(run: RunDirectory, engine_path: Path | None) -> dict[str, object]:
    payload = run.fixtures / "payload.bin"
    expected = create_payload(payload, 16 * 1024 * 1024)
    durations: dict[str, float] = {}

    with CaddyService(run, "http", run.fixtures) as caddy, WireMockService(
        run, "http"
    ) as wiremock, ToxiproxyService(run, "http") as toxiproxy:
        wiremock.file("payload.bin", payload)
        wiremock.stub(standard_mapping("/tls", "payload.bin"))
        wiremock.stub(
            {
                "scenarioName": "recover-after-empty-response",
                "requiredScenarioState": "Started",
                "newScenarioState": "Recovered",
                "request": {"method": "GET", "urlPath": "/fault-once"},
                "response": {"fault": "EMPTY_RESPONSE"},
            }
        )
        recovered = standard_mapping("/fault-once", "payload.bin")
        recovered["scenarioName"] = "recover-after-empty-response"
        recovered["requiredScenarioState"] = "Recovered"
        wiremock.stub(recovered)

        proxy_port = toxiproxy.create_proxy("http_cut", caddy.port)
        toxiproxy.add_toxic(
            "http_cut", "first_64k", "limit_data", {"bytes": 65536}
        )

        engine = EngineProcess(run, "engine", engine_path)
        engine.start()
        try:
            options = {
                "stream-max-connections": "6",
                "max-tries": "4",
                "retry-wait": "1",
                "auto-file-renaming": "false",
                "allow-overwrite": "true",
            }

            started = time.monotonic()
            gid = engine.add_uri(
                f"{caddy.base_url}/payload.bin", {**options, "out": "plain.bin"}
            )
            engine.rpc.wait_complete(gid, 30)
            durations["plain"] = round(time.monotonic() - started, 3)
            plain = engine.download_dir / "plain.bin"
            if sha256(plain) != expected:
                raise RuntimeError("Plain HTTP payload digest mismatch")

            started = time.monotonic()
            gid = engine.add_uri(
                f"{wiremock.https_base_url}/tls",
                {
                    **options,
                    "out": "tls.bin",
                    "check-certificate": "false",
                },
            )
            engine.rpc.wait_complete(gid, 30)
            durations["tls"] = round(time.monotonic() - started, 3)
            tls = engine.download_dir / "tls.bin"
            if sha256(tls) != expected:
                raise RuntimeError("HTTPS payload digest mismatch")

            started = time.monotonic()
            gid = engine.add_uri(
                f"{wiremock.base_url}/fault-once",
                {**options, "out": "fault-once.bin"},
            )
            engine.rpc.wait_complete(gid, 30)
            durations["emptyResponseRecovery"] = round(
                time.monotonic() - started, 3
            )
            recovered_file = engine.download_dir / "fault-once.bin"
            if sha256(recovered_file) != expected:
                raise RuntimeError("Recovered HTTP payload digest mismatch")

            started = time.monotonic()
            gid = engine.add_uri(
                f"http://127.0.0.1:{proxy_port}/payload.bin",
                {**options, "out": "cut-connection.bin"},
            )
            time.sleep(0.5)
            toxiproxy.remove_toxic("http_cut", "first_64k")
            engine.rpc.wait_complete(gid, 30)
            durations["connectionRecovery"] = round(
                time.monotonic() - started, 3
            )
            interrupted = engine.download_dir / "cut-connection.bin"
            if sha256(interrupted) != expected:
                raise RuntimeError("Interrupted HTTP payload digest mismatch")
        finally:
            engine.stop()

    return {"sha256": expected, "bytes": payload.stat().st_size, "runs": durations}


if __name__ == "__main__":
    raise SystemExit(run_validation("http", validate))
