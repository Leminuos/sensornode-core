#!/usr/bin/env python3
"""
Client thử cho control socket của otad: gửi lệnh rồi in mọi message nhận được.

Lệnh là JSON hoặc tên lệnh trần (status, ping).

Ví dụ:
  otactl.py status
  otactl.py -t 6 '{"cmd":"install","url":"http://h/x.swu"}'
  otactl.py --until done -t 10 '{"cmd":"install","url":"http://h/x.swu"}'
  otactl.py -t 0 --raw
"""

import argparse
import json
import os
import socket
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_SOCK = os.path.join(REPO, "build", "otad-test", "ota.sock")


def to_request(arg):
    return arg if arg.lstrip().startswith("{") else json.dumps({"cmd": arg})


def describe(m):
    o = m.get("ota") or {}
    parts = [f"{m.get('type', '?'):8}"]
    if m.get("type") == "reply":
        parts.append(f"{m.get('cmd')} ok={m.get('ok')}")
        if not m.get("ok"):
            parts.append(f"error={m.get('error')!r}")
    if o:
        parts.append(f"ota={o.get('state')}")
        if o.get("percent") is not None:
            parts.append(f"{o['percent']}%")
        if o.get("error"):
            parts.append(f"error={o['error']!r}")
        if o.get("available"):
            parts.append(f"available={o['available']['version']}")
    if "system" in m:
        parts.append("system=" + json.dumps(m["system"], separators=(",", ":")))
    return " ".join(parts)


def main():
    p = argparse.ArgumentParser(description="Talk to the otad control socket.")
    p.add_argument("-s", "--socket", default=DEFAULT_SOCK, help=f"default: {DEFAULT_SOCK}")
    p.add_argument("-t", "--timeout", type=float, default=1.0, help="seconds to listen, 0 = forever")
    p.add_argument("--until", metavar="STATE", help="exit 0 once ota.state reaches STATE, 1 on timeout")
    p.add_argument("--raw", action="store_true", help="print raw JSON lines")
    p.add_argument("commands", nargs="*")
    a = p.parse_args()

    s = socket.socket(socket.AF_UNIX)
    try:
        s.connect(a.socket)
    except OSError as e:
        print(f"connect {a.socket}: {e.strerror}", file=sys.stderr)
        return 2

    for c in a.commands:
        s.sendall((to_request(c) + "\n").encode())

    start = time.monotonic()
    buf = b""
    s.settimeout(0.1)
    try:
        while a.timeout == 0 or time.monotonic() - start < a.timeout:
            try:
                chunk = s.recv(4096)
            except socket.timeout:
                continue
            if not chunk:
                print("connection closed by otad", file=sys.stderr)
                return 1
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                m = json.loads(line)
                stamp = f"{time.monotonic() - start:6.2f}"
                print(line.decode() if a.raw else f"{stamp}  {describe(m)}", flush=True)
                if a.until and (m.get("ota") or {}).get("state") == a.until:
                    return 0
    except KeyboardInterrupt:
        pass

    return 1 if a.until else 0


if __name__ == "__main__":
    sys.exit(main())
