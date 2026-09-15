#!/usr/bin/env python3
"""Validate native media selection recovery, retry and output collision handling."""
from __future__ import annotations

import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from core.engine import EngineProcess
from core.report import run_validation
from core.runtime import RunDirectory
from core.services import CaddyService, WireMockService
from media.common import command, decoded_hash


def validate(run: RunDirectory, executable: Path | None) -> dict[str, object]:
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise RuntimeError("FFmpeg is required for fixture generation and decoding")
    server = CaddyService(run, "media-recovery", run.fixtures)
    manifest = run.fixtures / "index.m3u8"
    command(ffmpeg, "-v", "error", "-f", "lavfi", "-i",
            "testsrc2=size=64x64:rate=5", "-t", "6", "-c:v", "libx264",
            "-g", "10", "-f", "hls", "-hls_time", "2",
            "-hls_playlist_type", "vod", "-hls_base_url", server.base_url + "/", str(manifest))
    segment = sorted(run.fixtures.glob("*.ts"))[1]
    withheld = segment.with_suffix(".withheld")
    assert segment.resolve().parent == run.fixtures.resolve()
    assert withheld.resolve().parent == run.fixtures.resolve()
    segment.rename(withheld)
    with server, WireMockService(run, "media-mime") as manifest_server:
        manifest_server.stub({
            "request": {"method": "GET", "url": "/manifest"},
            "response": {"status": 200, "headers": {"Content-Type": "application/vnd.apple.mpegurl"}, "body": manifest.read_text(encoding="utf-8")},
        })
        with EngineProcess(run, "media-recovery", executable) as engine:
            session = engine.root / "session.txt"
            engine.rpc.call("aria2.changeGlobalOption", [{"save-session": str(session)}])
            # The source has no suffix; only its response identifies it as HLS.
            gid = engine.add_uri(manifest_server.base_url + "/manifest", {
                "media-pause-after-probe": "true", "max-tries": "1",
                "out": "recording.mp4", "media-audio": "none",
            })
            selected = engine.rpc.wait_status(gid, "paused", 20)
            assert selected["media"]["state"] == "awaiting-selection"
            assert engine.rpc.call("aria2.getOption", [gid])["media"] == "hls"
            engine.rpc.call("aria2.saveSession")
            engine.stop()
            engine.start([f"--input-file={session}", f"--save-session={session}"])
            restored = engine.rpc.wait_status(gid, "paused", 20)
            assert restored["media"]["state"] == "awaiting-selection"
            engine.rpc.call("aria2.changeOption", [gid, {"media-pause-after-probe": "false"}])
            engine.rpc.call("aria2.unpause", [gid])
            failed = engine.rpc.wait_status(gid, "error", 20)
            assert int(failed["media"]["downloadedLength"]) > 0
            withheld.rename(segment)
            assert engine.rpc.call("aria2.retryMedia", [gid]) == gid
            completed = engine.rpc.wait_status(gid, "complete", 20)
            output = completed["files"][0]["path"]
            reference = decoded_hash(ffmpeg, server.base_url + "/index.m3u8", "v")
            assert decoded_hash(ffmpeg, output, "v") == reference
            duplicate = engine.add_uri(server.base_url + "/index.m3u8", {
                "out": "recording.mp4", "media-audio": "none",
                "auto-file-renaming": "true",
            })
            second = engine.rpc.wait_status(duplicate, "complete", 20)
            assert second["files"][0]["path"] != output
            assert decoded_hash(ffmpeg, second["files"][0]["path"], "v") == reference
            assert decoded_hash(ffmpeg, output, "v") == reference
            return {"gid": gid, "selectionRestored": True, "retried": True,
                    "existingOutputPreserved": True}


if __name__ == "__main__":
    raise SystemExit(run_validation("media-recovery", validate))
