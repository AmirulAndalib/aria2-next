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
from media.common import command, control_action, decoded_hash, probe

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

# Exercise controls on representations whose storage or addressing differs.
OPERATIONS = {
    "hls-ts": [(12, "force-pause")],
    "hls-fmp4": [(12, "restart")],
    "hls-subtitles": [(12, "restart")],
    "dash-avc": [(12, "force-pause"), (24, "restart")],
    "dash-webm": [(2, "pause"), (7, "restart")],
    "audio-only": [(2, "restart")],
    "hls-hevc": [(12, "pause")],
    "hls-av1": [(12, "restart")],
    "hls-range-restart": [(12, "restart")],
    "hls-aes-ts": [(120, "restart")],
    "hls-aes-fmp4": [(120, "crash")],
    "dash-periods": [(750, "restart")],
    "hls-live": [(12, "pause"), (24, "restart"), (36, "crash"), (60, "finish-paused")],
    "dash-live": [(12, "pause"), (24, "restart"), (36, "crash"), (60, "finish-paused")],
}
for case in CASES:
    case["actions"] = OPERATIONS.get(case["name"], [])
    if case["name"] in ("dash-webm", "audio-only"):
        case.update(control_metric="elapsed", rate="128K")
    if case["name"] == "dash-live":
        case["duration"] = 60
BASE_CASES = {case["name"]: case for case in CASES}
for protocol in ("hls", "dash"):
    live = BASE_CASES[f"{protocol}-live"]
    CASES.extend(
        [
            dict(live, name=f"{protocol}-live-auto", duration=30, actions=[]),
            dict(
                live,
                name=f"{protocol}-live-finish",
                duration=30,
                actions=[(30, "finish")],
            ),
            dict(
                live,
                name=f"{protocol}-live-mkv",
                duration=30,
                format="mkv",
                actions=[(12, "pause"), (30, "finish")],
            ),
            dict(
                live,
                name=f"{protocol}-live-soak",
                url=DASHIF + "livesim2/periods_60/segtimeline_1/testpic_2s/Manifest.mpd"
                if protocol == "dash"
                else live["url"],
                duration=600,
                soak=True,
                actions=[
                    (60, "pause"),
                    (120, "restart"),
                    (240, "crash"),
                    (360, "pause"),
                    (600, "finish-paused"),
                ],
            ),
        ]
    )
    for mode, base in (
        ("vod", BASE_CASES["hls-ts" if protocol == "hls" else "dash-avc"]),
        ("live", live),
    ):
        for state in ("active", "paused"):
            if mode == "vod" and state == "active":
                state = "force"
            CASES.append(
                dict(
                    base,
                    name=f"{protocol}-{mode}-remove-{state}",
                    actions=[(6, f"remove-{state}")],
                    rate="128K",
                )
            )
for addressing in ("segtimeline", "segtimelinenr"):
    CASES.append(
        dict(
            BASE_CASES["dash-live"],
            name=f"dash-live-{addressing}",
            url=DASHIF + f"livesim2/{addressing}_1/testpic_2s/Manifest.mpd",
        )
    )
CASES.append(
    dict(
        BASE_CASES["dash-live"],
        name="dash-live-periods",
        duration=90,
        url=DASHIF + "livesim2/periods_60/segtimeline_1/testpic_2s/Manifest.mpd",
        actions=[(12, "pause"), (40, "restart"), (90, "finish-paused")],
    )
)
CASES.append(
    dict(
        BASE_CASES["dash-live"],
        name="dash-live-periods-number",
        duration=90,
        url=DASHIF + "livesim2/periods_60/testpic_2s/Manifest.mpd",
        actions=[(12, "pause"), (40, "restart"), (90, "finish-paused")],
    )
)
CASES.append(
    dict(
        BASE_CASES["dash-live"],
        name="dash-live-subtitles",
        duration=90,
        url=DASHIF
        + "livesim2/periods_60/segtimeline_1/timesubswvtt_en/testpic_2s/Manifest.mpd",
        subtitles="en",
        format="mkv",
        actions=[(12, "pause"), (40, "restart"), (90, "finish-paused")],
    )
)
for name, codec, child in (
    (
        "hls-ac3",
        "ac-3",
        "Job932393e2-1e4f-4fdb-ab59-0d201f752656-107660254-Transcodeaudio_en_surround51dd_audio/prog_index.m3u8",
    ),
    (
        "hls-eac3",
        "ec-3",
        "Job932393e2-1e4f-4fdb-ab59-0d201f752656-107660254-Transcode_audio_full_en_atmos_0_1-en_audio/prog_index.m3u8",
    ),
    (
        "hls-he-aac",
        "mp4a.40.5",
        "Job932393e2-1e4f-4fdb-ab59-0d201f752656-107660254-Transcode_audio_en_stereo64_audio/prog_index.m3u8",
    ),
    (
        "hls-he-aac-v2",
        "mp4a.40.29",
        "Job932393e2-1e4f-4fdb-ab59-0d201f752656-107660254-Transcode_audio_en_stereo32w441_audio/prog_index.m3u8",
    ),
):
    CASES.append(
        dict(
            name=name,
            url=APPLE + "adv_dv_atmos/main.m3u8",
            audio=codec,
            language="en-US",
            duration=98.432,
            actions=[(12, "restart")],
            reference={"a": APPLE + "adv_dv_atmos/" + child},
        )
    )
