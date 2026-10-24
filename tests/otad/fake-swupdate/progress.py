#!/usr/bin/env python3
"""
SWUpdate giả có progress socket, thay cho 09-swupdate-args.

Usage: progress.py <url>
  Socket lấy từ biến FAKE_SWU_PROGRESS (smoke_test.py đặt cùng giá trị với otad -p).
  Kịch bản chọn bằng query của url, vd http://host/x.swu?mode=late
"""

import os
import socket
import struct
import sys
import time
from urllib.parse import parse_qs, urlparse

IDLE, START, RUN, SUCCESS, FAILURE, DOWNLOAD, DONE, SUBPROCESS, PROGRESS = range(9)
API_V2 = 2 << 16
MSG = struct.Struct("<IIIxxxxQIII256s64sII2048sxxxx")
ACK = struct.Struct("<I4s")
assert MSG.size == 2416 and ACK.size == 8


def msg(status, dwl=0, nsteps=0, step=0, pct=0, info=b""):
    return MSG.pack(API_V2, status, dwl, 47127040, nsteps, step, pct,
                    b"rootfs.ext4.gz", b"raw", 3, len(info), info)


def listen(path, delay=0.0):
    time.sleep(delay)
    if os.path.exists(path):
        os.unlink(path)
    srv = socket.socket(socket.AF_UNIX)
    srv.bind(path)
    srv.listen(1)
    srv.settimeout(3)
    try:
        conn, _ = srv.accept()
    except socket.timeout:
        print("[WARN ] : no progress client")
        conn = None
    srv.close()
    os.unlink(path)
    return conn


def send(conn, data):
    if conn is None:
        return None
    try:
        conn.sendall(data)
        time.sleep(0.005)
    except OSError as e:
        print(f"[TRACE] : progress client dropped: {e}", flush=True)
        conn.close()
        return None
    return conn


def stale_socket(path):
    if os.path.exists(path):
        os.unlink(path)
    s = socket.socket(socket.AF_UNIX)
    s.bind(path)
    s.close()


def main():
    url = sys.argv[1]
    mode = parse_qs(urlparse(url).query).get("mode", ["ok"])[0]
    path = os.environ["FAKE_SWU_PROGRESS"]
    print(f"[INFO ] : fake swupdate mode={mode} url={url}", flush=True)

    if mode == "nosocket":
        time.sleep(0.5)
        return 0

    if mode == "late":
        stale_socket(path)
        conn = listen(path, delay=0.5)
    else:
        conn = listen(path)

    conn = send(conn, ACK.pack(1 << 16 if mode == "badapi" else API_V2, b"ACK\0"))
    time.sleep(0.05)
    conn = send(conn, msg(START, nsteps=2))
    conn = send(conn, b"".join(msg(DOWNLOAD, dwl=p) for p in range(0, 101, 20)))

    if mode == "fail":
        conn = send(conn, msg(RUN, nsteps=2, step=1))
        conn = send(conn, msg(PROGRESS, nsteps=2, step=1, pct=50))
        conn = send(conn, msg(FAILURE, info=b"Installation failed"))
        print("[ERROR] : SWUPDATE failed [0] Image invalid or corrupted", flush=True)
        return 1

    for step in (1, 2):
        conn = send(conn, msg(RUN, nsteps=2, step=step))
        for pct in (50, 100):
            conn = send(conn, msg(PROGRESS, nsteps=2, step=step, pct=pct))
    conn = send(conn, msg(SUCCESS) + msg(DONE))
    print("[INFO ] : SWUPDATE successful !", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
