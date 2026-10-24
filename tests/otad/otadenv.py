#!/usr/bin/env python3
"""
Tạo môi trường giả lập board cho otad trên host x86.

Generate vào OUTDIR:
  env.img        U-Boot env redundant, cùng format với mkenvimage -r
  fw_env.config  trỏ libubootenv vào env.img
  sw-versions    version firmware đang chạy
  cmdline        kernel command line với root= của slot đang chạy

Ví dụ:
  otadenv.py build/otad-test                               # slot A, ustate=0
  otadenv.py build/otad-test active_slot=B ustate=1        # đã cài vào B, chờ reboot
  otadenv.py build/otad-test --running B --sw-version 0.3.0
"""

import argparse
import os
import struct
import sys
import zlib

ENV_SIZE = 0x20000
SLOT_DEV = {"A": "/dev/mmcblk0p1", "B": "/dev/mmcblk0p2"}
DEFAULT_VARS = {"ustate": "0", "boot_count": "0", "boot_limit": "3"}


def env_copy(variables, flags=1):
    data = b"".join(f"{k}={v}".encode() + b"\0" for k, v in variables.items()) + b"\0"
    if len(data) > ENV_SIZE - 5:
        raise ValueError("environment too large")
    data += b"\xff" * (ENV_SIZE - 5 - len(data))
    return struct.pack("<IB", zlib.crc32(data), flags) + data


def write_env(outdir, variables):
    with open(os.path.join(outdir, "env.img"), "wb") as f:
        f.write(env_copy(variables) * 2)


def corrupt_env(outdir):
    with open(os.path.join(outdir, "env.img"), "wb") as f:
        f.write(b"\0" * ENV_SIZE * 2)


def setup(outdir, running="A", sw_version="0.2.0", overrides=None):
    outdir = os.path.abspath(outdir)
    os.makedirs(outdir, exist_ok=True)

    variables = {"active_slot": running, **DEFAULT_VARS, **(overrides or {})}
    write_env(outdir, variables)

    img = os.path.join(outdir, "env.img")
    with open(os.path.join(outdir, "fw_env.config"), "w") as f:
        f.write("# Device  Offset  Size\n")
        f.write(f"{img} 0x0 {ENV_SIZE:#x}\n")
        f.write(f"{img} {ENV_SIZE:#x} {ENV_SIZE:#x}\n")

    with open(os.path.join(outdir, "sw-versions"), "w") as f:
        f.write(sw_version + "\n")

    with open(os.path.join(outdir, "cmdline"), "w") as f:
        f.write(f"console=ttyO0,115200n8 root={SLOT_DEV[running]} ro rootwait\n")

    return variables


def otad_args(outdir):
    outdir = os.path.abspath(outdir)
    return ["-s", os.path.join(outdir, "ota.sock"),
            "-w", os.path.join(outdir, "sw-versions"),
            "-e", os.path.join(outdir, "fw_env.config"),
            "-k", os.path.join(outdir, "cmdline")]


def main():
    p = argparse.ArgumentParser(description="Create a fake board environment for otad.")
    p.add_argument("outdir")
    p.add_argument("--running", choices=["A", "B"], default="A", help="slot in root= (default: A)")
    p.add_argument("--sw-version", default="0.2.0", help="content of sw-versions (default: 0.2.0)")
    p.add_argument("vars", nargs="*", metavar="VAR=VALUE", help="override U-Boot env variables")
    a = p.parse_args()

    overrides = {}
    for kv in a.vars:
        if "=" not in kv:
            p.error(f"expected VAR=VALUE, got '{kv}'")
        k, v = kv.split("=", 1)
        overrides[k] = v

    variables = setup(a.outdir, a.running, a.sw_version, overrides)
    print(f"environment in {os.path.abspath(a.outdir)}")
    print(f"  running slot : {a.running}")
    print(f"  sw-versions  : {a.sw_version}")
    print("  u-boot env   : " + " ".join(f"{k}={v}" for k, v in variables.items()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
