#!/usr/bin/env python3
"""Local native-library media validation. No public media services are used."""

from __future__ import annotations

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
from media.common import command, decoded_hash, native_env, probe


def wait(
    engine: EngineProcess, gid: str, state: str = "complete", timeout: float = 30
) -> dict:
    return engine.rpc.wait_status(gid, state, timeout)


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
    for name in ("hls", "fmp4", "fmp4-switch", "dash", "byterange", "encrypted"):
        directory = run.fixtures / name
        directory.mkdir()
        args = [ffmpeg, "-v", "error", "-i", str(source), "-map", "0", "-c", "copy"]
        if name == "dash":
            args += [
                "-f",
                "dash",
                "-seg_duration",
                "2",
                (directory / "index.mpd").as_posix(),
            ]
        else:
            args += ["-f", "hls", "-hls_time", "2", "-hls_list_size", "0"]
            if name.startswith("fmp4"):
                args += ["-hls_segment_type", "fmp4"]
            if name == "fmp4-switch":
                args += [
                    "-streamid",
                    "0:11",
                    "-streamid",
                    "1:12",
                    "-hls_segment_options",
                    "use_stream_ids_as_track_ids=1",
                ]
            if name == "byterange":
                args += ["-hls_flags", "single_file"]
            if name == "encrypted":
                key = directory / "key.bin"
                key.write_bytes(bytes(range(16)))
                key_info = directory / "key.txt"
                key_info.write_text(f"key.bin\n{key}\n")
                args += ["-hls_key_info_file", str(key_info)]
            args += [(directory / "index.m3u8").as_posix()]
        command(*args)
    hls_manifest = run.fixtures / "hls/index.m3u8"
    hls_manifest.write_text(
        hls_manifest.read_text().replace(
            "#EXT-X-MEDIA-SEQUENCE:0", "#EXT-X-MEDIA-SEQUENCE:3456"
        )
    )
    openssl = shutil.which("openssl")
    if not openssl:
        raise RuntimeError("OpenSSL is required for the encrypted fMP4 fixtures")
    encrypted_fmp4 = run.fixtures / "encrypted-fmp4"
    encrypted_fmp4.mkdir()
    key = bytes(range(16))
    (encrypted_fmp4 / "key.bin").write_bytes(key)
    for media in (run.fixtures / "fmp4").iterdir():
        if media.suffix not in (".mp4", ".m4s"):
            continue
        command(
            openssl,
            "enc",
            "-aes-128-cbc",
            "-K",
            key.hex(),
            "-iv",
            "00" * 16,
            "-in",
            str(media),
            "-out",
            str(encrypted_fmp4 / media.name),
        )
    key_tag = f'#EXT-X-KEY:URI="key.bin",IV=0x{"00" * 16},METHOD=AES-128\n'
    for name in ("encrypted-init", "clear-init"):
        initialization = "init.mp4" if name == "encrypted-init" else "../fmp4/init.mp4"
        mapping = f'#EXT-X-MAP:URI="{initialization}"\n'
        tags = key_tag + mapping if name == "encrypted-init" else mapping + key_tag
        (encrypted_fmp4 / f"{name}.m3u8").write_text(
            "#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-TARGETDURATION:2\n"
            + tags
            + "".join(f"#EXTINF:2,\nindex{index}.m4s\n" for index in range(3))
            + "#EXT-X-ENDLIST\n"
        )
    switched = ["#EXTM3U", "#EXT-X-VERSION:7", "#EXT-X-TARGETDURATION:2"]
    for name in ("fmp4", "fmp4-switch"):
        if name == "fmp4-switch":
            switched.append("#EXT-X-DISCONTINUITY")
        switched.append(f'#EXT-X-MAP:URI="{name}/init.mp4"')
        for index in range(3):
            switched += ["#EXTINF:2,", f"{name}/index{index}.m4s"]
    (run.fixtures / "switch.m3u8").write_text(
        "\n".join(switched + ["#EXT-X-ENDLIST", ""])
    )
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
    clipped = copy.deepcopy(root)
    clipped.set("mediaPresentationDuration", "PT10S")
    for index, item in enumerate(clipped.findall(ns + "Period")):
        item.set("start", f"PT{index * 5}S")
        item.set("duration", "PT5S")
    ET.ElementTree(clipped).write(run.fixtures / "dash/clipped.mpd")
    higher = run.fixtures / "higher"
    higher.mkdir()
    command(
        ffmpeg,
        "-v",
        "error",
        "-i",
        str(source),
        "-f",
        "lavfi",
        "-i",
        "sine=frequency=880:sample_rate=48000",
        "-t",
        "6",
        "-map",
        "0:v",
        "-map",
        "1:a",
        "-vf",
        "scale=320:180",
        "-c:v",
        "libx264",
        "-preset",
        "ultrafast",
        "-g",
        "30",
        "-c:a",
        "aac",
        "-f",
        "hls",
        "-hls_time",
        "2",
        "-hls_list_size",
        "0",
        (higher / "index.m3u8").as_posix(),
    )
    for language, audio_source in (("en", source), ("fr", higher / "index.m3u8")):
        audio_dir = run.fixtures / f"audio-{language}"
        audio_dir.mkdir()
        if language == "fr":
            timestamp = "".join(
                f"\\x{byte:02x}" for byte in (126000).to_bytes(8, "big")
            )
            command(
                ffmpeg,
                "-v",
                "error",
                "-i",
                audio_source.as_posix(),
                "-map",
                "0:a",
                "-c",
                "copy",
                "-write_id3v2",
                "1",
                "-metadata",
                f"id3v2_priv.com.apple.streaming.transportStreamTimestamp={timestamp}",
                "-f",
                "adts",
                (audio_dir / "audio.aac").as_posix(),
            )
            (audio_dir / "index.m3u8").write_text(
                "#EXTM3U\n#EXT-X-TARGETDURATION:7\n#EXTINF:6.04,\naudio.aac\n#EXT-X-ENDLIST\n"
            )
            continue
        command(
            ffmpeg,
            "-v",
            "error",
            "-i",
            audio_source.as_posix(),
            "-map",
            "0:a",
            "-c",
            "copy",
            "-f",
            "hls",
            "-hls_time",
            "2",
            "-hls_list_size",
            "0",
            (audio_dir / "index.m3u8").as_posix(),
        )
    (run.fixtures / "quality.m3u8").write_text(
        '#EXTM3U\n#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="audio",NAME="English",LANGUAGE="en",URI="audio-en/index.m3u8"\n'
        '#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="audio",NAME="French",LANGUAGE="fr",URI="audio-fr/index.m3u8"\n'
        '#EXT-X-STREAM-INF:BANDWIDTH=400000,RESOLUTION=160x90,CODECS="avc1.42c00b,mp4a.40.2",AUDIO="audio"\nhls/index.m3u8\n'
        '#EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=320x180,CODECS="avc1.42c00b,mp4a.40.2",AUDIO="audio"\nhigher/index.m3u8\n'
    )
    offset_root = ET.parse(run.fixtures / "dash/index.mpd").getroot()
    offset_period = offset_root.find(ns + "Period")
    offset_period.clear()
    offset_period.set("start", "PT0S")
    for index, (kind, offset) in enumerate((("v", 5), ("a", 9))):
        name = f"offset-{kind}"
        directory = run.fixtures / name
        directory.mkdir()
        command(
            ffmpeg,
            "-v",
            "error",
            "-i",
            str(source),
            "-map",
            f"0:{kind}",
            "-c",
            "copy",
            "-output_ts_offset",
            str(offset),
            "-format_options",
            "movie_timescale=48000",
            "-f",
            "dash",
            "-seg_duration",
            "2",
            (directory / "index.mpd").as_posix(),
        )
        adaptation = (
            ET.parse(directory / "index.mpd")
            .getroot()
            .find(f"{ns}Period/{ns}AdaptationSet")
        )
        adaptation.set("id", str(index))
        base = ET.Element(ns + "BaseURL")
        base.text = f"{name}/"
        adaptation.insert(0, base)
        for template in adaptation.iter(ns + "SegmentTemplate"):
            template.set(
                "presentationTimeOffset",
                str(offset * int(template.get("timescale", "1"))),
            )
        offset_period.append(adaptation)
    ET.ElementTree(offset_root).write(
        run.fixtures / "offset.mpd", encoding="utf-8", xml_declaration=True
    )
    expected = decoded_hash(ffmpeg, str(source), "v")
    expected_audio = decoded_hash(ffmpeg, str(source), "a")
    results = {}
    subtitle_root = run.fixtures / "hls"
    (subtitle_root / "subtitles.m3u8").write_text(
        "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:2\n"
        + "".join(f"#EXTINF:2,\nsub{i}.vtt\n" for i in range(3))
        + "#EXT-X-ENDLIST\n"
    )
    for i in range(3):
        (subtitle_root / f"sub{i}.vtt").write_text(
            f"WEBVTT\nX-TIMESTAMP-MAP=LOCAL:00:00:00.000,MPEGTS:{126000 + i * 180000}\n"
            f"\n00:00:00.000 --> 00:00:01.500\nCaption {i}\n\n"
        )
    (subtitle_root / "master.m3u8").write_text(
        '#EXTM3U\n#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID="subs",NAME="English",LANGUAGE="en",URI="subtitles.m3u8"\n'
        '#EXT-X-STREAM-INF:BANDWIDTH=400000,RESOLUTION=160x90,CODECS="avc1.42c00b,mp4a.40.2",SUBTITLES="subs"\nindex.m3u8\n'
    )
    with (
        CaddyService(run, "media", run.fixtures) as server,
        EngineProcess(run, "media", engine_path) as engine,
    ):
        clear_audio = decoded_hash(
            ffmpeg, (run.fixtures / "fmp4/index.m3u8").as_posix(), "a", 6
        )
        for name in ("encrypted-init", "clear-init"):
            gid = engine.add_uri(
                f"{server.base_url}/encrypted-fmp4/{name}.m3u8", {"out": f"{name}.mp4"}
            )
            output = wait(engine, gid)["files"][0]["path"]
            assert decoded_hash(ffmpeg, output) == expected, name
            assert decoded_hash(ffmpeg, output, "a", 6) == clear_audio, name
        results["encryptedFmp4"] = (
            "key attribute order and independent initialization encryption"
        )
        for name in ("skewed-live", "audio-late"):
            skewed = run.fixtures / name
            skewed.mkdir()
            for kind, length in (
                ("v", "2" if name == "skewed-live" else "10"),
                ("a", "10"),
            ):
                directory = skewed / kind
                directory.mkdir()
                manifest = directory / "index.m3u8"
                command(
                    ffmpeg,
                    "-v",
                    "error",
                    "-i",
                    str(source),
                    "-map",
                    f"0:{kind}",
                    "-c",
                    "copy",
                    "-output_ts_offset",
                    "4" if name == "audio-late" and kind == "a" else "0",
                    "-f",
                    "hls",
                    "-hls_time",
                    length,
                    "-hls_list_size",
                    "0",
                    "-hls_segment_type",
                    "fmp4",
                    manifest.as_posix(),
                )
                manifest.write_text(
                    manifest.read_text(encoding="utf-8").replace("#EXT-X-ENDLIST", ""),
                    encoding="utf-8",
                )
            (skewed / "master.m3u8").write_text(
                '#EXTM3U\n#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="audio",NAME="Audio",URI="a/index.m3u8"\n'
                '#EXT-X-STREAM-INF:BANDWIDTH=400000,RESOLUTION=160x90,CODECS="avc1.42c00b,mp4a.40.2",AUDIO="audio"\nv/index.m3u8\n',
                encoding="utf-8",
            )
            for container in ("mp4", "mkv"):
                gid = engine.add_uri(
                    f"{server.base_url}/{name}/master.m3u8",
                    {
                        "out": f"{name}.{container}",
                        "media-format": container,
                        "media-record-time": "2",
                    },
                )
                status = wait(engine, gid)
                output = status["files"][0]["path"]
                info = probe(ffprobe, output, "-show_streams", "-show_format")
                assert abs(float(info["format"]["duration"]) - 2) < 0.1, info
                assert all(
                    abs(float(s["start_time"])) < 0.1 for s in info["streams"]
                ), info
                assert 2000 <= int(status["media"]["completedDuration"]) < 2100, status
                decoded_hash(ffmpeg, output, "v")
                decoded_hash(ffmpeg, output, "a")
        results["liveAlignment"] = (
            "Independent playlist edges share one recording window"
        )
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
            actual = decoded_hash(ffmpeg, str(output), "v")
            assert actual == expected, (name, actual, expected)
            info = probe(ffprobe, str(output), "-show_streams", "-show_format")
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
        info = probe(ffprobe, status["files"][0]["path"], "-show_format")
        assert abs(float(info["format"]["duration"]) - 12) < 0.2, info
        results["multiPeriod"] = info["format"]["duration"]

        gid = engine.add_uri(
            f"{server.base_url}/dash/clipped.mpd", {"out": "clipped.mp4"}
        )
        status = wait(engine, gid)
        info = probe(
            ffprobe,
            status["files"][0]["path"],
            "-count_frames",
            "-show_entries",
            "stream=codec_type,nb_read_frames",
            "-show_format",
        )
        assert abs(float(info["format"]["duration"]) - 10) < 0.1, info
        assert (
            next(s for s in info["streams"] if s["codec_type"] == "video")[
                "nb_read_frames"
            ]
            == "150"
        ), info
        decoded_hash(ffmpeg, status["files"][0]["path"], "a")
        results["periodBoundaries"] = (
            "overhanging fragments clipped to the declared periods"
        )

        gid = engine.add_uri(
            f"{server.base_url}/quality.m3u8",
            {"out": "quality.mp4", "media-pause-after-probe": "true"},
        )
        tracks = wait(engine, gid, "paused")["media"]["tracks"]
        assert {t["language"] for t in tracks if t["type"] == "audio"} == {
            "en",
            "fr",
        }, tracks
        low = next(t["id"] for t in tracks if t["height"] == "90")
        engine.rpc.call(
            "aria2.changeOption",
            [
                gid,
                {
                    "media-video": low,
                    "media-audio": "fr",
                    "media-pause-after-probe": "false",
                },
            ],
        )
        engine.rpc.call("aria2.unpause", [gid])
        output = wait(engine, gid)["files"][0]["path"]
        assert decoded_hash(ffmpeg, output) == expected
        assert decoded_hash(ffmpeg, output, "a") == decoded_hash(
            ffmpeg, (higher / "index.m3u8").as_posix(), "a"
        )
        results["initialSelection"] = (
            "selected video and language applied before the first media packet"
        )

        gid = engine.add_uri(f"{server.base_url}/switch.m3u8", {"out": "switch.mp4"})
        status = wait(engine, gid)
        switched_info = probe(
            ffprobe,
            status["files"][0]["path"],
            "-count_frames",
            "-show_entries",
            "stream=codec_type,nb_read_frames",
            "-show_format",
        )
        assert abs(float(switched_info["format"]["duration"]) - 12) < 0.2, switched_info
        assert (
            next(s for s in switched_info["streams"] if s["codec_type"] == "video")[
                "nb_read_frames"
            ]
            == "180"
        ), switched_info
        results["initializationSwitch"] = (
            "both initialization sections and 180 video frames retained"
        )

        gid = engine.add_uri(f"{server.base_url}/offset.mpd", {"out": "offset.mp4"})
        status = wait(engine, gid)
        for kind, expected_hash in (("v", expected), ("a", expected_audio)):
            actual = decoded_hash(ffmpeg, status["files"][0]["path"], kind)
            assert actual == expected_hash, (kind, actual, expected_hash)
        streams = probe(ffprobe, status["files"][0]["path"], "-show_streams")["streams"]
        assert all(abs(float(stream["start_time"])) < 0.05 for stream in streams), (
            streams
        )
        results["presentationOffsets"] = (
            "different audio/video offsets removed; both decoded hashes match"
        )

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
        info = probe(ffprobe, status["files"][0]["path"], "-show_streams")
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
        packets = probe(
            ffprobe,
            status["files"][0]["path"],
            "-select_streams",
            "s",
            "-show_entries",
            "packet=pts_time",
        )["packets"]
        assert len(packets) == 3 and all(
            abs(float(packet["pts_time"]) - index * 2) < 0.1
            for index, packet in enumerate(packets)
        ), packets
        results["subtitles"] = "caption content and mapped timestamps retained"

        gid = engine.add_uri(
            f"{server.base_url}/hls/master.m3u8",
            {
                "out": "missing-language.mkv",
                "media-format": "mkv",
                "media-subtitles": "missing",
            },
        )
        status = wait(engine, gid, "error")
        assert "unavailable" in status["media"]["error"], status
        assert not Path(status["files"][0]["path"]).exists(), status
        engine.rpc.call("aria2.removeDownloadResult", [gid])

        blocked_directory = run.fixtures / "blocked-directory"
        blocked_directory.write_text("regular file")
        gid = engine.add_uri(
            f"{server.base_url}/hls/index.m3u8",
            {"dir": str(blocked_directory), "out": "blocked.mp4"},
        )
        status = wait(engine, gid, "error")
        assert "Media file operation failed" in status["errorMessage"], status
        engine.rpc.call("aria2.removeDownloadResult", [gid])
        results["selectionAndFileErrors"] = (
            "missing language rejected; native filesystem errors remain valid RPC JSON"
        )

        gid = engine.add_uri(
            f"{server.base_url}/hls/index.m3u8",
            {"out": "hls-audio.mkv", "media-format": "mkv", "media-video": "none"},
        )
        status = wait(engine, gid)
        info = probe(ffprobe, status["files"][0]["path"], "-show_streams")
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
                    actual = decoded_hash(ffmpeg, status["files"][0]["path"], "v")
                    assert actual == expected, status
                else:
                    assert (
                        status["media"]["state"] == "error" and status["media"]["error"]
                    ), status
                    assert not Path(status["files"][0]["path"]).exists(), status
                    engine.rpc.call("aria2.removeDownloadResult", [gid])
                    assert not (engine.root / "media/tasks" / gid).exists(), gid
            live_playlist = playlist.replace("#EXT-X-ENDLIST", "")
            for index, state in enumerate(("Started", "loaded", "refresh")):
                faults.stub(
                    {
                        "scenarioName": "live-refresh-failure",
                        "requiredScenarioState": state,
                        **(
                            {"newScenarioState": ("loaded", "refresh")[index]}
                            if index < 2
                            else {}
                        ),
                        "request": {"method": "GET", "urlPath": "/refresh.m3u8"},
                        "response": {
                            "status": 200,
                            "headers": {
                                "Content-Type": "application/vnd.apple.mpegurl"
                            },
                            "body": live_playlist,
                        }
                        if index < 2
                        else {"status": 503},
                    }
                )
            gid = engine.add_uri(
                f"{faults.base_url}/refresh.m3u8",
                {"out": "refresh-failure.mp4", "max-tries": "1"},
            )
            status = wait(engine, gid, "error")
            assert "503" in status["media"]["error"], status
            assert not Path(status["files"][0]["path"]).exists(), status
            engine.rpc.call("aria2.removeDownloadResult", [gid])
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
                    (live_dir / "index.m3u8").as_posix(),
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
                    ("resumed-live", {}),
                    ("expired-live", {}),
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
                        if name in ("paused-live", "resumed-live", "expired-live"):
                            engine.rpc.call("aria2.pause", [gid])
                            paused = wait(engine, gid, "paused")
                        if name in ("resumed-live", "expired-live"):
                            time.sleep(3 if name == "resumed-live" else 13)
                            engine.rpc.call("aria2.unpause", [gid])
                            if name == "expired-live":
                                status = wait(engine, gid, "error")
                                assert any(
                                    word in status["media"]["error"].lower()
                                    for word in ("missing", "gap", "window")
                                ), status
                                assert not Path(status["files"][0]["path"]).exists(), (
                                    status
                                )
                                engine.rpc.call("aria2.removeDownloadResult", [gid])
                                results[name] = "out-of-window recovery rejected"
                                continue
                            until = time.monotonic() + 15
                            while time.monotonic() < until:
                                status = engine.rpc.call("aria2.tellStatus", [gid])
                                if status["status"] == "error":
                                    raise AssertionError(status)
                                if int(status["media"]["completedDuration"]) > int(
                                    paused["media"]["completedDuration"]
                                ):
                                    break
                                time.sleep(0.1)
                            else:
                                raise TimeoutError(status)
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
                    (dash_live / "index.mpd").as_posix(),
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
            assert (
                db.execute(
                    "SELECT MIN(number) FROM media_segments WHERE gid=?", (gid,)
                ).fetchone()[0]
                == 3456
            )
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
        actual = decoded_hash(ffmpeg, status["files"][0]["path"], "v")
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
                    "media_selections",
                    "media_publication",
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
