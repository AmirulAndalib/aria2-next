from __future__ import annotations

import argparse
import json
import time
import traceback
from pathlib import Path
from typing import Callable

from .runtime import RunDirectory


Validation = Callable[[RunDirectory, Path | None], dict[str, object]]


def run_validation(protocol: str, validation: Validation) -> int:
    parser = argparse.ArgumentParser(description=f"Validate {protocol} transfers.")
    parser.add_argument("--engine", type=Path)
    parser.add_argument("--keep-artifacts", action="store_true")
    args = parser.parse_args()

    run = RunDirectory(protocol, args.keep_artifacts)
    started = time.monotonic()
    result: dict[str, object]
    success = False
    try:
        details = validation(run, args.engine)
        success = True
        result = {
            "success": True,
            "durationSeconds": round(time.monotonic() - started, 3),
            "details": details,
        }
        print(f"PASS {protocol}: {json.dumps(details, sort_keys=True)}")
    except Exception as error:
        result = {
            "success": False,
            "durationSeconds": round(time.monotonic() - started, 3),
            "error": str(error),
            "traceback": traceback.format_exc(),
        }
        print(f"FAIL {protocol}: {error}")
    run.write_result(result)
    run.cleanup_payloads()
    return 0 if success else 1
