#!/usr/bin/env python3
"""Local native-library media validation. No public media services are used."""
from __future__ import annotations

import json
import os
import copy
import sqlite3
import xml.etree.ElementTree as ET
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from core.engine import EngineProcess
from core.report import run_validation
from core.runtime import RunDirectory
from core.services import CaddyService, WireMockService


def command(*args: str) -> str:
    # Do not inject the Python environment's native-library settings into FFmpeg.
    result = subprocess.run(args, capture_output=True, text=True, env=native_env())
    if result.returncode:
        raise RuntimeError(result.stderr or f"Command failed: {args[0]}")
    return result.stdout


def native_env() -> dict:
    return {
        key: os.environ[key]
        for key in ("PATH", "SystemRoot", "WINDIR", "TEMP", "TMP", "TMPDIR")
        if key in os.environ
    }


def wait(
    engine: EngineProcess, gid: str, state: str = "complete", timeout: float = 30
) -> dict:
    until = time.monotonic() + timeout
    last = {}
    while time.monotonic() < until:
        last = engine.rpc.call("aria2.tellStatus", [gid])
        if last["status"] == state:
            return last
        if last["status"] == "error":
            raise AssertionError(last)
        time.sleep(0.1)
    raise TimeoutError(last)


def validate(run: RunDirectory, engine_path: Path | None) -> dict:
    ffmpeg, ffprobe = shutil.which("ffmpeg"), shutil.which("ffprobe")
    if not ffmpeg or not ffprobe:
        raise RuntimeError(
            "Fixture generation requires FFmpeg and ffprobe (not engine runtime dependencies)"
        )
    source = run.fixtures / "source.mp4"
    command(
        ffmpeg,
        "-v",
        "error",
        "-f",
        "lavfi",
        "-i",
        "testsrc2=size=160x90:rate=15",
        "-f",
        "lavfi",
        "-i",
        "sine=frequency=440:sample_rate=48000",
        "-t",
        "6",
        "-c:v",
        "libx264",
        "-preset",
        "ultrafast",
        "-g",
        "30",
        "-c:a",
        "aac",
        str(source),
    )
    for name in ("hls", "fmp4", "dash", "byterange", "encrypted"):
        directory = run.fixtures / name
        directory.mkdir()
        args = [ffmpeg, "-v", "error", "-i", str(source), "-map", "0", "-c", "copy"]
        if name == "dash":
            args += ["-f", "dash", "-seg_duration", "2", str(directory / "index.mpd")]
        else:
            args += ["-f", "hls", "-hls_time", "2", "-hls_list_size", "0"]
            if name == "fmp4":
                args += ["-hls_segment_type", "fmp4"]
            if name == "byterange":
                args += ["-hls_flags", "single_file"]
            if name == "encrypted":
                key = directory / "key.bin"
                key.write_bytes(bytes(range(16)))
                key_info = directory / "key.txt"
                key_info.write_text(f"key.bin\n{key}\n")
                args += ["-hls_key_info_file", str(key_info)]
            args += [str(directory / "index.m3u8")]
        command(*args)
    manifest = ET.parse(run.fixtures / "dash/index.mpd")
    ns = "{urn:mpeg:dash:schema:mpd:2011}"
    root = manifest.getroot()
    root.set("mediaPresentationDuration", "PT12S")
    period = root.find(ns + "Period")
    period.set("duration", "PT6S")
    second = copy.deepcopy(period)
    second.set("id", "1")
    second.set("start", "PT6S")
    root.append(second)
    ET.register_namespace("", ns[1:-1])
    manifest.write(
        run.fixtures / "dash/multi.mpd", encoding="utf-8", xml_declaration=True
    )
    expected = command(
        ffmpeg,
        "-v",
        "error",
        "-i",
        str(source),
        "-map",
        "0:v",
        "-f",
        "hash",
        "-hash",
        "sha256",
        "-",
    ).strip()
    results = {}
    subtitle_root = run.fixtures / "hls"
    (subtitle_root / "subtitles.m3u8").write_text(
        "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:2\n"
        + "".join(f"#EXTINF:2,\nsub{i}.vtt\n" for i in range(3))
        + "#EXT-X-ENDLIST\n"
    )
    for i in range(3):
        (subtitle_root / f"sub{i}.vtt").write_text(
            f"WEBVTT\n\n00:00:0{i * 2}.000 --> 00:00:0{i * 2 + 1}.500\nCaption {i}\n\n"
        )
    (subtitle_root / "master.m3u8").write_text(
        '#EXTM3U\n#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID="subs",NAME="English",LANGUAGE="en",URI="subtitles.m3u8"\n'
        '#EXT-X-STREAM-INF:BANDWIDTH=400000,RESOLUTION=160x90,CODECS="avc1.42c00b,mp4a.40.2",SUBTITLES="subs"\nindex.m3u8\n'
    )
    with CaddyService(run, "media", run.fixtures) as server, EngineProcess(
        run, "media", engine_path
    ) as engine:
        for name, suffix, container in (
            ("hls", "m3u8", "mp4"),
            ("fmp4", "m3u8", "mkv"),
            ("dash", "mpd", "mp4"),
            ("byterange", "m3u8", "mp4"),
            ("encrypted", "m3u8", "mp4"),
        ):
            gid = engine.add_uri(
                f"{server.base_url}/{name}/index.{suffix}",
                {
                    "out": f"{name}.{container}",
                    "media-format": container,
                    "max-tries": "2",
                },
            )
            status = wait(engine, gid)
            output = Path(status["files"][0]["path"])
            actual = command(
                ffmpeg,
                "-v",
                "error",
                "-i",
                str(output),
                "-map",
                "0:v",
                "-f",
                "hash",
                "-hash",
                "sha256",
                "-",
            ).strip()
            assert actual == expected, (name, actual, expected)
            info = json.loads(
                command(
                    ffprobe,
                    "-v",
                    "error",
                    "-show_streams",
                    "-show_format",
                    "-of",
                    "json",
                    str(output),
                )
            )
            assert {s["codec_type"] for s in info["streams"]} == {
                "audio",
                "video",
            }, info
            assert abs(float(info["format"]["duration"]) - 6) < 0.2, info
            assert status["media"]["state"] == "complete", status
            assert status["media"]["progress"] == "1.000000", status
            assert int(status["completedLength"]) == output.stat().st_size, status
            results[name] = {
                "bytes": output.stat().st_size,
                "duration": info["format"]["duration"],
                "videoHash": actual,
            }
        gid = engine.add_uri(f"{server.base_url}/dash/multi.mpd", {"out": "multi.mp4"})
        status = wait(engine, gid)
        info = json.loads(
            command(
                ffprobe,
                "-v",
                "error",
                "-show_format",
                "-of",
                "json",
                status["files"][0]["path"],
            )
        )
        assert abs(float(info["format"]["duration"]) - 12) < 0.2, info
        results["multiPeriod"] = info["format"]["duration"]

        gid = engine.add_uri(
            f"{server.base_url}/dash/index.mpd",
            {
                "out": "audio.mkv",
                "media-format": "mkv",
                "media-pause-after-probe": "true",
            },
        )
        paused = wait(engine, gid, "paused")
        assert paused["media"]["state"] == "awaiting-selection", paused
        engine.rpc.call(
            "aria2.changeOption",
            [gid, {"media-video": "none", "media-pause-after-probe": "false"}],
        )
        engine.rpc.call("aria2.unpause", [gid])
        status = wait(engine, gid)
        info = json.loads(
            command(
                ffprobe,
                "-v",
                "error",
                "-show_streams",
                "-of",
                "json",
                status["files"][0]["path"],
            )
        )
        assert {s["codec_type"] for s in info["streams"]} == {"audio"}, info
        results["selection"] = "audio only"

        gid = engine.add_uri(
            f"{server.base_url}/hls/master.m3u8",
            {"out": "subtitles.mkv", "media-format": "mkv", "media-subtitles": "en"},
        )
        status = wait(engine, gid)
        captions = command(
            ffmpeg,
            "-v",
            "error",
            "-i",
            status["files"][0]["path"],
            "-map",
            "0:s",
            "-f",
            "webvtt",
            "-",
        )
        assert all(f"Caption {i}" in captions for i in range(3)), captions
        results["subtitles"] = "all segmented WebVTT captions retained"

        gid = engine.add_uri(
            f"{server.base_url}/hls/index.m3u8",
            {"out": "hls-audio.mkv", "media-format": "mkv", "media-video": "none"},
        )
        status = wait(engine, gid)
        info = json.loads(
            command(
                ffprobe,
                "-v",
                "error",
                "-show_streams",
                "-of",
                "json",
                status["files"][0]["path"],
            )
        )
        assert {s["codec_type"] for s in info["streams"]} == {"audio"}, info

        # Native servers supply fault responses; the engine must never publish
        # a successful partial presentation or silently fall back to a file.
        with WireMockService(run, "media-faults") as faults:
            fragment = run.fixtures / "hls/index0.ts"
            size = fragment.stat().st_size
            faults.file("fragment.ts", fragment)
            faults.stub(
                {
                    "request": {"method": "GET", "urlPath": "/wrong.ts"},
                    "response": {
                        "status": 206,
                        "headers": {"Content-Range": f"bytes 1-{size}/{size + 1}"},
                        "bodyFileName": "fragment.ts",
                    },
                }
            )
            playlist = (run.fixtures / "hls/index.m3u8").read_text()
            playlist = "\n".join(
                f"{server.base_url}/hls/{line}" if line.endswith(".ts") else line
                for line in playlist.splitlines()
            )
            for route, body in (
                ("manifest", playlist),
                ("missing", playlist.replace("index1.ts", "absent.ts")),
                (
                    "wrong-range",
                    f"#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXTINF:2,\n{faults.base_url}/wrong.ts\n#EXT-X-ENDLIST\n",
                ),
                (
                    "invalid",
                    "#EXTM3U\n#EXTINF:2,\nfile:///etc/passwd\n#EXT-X-ENDLIST\n",
                ),
            ):
                faults.stub(
                    {
                        "request": {"method": "GET", "urlPath": f"/{route}"},
                        "response": {
                            "status": 200,
                            "headers": {
                                "Content-Type": "application/vnd.apple.mpegurl"
                            },
                            "body": body,
                        },
                    }
                )
                gid = engine.add_uri(
                    f"{faults.base_url}/{route}",
                    {"out": f"{route}.mp4", "max-tries": "1"},
                )
                status = wait(
                    engine, gid, "complete" if route == "manifest" else "error"
                )
                if route == "manifest":
                    actual = command(
                        ffmpeg,
                        "-v",
                        "error",
                        "-i",
                        status["files"][0]["path"],
                        "-map",
                        "0:v",
                        "-f",
                        "hash",
                        "-hash",
                        "sha256",
                        "-",
                    ).strip()
                    assert actual == expected, status
                else:
                    assert (
                        status["media"]["state"] == "error" and status["media"]["error"]
                    ), status
                    assert not Path(status["files"][0]["path"]).exists(), status
                    engine.rpc.call("aria2.removeDownloadResult", [gid])
                    assert not (engine.root / "media/tasks" / gid).exists(), gid
            results["failureHandling"] = (
                "MIME detection succeeds; missing fragments and local-file references fail without publishing output"
            )

        live_dir = run.fixtures / "live"
        live_dir.mkdir()
        with (run.logs / "live-producer.log").open("wb") as log:
            producer = subprocess.Popen(
                [
                    ffmpeg,
                    "-v",
                    "error",
                    "-re",
                    "-stream_loop",
                    "-1",
                    "-i",
                    str(source),
                    "-map",
                    "0",
                    "-c",
                    "copy",
                    "-f",
                    "hls",
                    "-hls_time",
                    "2",
                    "-hls_list_size",
                    "5",
                    str(live_dir / "index.m3u8"),
                ],
                stdout=log,
                stderr=log,
                env=native_env(),
            )
            try:
                until = time.monotonic() + 10
                while not (live_dir / "index.m3u8").exists():
                    if time.monotonic() >= until or producer.poll() is not None:
                        raise RuntimeError("Local live producer did not start")
                    time.sleep(0.1)
                for name, options in (
                    ("limited-live", {"media-record-time": "4"}),
                    ("stopped-live", {}),
                    ("paused-live", {}),
                ):
                    gid = engine.add_uri(
                        f"{server.base_url}/live/index.m3u8",
                        {"out": f"{name}.mp4", **options},
                    )
                    if name != "limited-live":
                        until = time.monotonic() + 15
                        while time.monotonic() < until:
                            status = engine.rpc.call("aria2.tellStatus", [gid])
                            if (
                                int(
                                    status.get("media", {}).get(
                                        "completedDuration", "0"
                                    )
                                )
                                >= 2000
                            ):
                                break
                            if status["status"] == "error":
                                raise AssertionError(status)
                            time.sleep(0.1)
                        if name == "paused-live":
                            engine.rpc.call("aria2.pause", [gid])
                            wait(engine, gid, "paused")
                        engine.rpc.call("aria2.finishMedia", [gid])
                    status = wait(engine, gid)
                    assert status["media"]["live"] == "true", status
                    if name == "limited-live":
                        assert (
                            4000 <= int(status["media"]["completedDuration"]) <= 6000
                        ), status
                    results[name] = status["media"]["completedDuration"]
            finally:
                producer.terminate()
                producer.wait(timeout=10)

        dash_live = run.fixtures / "live-dash"
        dash_live.mkdir()
        with (run.logs / "dash-producer.log").open("wb") as log:
            producer = subprocess.Popen(
                [
                    ffmpeg,
                    "-v",
                    "error",
                    "-re",
                    "-stream_loop",
                    "-1",
                    "-i",
                    str(source),
                    "-map",
                    "0",
                    "-c",
                    "copy",
                    "-f",
                    "dash",
                    "-seg_duration",
                    "2",
                    "-window_size",
                    "5",
                    str(dash_live / "index.mpd"),
                ],
                stdout=log,
                stderr=log,
                env=native_env(),
            )
            try:
                until = time.monotonic() + 10
                while not (dash_live / "index.mpd").exists():
                    if time.monotonic() >= until or producer.poll() is not None:
                        raise RuntimeError("Local DASH producer did not start")
                    time.sleep(0.1)
                gid = engine.add_uri(
                    f"{server.base_url}/live-dash/index.mpd",
                    {"out": "live-dash.mp4", "media-record-time": "4"},
                )
                status = wait(engine, gid)
                assert status["media"]["live"] == "true", status
                assert int(status["media"]["completedDuration"]) >= 4000, status
                results["dashLive"] = status["media"]["completedDuration"]
            finally:
                producer.terminate()
                producer.wait(timeout=10)

        session = engine.root / "session.txt"
        engine.rpc.call("aria2.changeGlobalOption", [{"save-session": str(session)}])
        gid = engine.add_uri(
            f"{server.base_url}/hls/index.m3u8",
            {"out": "resumed.mp4", "max-download-limit": "32K"},
        )
        until = time.monotonic() + 20
        while time.monotonic() < until:
            status = engine.rpc.call("aria2.tellStatus", [gid])
            if int(status.get("media", {}).get("completedDuration", "0")) >= 2000:
                break
            time.sleep(0.1)
        engine.rpc.call("aria2.pause", [gid])
        paused = wait(engine, gid, "paused")
        assert int(paused["media"]["completedDuration"]) >= 2000, paused
        engine.rpc.call("aria2.saveSession")
        engine.stop()
        with sqlite3.connect(engine.root / "media/state.db") as db:
            cached = Path(
                db.execute(
                    "SELECT path FROM media_segments WHERE gid=? LIMIT 1", (gid,)
                ).fetchone()[0]
            )
        cached.write_bytes(b"damaged cached fragment")
        engine.start([f"--input-file={session}", f"--save-session={session}"])
        restored = wait(engine, gid, "paused")
        assert (
            restored["media"]["completedDuration"]
            == paused["media"]["completedDuration"]
        ), (paused, restored)
        assert restored["downloadSpeed"] == "0", restored
        engine.rpc.call("aria2.unpause", [gid])
        status = wait(engine, gid)
        actual = command(
            ffmpeg,
            "-v",
            "error",
            "-i",
            status["files"][0]["path"],
            "-map",
            "0:v",
            "-f",
            "hash",
            "-hash",
            "sha256",
            "-",
        ).strip()
        assert actual == expected, actual
        results["restart"] = (
            "paused progress retained; corrupted cache re-fetched; complete video hash matches"
        )

        for active in (False, True):
            gid = engine.add_uri(
                f"{server.base_url}/hls/index.m3u8",
                {
                    "out": f"removed-{active}.mp4",
                    "media-pause-after-probe": "false" if active else "true",
                    "max-download-limit": "16K",
                },
            )
            if active:
                wait(engine, gid, "active")
            else:
                wait(engine, gid, "paused")
            engine.rpc.call("aria2.remove", [gid])
            until = time.monotonic() + 5
            while True:
                tasks = engine.rpc.call("aria2.tellActive") + engine.rpc.call(
                    "aria2.tellWaiting", [0, 100]
                )
                if not any(task["gid"] == gid for task in tasks):
                    break
                if time.monotonic() >= until:
                    raise TimeoutError(f"Media removal did not finish: {gid}")
                time.sleep(0.1)
            assert not (engine.root / "media/tasks" / gid).exists(), gid
            with sqlite3.connect(engine.root / "media/state.db") as db:
                for table in (
                    "media_tasks",
                    "media_segments",
                    "media_tracks",
                    "media_identity",
                    "media_manifests",
                ):
                    assert (
                        db.execute(
                            f"SELECT COUNT(*) FROM {table} WHERE gid=?", (gid,)
                        ).fetchone()[0]
                        == 0
                    ), (table, gid)
        results["removal"] = (
            "active and paused tasks discard only their own recovery data"
        )
    return results


if __name__ == "__main__":
    raise SystemExit(run_validation("media", validate))
