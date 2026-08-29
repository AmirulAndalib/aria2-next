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
from core.services import WireMockService


def validate(run: RunDirectory, engine_path: Path | None) -> dict[str, object]:
    payload = run.fixtures / "payload.bin"
    expected = create_payload(payload, 1024 * 1024)

    with WireMockService(run, "metalink") as wiremock:
        wiremock.file("payload.bin", payload)
        wiremock.stub(
            {
                "request": {"method": "GET", "urlPath": "/unavailable"},
                "response": {"status": 503},
            }
        )
        wiremock.stub(
            {
                "request": {"method": "GET", "urlPath": "/payload"},
                "response": {
                    "status": 200,
                    "bodyFileName": "payload.bin",
                    "headers": {"Content-Type": "application/octet-stream"},
                },
            }
        )
        metalink = run.fixtures / "payload.meta4"
        metalink.write_text(
            f"""<?xml version="1.0" encoding="UTF-8"?>
<metalink xmlns="urn:ietf:params:xml:ns:metalink">
  <file name="metalink.bin">
    <size>{payload.stat().st_size}</size>
    <hash type="sha-256">{expected}</hash>
    <url priority="1">{wiremock.base_url}/unavailable</url>
    <url priority="2">{wiremock.base_url}/payload</url>
  </file>
</metalink>
""",
            encoding="utf-8",
        )

        engine = EngineProcess(run, "engine", engine_path)
        engine.start()
        try:
            started = time.monotonic()
            gids = engine.add_metalink(
                metalink,
                {
                    "dir": str(engine.download_dir),
                    "max-tries": "2",
                    "auto-file-renaming": "false",
                    "allow-overwrite": "true",
                },
            )
            if len(gids) != 1:
                raise RuntimeError(f"Metalink created {len(gids)} tasks")
            status = engine.rpc.wait_complete(gids[0], 30)
            duration = round(time.monotonic() - started, 3)
            downloaded = engine.download_dir / "metalink.bin"
            if sha256(downloaded) != expected:
                raise RuntimeError("Metalink payload digest mismatch")
        finally:
            engine.stop()

    return {
        "sha256": expected,
        "bytes": payload.stat().st_size,
        "durationSeconds": duration,
        "status": status.get("status"),
    }


if __name__ == "__main__":
    raise SystemExit(run_validation("metalink", validate))
