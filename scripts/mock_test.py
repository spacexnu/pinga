#!/usr/bin/env python3
import json
import os
import subprocess
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import urlparse, parse_qs


class EchoHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length).decode("utf-8")
        parsed = urlparse(self.path)
        response = {
            "method": self.command,
            "path": parsed.path,
            "query": parse_qs(parsed.query),
            "headers": dict(self.headers),
            "body": body,
        }
        payload = json.dumps(response).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, fmt, *args):
        return


def run():
    server = HTTPServer(("127.0.0.1", 0), EchoHandler)
    port = server.server_port
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    payload_obj = {"hello": "pinga", "count": 3}
    payload_raw = json.dumps(payload_obj, separators=(",", ":"))

    config = {
        "url": f"http://127.0.0.1:{port}/users/{'{id}'}",
        "method": "POST",
        "headers": {
            "Content-Type": "application/json",
            "X-Test": "true",
        },
        "path_params": {"id": "99"},
        "query_params": {"debug": "1"},
        "payload": payload_obj,
    }

    with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as tmp:
        json.dump(config, tmp)
        tmp_path = tmp.name

    try:
        cmd = ["./build/pinga", "--exclude-response-headers", tmp_path]
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            raise SystemExit(result.stderr.strip() or "pinga failed")

        output = result.stdout.strip()
        data = json.loads(output)
        if data["method"] != "POST":
            raise SystemExit("unexpected method")
        if data["path"] != "/users/99":
            raise SystemExit("unexpected path")
        if data["query"].get("debug") != ["1"]:
            raise SystemExit("unexpected query")
        if data["headers"].get("Content-Type") != "application/json":
            raise SystemExit("missing content-type header")
        try:
            body_obj = json.loads(data["body"])
        except json.JSONDecodeError:
            raise SystemExit("body is not valid json")
        if body_obj != payload_obj:
            raise SystemExit("unexpected body")
    finally:
        os.unlink(tmp_path)
        server.shutdown()


if __name__ == "__main__":
    run()
