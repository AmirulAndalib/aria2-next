"""Native FFmpeg inspection shared by local and public integration tests."""

from __future__ import annotations

import json
import os
import subprocess


def native_env() -> dict[str, str]:
    return {
        key: os.environ[key]
        for key in ("PATH", "SystemRoot", "WINDIR", "TEMP", "TMP", "TMPDIR")
        if key in os.environ
    }


def command(*args: str, timeout: float = 600) -> str:
    result = subprocess.run(
        args,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=native_env(),
        timeout=timeout,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )
    if result.returncode:
        raise RuntimeError(result.stderr or f"Command failed: {args[0]}")
    return result.stdout


def probe(ffprobe: str, source: str, *options: str) -> dict:
    return json.loads(command(ffprobe, "-v", "error", *options, "-of", "json", source))


def decoded_hash(
    ffmpeg: str, source: str, kind: str = "v", duration: float | None = None
) -> str:
    return command(
        ffmpeg,
        "-v",
        "error",
        "-xerror",
        "-rw_timeout",
        "20000000",
        "-i",
        source,
        "-map",
        f"0:{kind}:0",
        *(["-fps_mode", "passthrough"] if kind == "v" else []),
        *(["-af", "asetpts=N/SR/TB"] if kind == "a" and duration is not None else []),
        *(["-t", str(duration)] if duration is not None else []),
        "-f",
        "hash",
        "-hash",
        "sha256",
        "-",
    ).strip()