for codec in ("opus", "flac"):
    CASES.append(
        dict(
            name=f"hls-live-{codec}",
            url=BASE_CASES["hls-live"]["url"],
            audio=codec,
            duration=20,
            live=True,
            format="mkv",
            actions=[],
        )
    )


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
                "max-download-limit": case.get("rate", "0"),
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
        actions = case.get("actions", [])
        if case.get("live") and not any(
            name.startswith(("finish", "remove")) for _, name in actions
        ):
            options["media-record-time"] = str(case["duration"])
        engine.rpc.call("aria2.changeOption", [gid, options])
        engine.rpc.call("aria2.unpause", [gid])
        result.update(
            gid=gid,
            selection=options,
            probeSeconds=round(time.monotonic() - started, 3),
        )
        downloaded = time.monotonic()
        active_elapsed = 0.0
        last_poll = downloaded
        last_advance = downloaded
        last_counters = (0, 0)
        previous = 0
        action_index = 0
        result["actions"] = []
        finalize_start = None
        with (evidence / "progress.jsonl").open("w", encoding="utf-8") as history:
            while time.monotonic() - downloaded < max(
                900, case["duration"] * 3 if case.get("live") else 900
            ):
                status = engine.rpc.call("aria2.tellStatus", [gid])
                media = status.get("media", {})
                now = time.monotonic()
                if media.get("state") in ("downloading", "recording"):
                    active_elapsed += now - last_poll
                last_poll = now
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
                counters = (duration, int(media.get("downloadedLength", 0)))
                if counters != last_counters or int(status.get("downloadSpeed", 0)) > 0:
                    last_advance = time.monotonic()
                    last_counters = counters
                elif time.monotonic() - last_advance > 60:
                    raise TimeoutError(
                        f"Media made no progress for 60 seconds: {status}"
                    )
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
                metric = (
                    active_elapsed
                    if case.get("control_metric") == "elapsed"
                    else duration / 1000
                )
                if (
                    action_index < len(actions)
                    and metric >= actions[action_index][0]
                    and media.get("state") in ("downloading", "recording")
                ):
                    action = actions[action_index][1]
                    result["actions"].append(control_action(engine, gid, action))
                    last_poll = time.monotonic()
                    last_advance = last_poll
                    action_index += 1
                    (evidence / "actions.json").write_text(
                        json.dumps(result["actions"], indent=2), encoding="utf-8"
                    )
                    if action.startswith("remove-"):
                        result.update(success=True, removed=True)
                        return result
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
        if action_index != len(actions):
            raise AssertionError(
                f"Task completed before all controls ran: {actions[action_index:]}"
            )
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
    if case.get("audio") and case["audio"] != "muxed":
        expected_audio = (
            "aac"
            if case["audio"].startswith("mp4a")
            else {"ac-3": "ac3", "ec-3": "eac3"}.get(case["audio"], case["audio"])
        )
        audio = next(s for s in info["streams"] if s["codec_type"] == "audio")
        if audio["codec_name"] != expected_audio:
            raise AssertionError(
                f"The engine selected a different audio codec: {audio}"
            )
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
        av_indexes = {
            s["index"] for s in info["streams"] if s["codec_type"] in ("audio", "video")
        }
        previous_packets = {}
        gaps = {}
        for packet in probe(
            ffprobe,
            str(output),
            "-show_packets",
            "-show_entries",
            "packet=stream_index,dts_time,duration_time",
        )["packets"]:
            index = packet["stream_index"]
            if index not in av_indexes or "dts_time" not in packet:
                continue
            timestamp = float(packet["dts_time"])
            if index in previous_packets:
                before, length = previous_packets[index]
                gap = timestamp - before - length
                if timestamp <= before or gap > 0.1:
                    raise AssertionError(
                        f"Live track {index} lost continuity: {before} -> {timestamp}"
                    )
                gaps[index] = max(gaps.get(index, 0), gap)
            previous_packets[index] = (timestamp, float(packet.get("duration_time", 0)))
        result["maxPacketGapMs"] = {
            str(i): round(gap * 1000, 3) for i, gap in gaps.items()
        }
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
        if case.get("subtitle_reference"):
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
                raise AssertionError(
                    "Subtitle content differs from the published source"
                )
            differences = []
            for text, intervals in source_cues.items():
                if len(intervals) != len(output_cues[text]):
                    raise AssertionError(
                        "Subtitle cue intervals were lost or duplicated"
                    )
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
        else:
            spans = sorted(
                span
                for spans in cue_spans(ffprobe, [evidence / "subtitles.vtt"]).values()
                for span in spans
            )
            if not spans or spans[0][0] > 2.1 or spans[-1][1] < duration - 2.1:
                raise AssertionError(
                    f"Timed live subtitles do not cover the recording: {spans}"
                )
            if any(b[0] - a[1] > 2.1 for a, b in zip(spans, spans[1:])):
                raise AssertionError("Timed live subtitle cues were lost")
            result["subtitles"] = {"cues": len(spans), "periodicCoverage": True}
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
    parser.add_argument(
        "--suite", choices=("standard", "soak", "all"), default="standard"
    )
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
    selected = [
        case
        for case in CASES
        if (
            case["name"] in args.case
            if args.case
            else args.suite == "all" or bool(case.get("soak")) == (args.suite == "soak")
        )
    ]
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
