#!/usr/bin/env python3
"""Complete public HLS/DASH downloads through the engine's native RPC contract."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
import time
import traceback
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from core.engine import EngineProcess
from core.runtime import REPOSITORY_ROOT, RunDirectory, sha256
from media.common import command, decoded_hash, probe

SHAKA = "https://storage.googleapis.com/shaka-demo-assets/"
APPLE = "https://devstreaming-cdn.apple.com/videos/streaming/examples/"
DASHIF = "https://livesim2.dashif.org/"

# Explicit source tracks provide an independent decoding reference. Keep the
# provider's master manifest intact; the engine selects its native track IDs.
CASES = [
    dict(
        name="hls-ts",
        url="https://test-streams.mux.dev/pts_shift/master.m3u8",
        video="avc1",
        height=270,
        audio="muxed",
        duration=165.09,
        reference={
            "v": "https://test-streams.mux.dev/pts_shift/25774983_7654066_lsid3f54xlucafyahfr_1@198000pb.m3u8",
            "a": "https://test-streams.mux.dev/pts_shift/25774983_7654066_lsid3f54xlucafyahfr_1@198000pb.m3u8",
        },
    ),
    dict(
        name="hls-fmp4",
        url=SHAKA + "angel-one-hls/hls.m3u8",
        video="avc1",
        height=360,
        audio="mp4a.40.2",
        language="en",
        duration=60,
        reference={
            "v": SHAKA + "angel-one-hls/playlist_v-0360p-0750k-libx264.mp4.m3u8",
            "a": SHAKA + "angel-one-hls/playlist_a-eng-0128k-aac-2c.mp4.m3u8",
        },
    ),
    dict(
        name="hls-subtitles",
        url=SHAKA + "angel-one-hls/hls.m3u8",
        video="avc1",
        height=360,
        audio="mp4a.40.2",
        language="en",
        subtitles="en",
        format="mkv",
        duration=60,
        subtitle_reference=[
            SHAKA + f"angel-one-hls/s-en-s{i}.vtt" for i in range(1, 17)
        ],
    ),
    dict(
        name="dash-avc",
        url=SHAKA + "angel-one/dash.mpd",
        video="avc1",
        height=360,
        audio="mp4a.40.2",
        language="en",
        duration=60,
        reference={
            "v": SHAKA + "angel-one/video_360p_400k_h264.mp4",
            "a": SHAKA + "angel-one/audio_en_2c_128k_aac.mp4",
        },
    ),
    dict(
        name="dash-webm",
        url=SHAKA + "angel-one/dash.mpd",
        video="vp09",
        height=360,
        audio="opus",
        language="en",
        format="mkv",
        duration=60,
        reference={
            "v": SHAKA + "angel-one/video_360p_277k_vp9.webm",
            "a": SHAKA + "angel-one/audio_en_2c_64k_opus.webm",
        },
    ),
    dict(
        name="audio-only",
        url=SHAKA + "angel-one/dash.mpd",
        audio="mp4a.40.2",
        language="en",
        duration=60,
        reference={"a": SHAKA + "angel-one/audio_en_2c_128k_aac.mp4"},
    ),
    dict(
        name="dash-av1",
        url=DASHIF + "vod/testpic_2s_av1/Manifest.mpd",
        video="av01",
        height=720,
        audio="mp4a.40.2",
        duration=10,
    ),
    dict(
        name="hls-hevc",
        url=APPLE + "adv_dv_atmos/main.m3u8",
        video="hvc1",
        height=270,
        duration=98.432,
    ),
    dict(
        name="hls-av1",
        url=APPLE + "av1-sample/av1-sample.m3u8",
        video="av01",
        height=270,
        duration=98.432,
    ),
    dict(
        name="hls-range-restart",
        url=APPLE + "bipbop_adv_example_hevc/master.m3u8",
        video="avc1",
        height=234,
        duration=600,
        restart=True,
        reference={"v": APPLE + "bipbop_adv_example_hevc/v1/prog_index.m3u8"},
    ),
    dict(
        name="hls-aes-ts",
        url=SHAKA + "sintel-ts-aes-key-rotation/master.m3u8",
        video="avc1",
        height=110,
        audio="mp4a.40.2",
        language="eng",
        duration=888,
        reference={
            "v": SHAKA + "sintel/v-0144p-0100k-libx264.mp4",
            "a": SHAKA + "sintel/a-eng-0128k-aac.mp4",
        },
    ),
    dict(
        name="hls-aes-fmp4",
        url=SHAKA + "sintel-fmp4-aes/master.m3u8",
        video="avc1",
        height=110,
        audio="mp4a.40.2",
        language="en",
        duration=888,
        reference={
            "v": SHAKA + "sintel/v-0144p-0100k-libx264.mp4",
            "a": SHAKA + "sintel/a-eng-0128k-aac.mp4",
        },
    ),
    dict(
        name="dash-periods",
        url="https://media.axprod.net/TestVectors/v7-Clear/Manifest_MultiPeriod.mpd",
        video="avc1",
        height=288,
        audio="mp4a",
        language="en",
        duration=1468,
    ),
    dict(
        name="hls-live",
        url="https://storage.googleapis.com/shaka-live-assets/player-source.m3u8",
        video="avc1",
        height=480,
        audio="mp4a",
        duration=60,
        live=True,
        finish=True,
    ),
    dict(
        name="dash-live",
        url=DASHIF + "livesim2/testpic_2s/Manifest.mpd",
        video="avc1",
        height=360,
        audio="mp4a",
        duration=90,
        live=True,
    ),
]


def select(tracks: list[dict], kind: str, codec: str, case: dict) -> str:
    candidates = [
        t
        for t in tracks
        if t["type"] in (("video", "muxed") if kind == "video" else (kind,))
        and codec.lower() in t["codec"].lower()
    ]
    if kind == "video":
        candidates = [t for t in candidates if int(t["height"]) == case["height"]]
    if kind == "audio" and case.get("language"):
        candidates = [t for t in candidates if t["language"] == case["language"]]
    if not candidates:
        raise RuntimeError(f"No {kind} matches {codec}: {tracks}")
    return min(candidates, key=lambda t: int(t["bandwidth"]))["id"]


def cue_spans(ffprobe: str, files: list[Path]) -> dict:
    cues = {}
    for file in files:
        for packet in probe(
            ffprobe,
            str(file),
            "-select_streams",
            "s",
            "-show_packets",
            "-show_data_hash",
            "sha256",
            "-show_entries",
            "packet=pts_time,duration_time,data_hash",
        ).get("packets", []):
            start = float(packet["pts_time"])
            cues.setdefault(packet["data_hash"], []).append(
                (start, start + float(packet["duration_time"]))
            )
    # HLS repeats cues across segment boundaries. Compare their complete spans.
    for text, intervals in cues.items():
        merged = []
        for start, end in sorted(set(intervals)):
            if merged and start <= merged[-1][1] + 0.001:
                merged[-1] = (merged[-1][0], max(end, merged[-1][1]))
            else:
                merged.append((start, end))
        cues[text] = merged
    return cues


def validate(
    run: RunDirectory,
    engine_path: Path,
    case: dict,
    ffmpeg: str,
    ffprobe: str,
    curl: str,
) -> dict:
    name = case["name"]
    evidence = run.fixtures / name
    evidence.mkdir()
    command(
        curl,
        "--fail",
        "--location",
        "--max-time",
        "30",
        "--silent",
        "--show-error",
        "--dump-header",
        str(evidence / "headers.txt"),
        "--output",
        str(evidence / "manifest"),
        case["url"],
    )
    result = {"source": case["url"], "manifestSha256": sha256(evidence / "manifest")}
    started = time.monotonic()
    container = case.get("format", "mp4")
    with EngineProcess(run, name, engine_path) as engine:
        gid = engine.add_uri(
            case["url"],
            {
                "out": f"{name}.{container}",
                "media-format": container,
                "media-pause-after-probe": "true",
                "max-tries": "2",
                "retry-wait": "1",
                "connect-timeout": "10",
                "timeout": "20",
            },
        )
        status = engine.rpc.wait_status(gid, "paused", 90)
        tracks = status["media"]["tracks"]
        (evidence / "tracks.json").write_text(
            json.dumps(tracks, indent=2), encoding="utf-8"
        )
        options = {
            "media-pause-after-probe": "false",
            "media-video": "none",
            "media-audio": "none",
        }
        for kind in ("video", "audio"):
            codec = case.get(kind)
            if codec:
                options[f"media-{kind}"] = (
                    "best" if codec == "muxed" else select(tracks, kind, codec, case)
                )
        if case.get("subtitles"):
            options["media-subtitles"] = case["subtitles"]
        if case.get("live") and not case.get("finish"):
            options["media-record-time"] = str(case["duration"])
        engine.rpc.call("aria2.changeOption", [gid, options])
        engine.rpc.call("aria2.unpause", [gid])
        result.update(
            gid=gid,
            selection=options,
            probeSeconds=round(time.monotonic() - started, 3),
        )
        downloaded = time.monotonic()
        previous = 0
        recovered = False
        finishing = False
        finalize_start = None
        session = engine.root / "session.txt"
        engine.rpc.call("aria2.changeGlobalOption", [{"save-session": str(session)}])
        with (evidence / "progress.jsonl").open("w", encoding="utf-8") as history:
            while time.monotonic() - downloaded < 900:
                status = engine.rpc.call("aria2.tellStatus", [gid])
                media = status.get("media", {})
                history.write(
                    json.dumps(
                        {
                            "elapsedSeconds": round(time.monotonic() - downloaded, 3),
                            "status": status["status"],
                            "state": media.get("state"),
                            "completedDuration": media.get("completedDuration"),
                            "downloadedLength": media.get("downloadedLength"),
                            "downloadSpeed": status.get("downloadSpeed"),
                        }
                    )
                    + "\n"
                )
                history.flush()
                duration = int(media.get("completedDuration", 0))
                if status["status"] == "error":
                    raise RuntimeError(json.dumps(status))
                if duration < previous:
                    raise AssertionError(
                        f"Media progress regressed: {previous} -> {duration}"
                    )
                previous = duration
                if media.get("state") == "finalizing" and finalize_start is None:
                    finalize_start = time.monotonic()
                if status["status"] == "complete":
                    break
                if (
                    not recovered
                    and duration >= 12000
                    and (case.get("restart") or case.get("finish"))
                ):
                    engine.rpc.call("aria2.pause", [gid])
                    paused = engine.rpc.wait_status(gid, "paused", 20)
                    if case.get("restart"):
                        engine.rpc.call("aria2.saveSession")
                        engine.stop()
                        engine.start(
                            [f"--input-file={session}", f"--save-session={session}"]
                        )
                        restored = engine.rpc.wait_status(gid, "paused", 30)
                        if (
                            restored["media"]["completedDuration"]
                            != paused["media"]["completedDuration"]
                        ):
                            raise AssertionError(
                                "Restart lost committed media progress"
                            )
                    else:
                        time.sleep(2)
                    engine.rpc.call("aria2.unpause", [gid])
                    recovered = True
                if (
                    case.get("finish")
                    and duration >= case["duration"] * 1000
                    and not finishing
                ):
                    engine.rpc.call("aria2.finishMedia", [gid])
                    finishing = True
                time.sleep(0.25)
            else:
                raise TimeoutError(f"Media task timed out: {status}")
        elapsed = time.monotonic() - downloaded
        output = Path(status["files"][0]["path"])
        if (
            status["media"]["state"] != "complete"
            or int(status["completedLength"]) != output.stat().st_size
        ):
            raise AssertionError(f"Invalid completion: {status}")
        if case.get("restart") and not recovered:
            raise AssertionError("Download completed before restart was exercised")
        result.update(
            status=status,
            output=str(output),
            bytes=output.stat().st_size,
            sha256=sha256(output),
            transferSeconds=round(elapsed, 3),
            finalizeSeconds=round(time.monotonic() - finalize_start, 3)
            if finalize_start
            else None,
        )
    info = probe(ffprobe, str(output), "-show_streams", "-show_format")
    (evidence / "output.json").write_text(json.dumps(info, indent=2), encoding="utf-8")
    types = {s["codec_type"] for s in info["streams"]}
    expected = {kind for kind in ("video", "audio") if case.get(kind)}
    if case.get("subtitles"):
        expected.add("subtitle")
    if types != expected:
        raise AssertionError(f"Output tracks differ: {types} != {expected}")
    codec_names = {"avc1": "h264", "hvc1": "hevc", "av01": "av1", "vp09": "vp9"}
    if case.get("video"):
        video = next(s for s in info["streams"] if s["codec_type"] == "video")
        if (
            video["height"] != case["height"]
            or video["codec_name"] != codec_names[case["video"]]
        ):
            raise AssertionError(
                f"The engine selected a different video representation: {video}"
            )
    duration = float(info["format"]["duration"])
    tolerance = 10 if case.get("live") else 5 if case.get("subtitles") else 0.25
    if abs(duration - case["duration"]) > tolerance:
        raise AssertionError(f"Incomplete duration: {duration} != {case['duration']}")
    if case.get("live"):
        starts = [float(s["start_time"]) for s in info["streams"]]
        if any(abs(start) > 0.1 for start in starts):
            raise AssertionError(
                f"Live tracks do not share a zero-based timeline: {starts}"
            )
    hashes = {}
    for kind, stream in (("video", "v"), ("audio", "a")):
        if kind not in expected:
            continue
        hashes[kind] = decoded_hash(ffmpeg, str(output), stream)
        reference = case.get("reference", {}).get(stream)
        if reference:
            if reference.endswith((".mp4", ".webm")):
                source = evidence / f"reference-{stream}{Path(reference).suffix}"
                command(
                    curl,
                    "--fail",
                    "--location",
                    "--silent",
                    "--show-error",
                    "--max-time",
                    "120",
                    "--output",
                    str(source),
                    reference,
                )
                reference = str(source)
            # Encoded audio may extend past the MPD presentation boundary.
            # Decode the whole output above, then compare the advertised window.
            window = case["duration"] if stream == "a" else None
            expected_hash = decoded_hash(ffmpeg, reference, stream, window)
            actual_hash = decoded_hash(ffmpeg, str(output), stream, window)
            (evidence / f"reference-{stream}.txt").write_text(
                expected_hash + "\n", encoding="utf-8"
            )
            if actual_hash != expected_hash:
                raise AssertionError(
                    f"Decoded {kind} differs: {actual_hash} != {expected_hash}"
                )
    if "subtitle" in expected:
        captions = command(
            ffmpeg,
            "-v",
            "error",
            "-i",
            str(output),
            "-map",
            "0:s:0",
            "-f",
            "webvtt",
            "-",
        )
        (evidence / "subtitles.vtt").write_text(
            captions, encoding="utf-8", newline="\n"
        )
        if "-->" not in captions:
            raise AssertionError("Selected subtitles contain no cues")
        originals = []
        for index, url in enumerate(case["subtitle_reference"]):
            file = evidence / f"reference-subtitle-{index}.vtt"
            command(
                curl,
                "--fail",
                "--location",
                "--silent",
                "--show-error",
                "--max-time",
                "30",
                "--output",
                str(file),
                url,
            )
            originals.append(file)
        source_cues = cue_spans(ffprobe, originals)
        output_cues = cue_spans(ffprobe, [evidence / "subtitles.vtt"])
        if source_cues.keys() != output_cues.keys():
            raise AssertionError("Subtitle content differs from the published source")
        differences = []
        for text, intervals in source_cues.items():
            if len(intervals) != len(output_cues[text]):
                raise AssertionError("Subtitle cue intervals were lost or duplicated")
            differences.extend(
                abs(a - b)
                for pair, actual in zip(intervals, output_cues[text])
                for a, b in zip(pair, actual)
            )
        if max(differences, default=0) > 0.05:
            raise AssertionError(
                f"Subtitle timestamps differ: {max(differences)} seconds"
            )
        result["subtitles"] = {
            "cues": sum(map(len, source_cues.values())),
            "maxTimestampErrorMs": round(max(differences, default=0) * 1000, 3),
        }
    result.update(duration=duration, decoded=hashes, success=True)
    if name == "hls-ts":
        cli_output = run.downloads / "cli.mp4"
        cli_started = time.monotonic()
        stdout = command(
            str(engine_path),
            "--no-conf=true",
            "--enable-dht=false",
            "--bt-port-mapping=false",
            "--bt-enable-lpd=false",
            f"--state-dir={run.state / 'cli'}",
            f"--dir={run.downloads}",
            "--out=cli.mp4",
            "--max-tries=2",
            "--connect-timeout=10",
            "--timeout=20",
            "--show-console-readout=false",
            case["reference"]["v"],
        )
        (evidence / "cli.log").write_text(stdout, encoding="utf-8")
        for kind, stream in (("video", "v"), ("audio", "a")):
            if decoded_hash(ffmpeg, str(cli_output), stream) != hashes[kind]:
                raise AssertionError(f"CLI {kind} differs from RPC output")
        result["cli"] = {
            "bytes": cli_output.stat().st_size,
            "elapsedSeconds": round(time.monotonic() - cli_started, 3),
        }
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--engine",
        type=Path,
        default=REPOSITORY_ROOT
        / "build/default"
        / ("aria2-next.exe" if os.name == "nt" else "aria2-next"),
    )
    parser.add_argument("--case", action="append", choices=[c["name"] for c in CASES])
    args = parser.parse_args()
    ffmpeg, ffprobe = shutil.which("ffmpeg"), shutil.which("ffprobe")
    curl = (
        str(Path(os.environ["SystemRoot"]) / "System32/curl.exe")
        if os.name == "nt"
        else shutil.which("curl")
    )
    if not all((ffmpeg, ffprobe, curl)):
        parser.error("FFmpeg, ffprobe and curl must be installed")
    run = RunDirectory("public-media", True)
    executable = run.root / args.engine.name
    shutil.copy2(args.engine, executable)
    selected = [case for case in CASES if not args.case or case["name"] in args.case]
    report = {
        "revision": command(
            "git", "-C", str(REPOSITORY_ROOT), "rev-parse", "HEAD"
        ).strip(),
        "dirty": command(
            "git", "-C", str(REPOSITORY_ROOT), "status", "--porcelain"
        ).splitlines(),
        "engineSha256": sha256(args.engine),
        "engineVersion": command(str(args.engine), "--version"),
        "ffmpegVersion": command(ffmpeg, "-version").splitlines()[0],
        "cases": {},
        "planned": [case["name"] for case in selected],
    }
    print(f"Run directory: {run.root}", flush=True)
    for case in selected:
        started = time.monotonic()
        print(f"START {case['name']}", flush=True)
        try:
            result = validate(run, executable, case, ffmpeg, ffprobe, curl)
        except Exception as error:
            result = {
                "success": False,
                "error": str(error),
                "traceback": traceback.format_exc(),
            }
        result["elapsedSeconds"] = round(time.monotonic() - started, 3)
        report["cases"][case["name"]] = result
        report["success"] = len(report["cases"]) == len(selected) and all(
            c["success"] for c in report["cases"].values()
        )
        run.write_result(report)
        print(
            f"{'PASS' if result['success'] else 'FAIL'} {case['name']}: {result.get('error', result['elapsedSeconds'])}",
            flush=True,
        )
    return 0 if report["success"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
