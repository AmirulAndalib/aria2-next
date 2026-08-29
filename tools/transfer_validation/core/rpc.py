from __future__ import annotations

import json
import time
import urllib.error
import urllib.request
from typing import Any


class RpcError(RuntimeError):
    pass


class RpcClient:
    def __init__(self, port: int, secret: str):
        self.url = f"http://127.0.0.1:{port}/jsonrpc"
        self.secret = secret
        self.request_id = 0

    def call(self, method: str, params: list[Any] | None = None) -> Any:
        self.request_id += 1
        arguments = [f"token:{self.secret}"]
        if params:
            arguments.extend(params)
        payload = json.dumps(
            {
                "jsonrpc": "2.0",
                "id": str(self.request_id),
                "method": method,
                "params": arguments,
            }
        ).encode()
        request = urllib.request.Request(
            self.url, data=payload, headers={"Content-Type": "application/json"}
        )
        try:
            with urllib.request.urlopen(request, timeout=5) as response:
                document = json.load(response)
        except (OSError, urllib.error.URLError) as error:
            raise RpcError(f"RPC request failed for {method}: {error}") from error
        if "error" in document:
            raise RpcError(f"RPC {method} failed: {document['error']}")
        return document.get("result")

    def wait_ready(self, timeout: float = 15.0) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                self.call("aria2.getVersion")
                return
            except RpcError:
                time.sleep(0.1)
        raise TimeoutError(f"RPC did not become ready within {timeout:g}s")

    def wait_complete(self, gid: str, timeout: float = 60.0) -> dict[str, Any]:
        deadline = time.monotonic() + timeout
        last: dict[str, Any] = {}
        while time.monotonic() < deadline:
            last = self.call("aria2.tellStatus", [gid])
            status = last.get("status")
            if status == "error":
                raise RpcError(
                    f"Task {gid} failed: {last.get('errorCode')} {last.get('errorMessage')}"
                )
            total = int(last.get("totalLength", "0"))
            completed = int(last.get("completedLength", "0"))
            if total > 0 and completed == total:
                return last
            time.sleep(0.1)
        raise TimeoutError(f"Task {gid} did not complete: {last}")
