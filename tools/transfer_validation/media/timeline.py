"""DASH availability and resume checks using native FFmpeg and WireMock."""

from __future__ import annotations

import time
import xml.etree.ElementTree as ET

from media.common import command, control_action, probe, wait_duration


def validate_epoch_timeline(run, source, ffmpeg, ffprobe, engine, server, clock):
    directory = run.fixtures / "epoch-timeline"
    directory.mkdir()
    manifest = directory / "index.mpd"
    epoch = int(time.time()) - 6
    command(
        ffmpeg, "-v", "error", "-stream_loop", "-1", "-i", str(source),
        "-t", "24", "-map", "0", "-c", "copy", "-output_ts_offset", str(epoch),
        "-f", "dash", "-seg_duration", "2", manifest.as_posix(),
    )
    document = ET.parse(manifest)
    mpd = document.getroot()
    mpd.set("type", "dynamic")
    mpd.set("availabilityStartTime", "1970-01-01T00:00:00Z")
    mpd.set("minimumUpdatePeriod", "PT1S")
    mpd.set("timeShiftBufferDepth", "PT60S")
    mpd.attrib.pop("mediaPresentationDuration", None)
    for period in mpd.findall("{*}Period"):
        period.attrib.pop("duration", None)
    ET.SubElement(
        mpd, "{urn:mpeg:dash:schema:mpd:2011}UTCTiming",
        schemeIdUri="urn:mpeg:dash:utc:http-iso:2014",
        value=f"{clock.base_url}/epoch-clock",
    )
    document.write(manifest, encoding="utf-8", xml_declaration=True)
    # A server clock ahead of the client gives a negative UTC correction. Keep
    # it deterministic with WireMock's native clock, without changing OS time.
    clock.stub({
        "request": {"method": "GET", "urlPath": "/epoch-clock"},
        "response": {
            "status": 200, "body": "{{now offset='3 seconds'}}",
            "headers": {"Content-Type": "text/plain"},
            "transformers": ["response-template"],
        },
    })
    gid = engine.add_uri(
        f"{server.base_url}/epoch-timeline/index.mpd",
        {"out": "epoch-timeline.mp4", "media-record-time": "8"},
    )
    wait_duration(engine, gid, 4000)
    control_action(engine, gid, "pause")
    status = engine.rpc.wait_complete(gid, 30)
    output = status["files"][0]["path"]
    info = probe(ffprobe, output, "-show_format")
    duration = float(info["format"]["duration"])
    assert 7.9 <= duration <= 12, info
    command(ffmpeg, "-v", "error", "-xerror", "-i", output, "-f", "null", "-")
    return {"durationSeconds": duration, "pausedAndResumed": True}
