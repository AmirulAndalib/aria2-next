"""Native FFmpeg inspection shared by local and public integration tests."""

from __future__ import annotations

import json
import os
import subprocess
import sqlite3
import time
from pathlib import Path


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


def wait_duration(engine, gid: str, milliseconds: int) -> dict:
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        status = engine.rpc.call("aria2.tellStatus", [gid])
        if status["status"] == "error":
            raise AssertionError(status)
        if int(status.get("media", {}).get("completedDuration", 0)) >= milliseconds:
            return status
        time.sleep(0.1)
    raise TimeoutError(status)


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


def control_action(engine, gid: str, action: str) -> dict:
    """Exercise the public task lifecycle and verify its durable state."""
    before = engine.rpc.call("aria2.tellStatus", [gid])
    result = {"action": action, "before": before["media"]}
    if action == "finish":
        engine.rpc.call("aria2.finishMedia", [gid])
        return result
    if action not in ("remove-active", "remove-force"):
        requested = time.monotonic()
        engine.rpc.call(
            "aria2.forcePause" if action == "force-pause" else "aria2.pause", [gid]
        )
        paused = engine.rpc.wait_status(gid, "paused", 30)
        result["pauseSeconds"] = round(time.monotonic() - requested, 3)
        time.sleep(1)
        stable = engine.rpc.call("aria2.tellStatus", [gid])
        for key in ("completedDuration", "downloadedLength"):
            if paused["media"][key] != stable["media"][key]:
                raise AssertionError(
                    f"Paused media changed {key}: {paused} -> {stable}"
                )
        result["paused"] = paused["media"]
        if action in ("restart", "crash"):
            session = engine.root / "session.txt"
            engine.rpc.call(
                "aria2.changeGlobalOption", [{"save-session": str(session)}]
            )
            engine.rpc.call("aria2.saveSession")
            if action == "crash":
                engine.rpc.call("aria2.unpause", [gid])
                deadline = time.monotonic() + 45
                while time.monotonic() < deadline:
                    interrupted = engine.rpc.call("aria2.tellStatus", [gid])
                    if interrupted["status"] in ("complete", "error"):
                        raise AssertionError(
                            f"Task terminated before crash injection: {interrupted}"
                        )
                    if int(interrupted["media"]["downloadedLength"]) > int(
                        paused["media"]["downloadedLength"]
                    ):
                        break
                    time.sleep(0.1)
                else:
                    raise TimeoutError("No new media committed before crash injection")
                result["interrupted"] = interrupted["media"]
                engine.process.kill()
                engine.process.wait(timeout=10)
            engine.stop()
            engine.start([f"--input-file={session}", f"--save-session={session}"])
            restored = engine.rpc.wait_status(gid, "paused", 30)
            checkpoint = result.get("interrupted", paused["media"])
            if int(restored["media"]["completedDuration"]) < int(
                checkpoint["completedDuration"]
            ):
                raise AssertionError("Restart lost committed media duration")
            result["restored"] = restored["media"]
        if action == "finish-paused":
            engine.rpc.call("aria2.finishMedia", [gid])
            return result
    if action.startswith("remove-"):
        engine.rpc.call(
            "aria2.forceRemove" if action == "remove-force" else "aria2.remove", [gid]
        )
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            tasks = engine.rpc.call("aria2.tellActive") + engine.rpc.call(
                "aria2.tellWaiting", [0, 100]
            )
            if not any(task["gid"] == gid for task in tasks):
                break
            time.sleep(0.1)
        else:
            raise TimeoutError("Removed media task remained active")
        if Path(before["files"][0]["path"]).exists():
            raise AssertionError("Removed media task published an output")
        if (engine.root / "media/tasks" / gid).exists():
            raise AssertionError("Removed media task retained its cache")
        with sqlite3.connect(engine.root / "media/state.db") as db:
            tables = db.execute(
                "SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 'media_%'"
            ).fetchall()
            for (table,) in tables:
                if db.execute(
                    f'SELECT COUNT(*) FROM "{table}" WHERE gid=?', (gid,)
                ).fetchone()[0]:
                    raise AssertionError(f"Removed media task retained rows in {table}")
        result["removed"] = True
    else:
        engine.rpc.call("aria2.unpause", [gid])
    return result
