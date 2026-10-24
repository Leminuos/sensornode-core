#!/usr/bin/env python3
"""
Smoke test cho sensornode-otad trên host: mỗi test chạy một otad riêng với
môi trường giả lập (otadenv.py) và nói chuyện qua control socket.

Usage:
  smoke_test.py [--otad PATH] [--workdir DIR] [unittest args...]
  smoke_test.py -k install                     # chỉ chạy test có tên chứa "install"
"""

import argparse
import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from contextlib import closing

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import otadenv  # noqa: E402

FAKE = os.path.join(HERE, "fake-swupdate")
FAKE_REBOOT = os.path.join(HERE, "fake-reboot")
OTAD = os.path.join(REPO, "build", "otad", "sensornode-otad")
WORKDIR = os.path.join(REPO, "build", "otad-smoke")
SUN_PATH_MAX = 107
MOSQUITTO = shutil.which("mosquitto") or "/usr/sbin/mosquitto"
HAVE_MQTT = os.access(MOSQUITTO, os.X_OK) and shutil.which("mosquitto_pub") is not None


class Client:
    def __init__(self, path, timeout=3.0):
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.settimeout(timeout)
        self.sock.connect(path)
        self.buf = b""
        self.events = []
        self.hello = self.next()

    def close(self):
        self.sock.close()

    def next(self, timeout=3.0):
        deadline = time.monotonic() + timeout
        while b"\n" not in self.buf:
            left = deadline - time.monotonic()
            if left <= 0:
                raise TimeoutError("no message from otad")
            self.sock.settimeout(left)
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("otad closed the connection")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return json.loads(line)

    def send(self, obj):
        self.sock.sendall((json.dumps(obj) + "\n").encode())

    def cmd(self, obj, timeout=3.0):
        """Gửi lệnh, trả về reply; event nhận trong lúc chờ được giữ lại trong self.events."""
        self.send(obj)
        while True:
            m = self.next(timeout)
            if m["type"] == "reply":
                return m
            self.events.append(m)

    def wait_type(self, type_, timeout=10.0):
        """Chờ message có type == type_, trả về message đó."""
        deadline = time.monotonic() + timeout
        while True:
            m = self.events.pop(0) if self.events else self.next(deadline - time.monotonic())
            if m["type"] == type_:
                return m

    def wait_state(self, state, timeout=10.0):
        """Chờ state event có ota.state == state, trả về mọi event đã nhận tới lúc đó."""
        deadline = time.monotonic() + timeout
        seen = []
        while True:
            m = self.events.pop(0) if self.events else self.next(deadline - time.monotonic())
            seen.append(m)
            if m["type"] == "state" and m["ota"]["state"] == state:
                return seen


class Broker:
    """mosquitto riêng cho từng test, chỉ listen 127.0.0.1 trên port ngẫu nhiên."""

    def __init__(self, workdir, port=None):
        self.dir = workdir
        os.makedirs(workdir, exist_ok=True)
        self.port = port or free_port()
        self.proc = None

    def start(self):
        conf = os.path.join(self.dir, "mosquitto.conf")
        with open(conf, "w") as f:
            f.write(f"listener {self.port} 127.0.0.1\nallow_anonymous true\npersistence false\n")
        self.log_file = open(os.path.join(self.dir, "mosquitto.log"), "a")
        self.proc = subprocess.Popen([MOSQUITTO, "-c", conf], stdout=self.log_file, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 3
        while True:
            with closing(socket.socket()) as s:
                if s.connect_ex(("127.0.0.1", self.port)) == 0:
                    return self
            if self.proc.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError("mosquitto did not start")
            time.sleep(0.05)

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            self.proc.wait(3)
        if self.proc:
            self.log_file.close()
        self.proc = None

    def publish(self, payload, topic="ota/latest", retain=True):
        if isinstance(payload, dict):
            payload = json.dumps(payload)
        args = ["mosquitto_pub", "-h", "127.0.0.1", "-p", str(self.port), "-t", topic, "-q", "1"]
        if retain:
            args.append("-r")
        args += ["-n"] if payload == "" else ["-m", payload]
        subprocess.run(args, check=True, timeout=3)


def free_port():
    with closing(socket.socket()) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Otad:
    def __init__(self, workdir, args=(), running="A", **env):
        self.dir = workdir
        shutil.rmtree(workdir, ignore_errors=True)
        otadenv.setup(workdir, running=running, overrides=env)
        self.tmp = None
        sockdir = workdir
        # Repo nằm sâu thì đường dẫn vượt sun_path, chuyển riêng socket sang /tmp.
        if len(os.path.join(workdir, "swupdateprog")) > SUN_PATH_MAX:
            self.tmp = tempfile.mkdtemp(prefix="otad-")
            sockdir = self.tmp
        self.sock = os.path.join(sockdir, "ota.sock")
        # Mỗi test một progress socket riêng, không đụng /tmp/swupdateprog thật.
        self.progress = os.path.join(sockdir, "swupdateprog")
        self.log_path = os.path.join(workdir, "otad.log")
        self.args = list(args)
        self.proc = None

    def start(self):
        self.log_file = open(self.log_path, "w")
        args = otadenv.otad_args(self.dir)
        args[args.index("-s") + 1] = self.sock
        args += ["-p", self.progress]
        self.reboot_mark = os.path.join(self.dir, "rebooted")
        env = dict(os.environ, FAKE_SWU_PROGRESS=self.progress, FAKE_REBOOT_MARK=self.reboot_mark)
        self.proc = subprocess.Popen([OTAD, *args, *self.args], env=env,
                                     stdout=self.log_file, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 3
        while not os.path.exists(self.sock):
            if self.proc.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError("otad did not start:\n" + self.log())
            time.sleep(0.02)
        return self

    def stop(self, timeout=3.0):
        if self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
        return self.proc.wait(timeout)

    def kill(self):
        if self.proc and self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()
        if self.proc:
            self.log_file.close()
        if self.tmp:
            shutil.rmtree(self.tmp, ignore_errors=True)

    def client(self):
        return Client(self.sock)

    def log(self):
        with open(self.log_path) as f:
            return f.read()


def pgroup_alive(pgid):
    for pid in filter(str.isdigit, os.listdir("/proc")):
        try:
            with open(f"/proc/{pid}/stat") as f:
                # Field 5 là pgrp, comm trong ngoặc có thể chứa khoảng trắng.
                fields = f.read().rsplit(")", 1)[1].split()
        except OSError:
            continue
        if int(fields[2]) == pgid:
            return True
    return False


def spawned_pid(log):
    for line in log.splitlines():
        if "spawned pid=" in line:
            return int(line.split("spawned pid=")[1].split()[0])
    raise AssertionError("no 'spawned pid=' in log")


class OtadTestCase(unittest.TestCase):
    def otad(self, *args, **env):
        o = Otad(os.path.join(WORKDIR, self._testMethodName), args, **env)
        self.addCleanup(o.kill)
        return o.start()

    def connect(self, otad):
        c = otad.client()
        self.addCleanup(c.close)
        return c


class StatusTest(OtadTestCase):
    def test_hello(self):
        c = self.connect(self.otad())
        self.assertEqual(c.hello["type"], "hello")
        self.assertEqual(c.hello["daemon"], "otad")
        self.assertEqual(c.hello["ota"]["state"], "idle")

    def test_status_reads_system(self):
        c = self.connect(self.otad())
        r = c.cmd({"cmd": "status", "id": 7})
        self.assertTrue(r["ok"])
        self.assertEqual(r["id"], 7)
        self.assertEqual(r["system"], {
            "sw_version": "0.2.0", "running_slot": "A", "active_slot": "A",
            "ustate": 0, "boot_count": 0, "boot_limit": 3, "errors": [],
        })

    def test_status_rereads_env(self):
        o = self.otad()
        c = self.connect(o)
        otadenv.write_env(o.dir, {"active_slot": "B", "ustate": "1", "boot_count": "2", "boot_limit": "3"})
        s = c.cmd({"cmd": "status"})["system"]
        self.assertEqual((s["running_slot"], s["active_slot"], s["ustate"], s["boot_count"]), ("A", "B", 1, 2))

    def test_status_invalid_value(self):
        c = self.connect(self.otad(boot_count="abc"))
        s = c.cmd({"cmd": "status"})["system"]
        self.assertIsNone(s["boot_count"])
        self.assertIn("invalid boot_count", s["errors"][0]["error"])

    def test_status_corrupt_env(self):
        o = self.otad()
        otadenv.corrupt_env(o.dir)
        c = self.connect(o)
        r = c.cmd({"cmd": "status"})
        self.assertTrue(r["ok"])
        self.assertIsNone(r["system"]["active_slot"])
        self.assertEqual(r["system"]["errors"][0]["source"], "bootenv")
        self.assertEqual(r["system"]["sw_version"], "0.2.0")

    def test_protocol_errors(self):
        c = self.connect(self.otad())
        c.send({"x": 1})
        self.assertEqual(c.next()["error"], "missing cmd")
        c.sock.sendall(b"not json\n")
        self.assertEqual(c.next()["error"], "invalid json")
        self.assertEqual(c.cmd({"cmd": "foo"})["error"], "unknown command")
        self.assertTrue(c.cmd({"cmd": "ping"})["ok"])

    def test_sigterm_removes_socket(self):
        o = self.otad()
        self.assertEqual(o.stop(), 0)
        self.assertFalse(os.path.exists(o.sock))


class StateMachineTest(OtadTestCase):
    URL = "http://host:8000/release/x-0.3.0.swu"

    def test_fake_install_done(self):
        c = self.connect(self.otad("-f"))
        self.assertTrue(c.cmd({"cmd": "install", "url": self.URL})["ok"])
        events = c.wait_state("done")
        states = [m["ota"]["state"] for m in events if m["type"] == "state"]
        self.assertEqual(states, ["downloading", "installing", "done"])
        for phase in ("downloading", "installing"):
            pct = [m["ota"]["percent"] for m in events if m["ota"]["state"] == phase]
            self.assertEqual(pct, sorted(pct), f"{phase} percent not monotonic")
        self.assertEqual(c.cmd({"cmd": "install", "url": self.URL})["error"], "reboot pending")

    def test_fake_install_fail_then_retry(self):
        c = self.connect(self.otad("-f"))
        c.cmd({"cmd": "install", "url": "http://host/fail.swu"})
        last = c.wait_state("failed")[-1]
        self.assertEqual(last["ota"]["error"], "simulated failure")
        self.assertTrue(c.cmd({"cmd": "install", "url": self.URL})["ok"])

    def test_busy(self):
        c = self.connect(self.otad("-f"))
        c.cmd({"cmd": "install", "url": self.URL})
        self.assertEqual(c.cmd({"cmd": "install", "url": self.URL})["error"], "busy")

    def test_invalid_url(self):
        c = self.connect(self.otad("-f"))
        cases = [
            ({"cmd": "install"}, "no update available"),
            ({"cmd": "install", "url": 5}, "invalid url"),
            ({"cmd": "install", "url": ""}, "invalid url"),
            ({"cmd": "install", "url": "ftp://host/x.swu"}, "invalid url"),
            ({"cmd": "install", "url": "http://host/x.swu -e stable,copy1"}, "invalid url"),
            ({"cmd": "install", "url": "http://host/" + "a" * 1100}, "invalid url"),
        ]
        for req, err in cases:
            with self.subTest(req=req):
                self.assertEqual(c.cmd(req)["error"], err)

    def test_broadcast_and_late_hello(self):
        o = self.otad("-f")
        listener = self.connect(o)
        sender = self.connect(o)
        sender.cmd({"cmd": "install", "url": self.URL})
        self.assertEqual(listener.next()["ota"]["state"], "downloading")
        time.sleep(0.5)
        late = self.connect(o)
        self.assertEqual(late.hello["ota"]["state"], "downloading")
        self.assertGreater(late.hello["ota"]["percent"], 0)

    def test_reboot_pending_at_start(self):
        c = self.connect(self.otad("-f", active_slot="B", ustate="1"))
        self.assertEqual(c.hello["ota"]["state"], "done")
        self.assertEqual(c.cmd({"cmd": "install", "url": self.URL})["error"], "reboot pending")

    def test_idle_after_reboot_into_new_slot(self):
        c = self.connect(self.otad("-f", running="B", active_slot="B", ustate="1"))
        self.assertEqual(c.hello["ota"]["state"], "idle")

    def test_install_rejected_during_trial_boot(self):
        o = self.otad("-f", running="B", active_slot="B", ustate="1")
        c = self.connect(o)
        self.assertEqual(c.cmd({"cmd": "install", "url": self.URL})["error"], "trial boot")
        self.assertEqual(c.cmd({"cmd": "status"})["ota"]["state"], "idle")
        # ota-confirm-boot commit xong thì cài được ngay, không cần restart otad.
        otadenv.write_env(o.dir, {"active_slot": "B", "ustate": "0", "boot_count": "0", "boot_limit": "3"})
        self.assertTrue(c.cmd({"cmd": "install", "url": self.URL})["ok"])


class SwupdateInstallerTest(OtadTestCase):
    URL = "http://host:8000/release/x-0.3.0.swu"

    def install(self, tool, until, timeout=5.0):
        o = self.otad("-t", os.path.join(FAKE, tool))
        c = self.connect(o)
        self.assertTrue(c.cmd({"cmd": "install", "url": self.URL})["ok"])
        return o, c.wait_state(until, timeout)[-1]["ota"]

    def test_ok(self):
        o, ota = self.install("ok.sh", "done")
        log = o.log()
        self.assertIn("SigBlk:", log)
        self.assertRegex(log, r"SigBlk:\W0+\b", "child inherited a blocked signal mask")
        pid = spawned_pid(log)
        self.assertIn(f"pgid={pid} pid={pid}", log)
        self.assertIn("to stderr", log)
        self.assertIn("last line without newline", log)

    def test_fail_reports_error_line(self):
        _, ota = self.install("fail.sh", "failed")
        self.assertEqual(ota["error"], "[ERROR] : SWUPDATE failed [0] HW compatibility not verified")

    def test_killed_by_signal(self):
        _, ota = self.install("selfkill.sh", "failed")
        self.assertEqual(ota["error"], "killed by SIGKILL")

    def test_missing_tool(self):
        o = self.otad("-t", "/nonexistent/swupdate")
        c = self.connect(o)
        c.cmd({"cmd": "install", "url": self.URL})
        ota = c.wait_state("failed", 2)[-1]["ota"]
        self.assertIn("No such file or directory", ota["error"])

    def test_grandchild_does_not_delay_finish(self):
        t0 = time.monotonic()
        o, _ = self.install("bg.sh", "done", timeout=5)
        self.assertLess(time.monotonic() - t0, 5)
        self.assertFalse(pgroup_alive(spawned_pid(o.log())), "grandchild left behind")

    def test_noisy_output(self):
        o, _ = self.install("noisy.sh", "done", timeout=15)
        log = o.log()
        self.assertIn("swupdate: 50000\n", log)
        self.assertEqual(sum(1 for l in log.splitlines() if l.rstrip().endswith(("xxx", "xxx..."))), 3)

    def test_shutdown_kills_child(self):
        o = self.otad("-t", os.path.join(FAKE, "hang.sh"))
        c = self.connect(o)
        c.cmd({"cmd": "install", "url": self.URL})
        time.sleep(0.3)
        pid = spawned_pid(o.log())
        self.assertTrue(pgroup_alive(pid))
        self.assertEqual(o.stop(), 0)
        self.assertFalse(pgroup_alive(pid), "child process group still alive after otad exit")


class ProgressTest(OtadTestCase):
    def install(self, mode, until, timeout=10.0):
        o = self.otad("-t", os.path.join(FAKE, "progress.py"))
        c = self.connect(o)
        self.assertTrue(c.cmd({"cmd": "install", "url": f"http://host/x.swu?mode={mode}"})["ok"])
        return o, c.wait_state(until, timeout)

    @staticmethod
    def percents(events, state):
        return [m["ota"]["percent"] for m in events if m["ota"]["state"] == state]

    def test_progress(self):
        o, events = self.install("ok", "done")
        states = [m["ota"]["state"] for m in events if m["type"] == "state"]
        self.assertEqual(states, ["downloading", "installing", "done"])
        self.assertEqual(self.percents(events, "downloading"), [0, 20, 40, 60, 80, 100])
        self.assertEqual(self.percents(events, "installing"), [0, 25, 50, 75, 100])
        self.assertIn("step 1/2 image=rootfs.ext4.gz handler=raw", o.log())
        self.assertIn("step 2/2", o.log())

    def test_late_socket_and_stale_file(self):
        o, events = self.install("late", "done")
        self.assertEqual(self.percents(events, "installing")[-1], 100)
        self.assertIn("progress connected", o.log())

    def test_bad_api_disables_progress(self):
        o, events = self.install("badapi", "done")
        self.assertEqual(self.percents(events, "downloading"), [0])
        self.assertEqual(self.percents(events, "installing"), [])
        self.assertIn("progress client dropped", o.log())

    def test_no_socket(self):
        _, events = self.install("nosocket", "done")
        self.assertEqual([m["type"] for m in events], ["state", "state"])

    def test_fail_after_progress(self):
        _, events = self.install("fail", "failed")
        self.assertEqual(self.percents(events, "installing"), [0, 25])
        self.assertEqual(events[-1]["ota"]["error"], "[ERROR] : SWUPDATE failed [0] Image invalid or corrupted")


class RebootTest(OtadTestCase):
    URL = "http://host:8000/release/x-0.3.0.swu"

    def otad_reboot(self, tool, *args, **env):
        return self.otad("-f", "-r", os.path.join(FAKE_REBOOT, tool), *args, **env)

    def installed(self, tool):
        o = self.otad_reboot(tool)
        c = self.connect(o)
        c.cmd({"cmd": "install", "url": self.URL})
        c.wait_state("done")
        return o, c

    def test_reboot_after_install(self):
        o, c = self.installed("ok.sh")
        self.assertTrue(c.cmd({"cmd": "reboot", "id": 9})["ok"])
        self.assertEqual(c.wait_state("rebooting", 3)[-1]["ota"]["percent"], 100)
        self.assertEqual(o.proc.wait(3), 0, "otad should exit cleanly on SIGTERM from reboot")
        self.assertTrue(os.path.exists(o.reboot_mark))
        self.assertIn("reboot requested by pid=%d" % o.proc.pid, o.log())

    def test_reboot_rejected_when_not_done(self):
        o = self.otad_reboot("ok.sh")
        c = self.connect(o)
        self.assertEqual(c.cmd({"cmd": "reboot"})["error"], "no update installed")
        c.cmd({"cmd": "install", "url": self.URL})
        self.assertEqual(c.cmd({"cmd": "reboot"})["error"], "busy")
        c.wait_state("done")
        self.assertFalse(os.path.exists(o.reboot_mark))

    def test_reboot_after_failed_install_rejected(self):
        c = self.connect(self.otad_reboot("ok.sh"))
        c.cmd({"cmd": "install", "url": "http://host/fail.swu"})
        c.wait_state("failed")
        self.assertEqual(c.cmd({"cmd": "reboot"})["error"], "no update installed")

    def test_reboot_pending_at_start(self):
        o = self.otad_reboot("ok.sh", active_slot="B", ustate="1")
        c = self.connect(o)
        self.assertEqual(c.hello["ota"]["state"], "done")
        self.assertTrue(c.cmd({"cmd": "reboot"})["ok"])
        self.assertEqual(o.proc.wait(3), 0)

    def test_reboot_command_fails(self):
        o, c = self.installed("fail.sh")
        self.assertTrue(c.cmd({"cmd": "reboot"})["ok"])
        c.wait_state("rebooting", 3)
        ota = c.wait_state("done", 3)[-1]["ota"]
        self.assertEqual(ota["error"], "reboot failed: Failed to reboot: Access denied")
        self.assertEqual(c.cmd({"cmd": "install", "url": self.URL})["error"], "reboot pending")
        self.assertEqual(c.cmd({"cmd": "status"})["ota"]["error"], ota["error"])
        # Thử lại được, lỗi cũ bị xoá khi bắt đầu lần reboot mới.
        self.assertTrue(c.cmd({"cmd": "reboot"})["ok"])
        self.assertIsNone(c.wait_state("rebooting", 3)[-1]["ota"]["error"])

    def test_reboot_command_missing(self):
        o = self.otad("-f", "-r", "/nonexistent/systemctl reboot")
        c = self.connect(o)
        c.cmd({"cmd": "install", "url": self.URL})
        c.wait_state("done")
        c.cmd({"cmd": "reboot"})
        ota = c.wait_state("done", 3)[-1]["ota"]
        self.assertIn("No such file or directory", ota["error"])

    def test_reboot_twice_and_shutdown_cleans_up(self):
        o, c = self.installed("hang.sh")
        self.assertTrue(c.cmd({"cmd": "reboot"})["ok"])
        self.assertEqual(c.cmd({"cmd": "reboot"})["error"], "already rebooting")
        self.assertEqual(c.cmd({"cmd": "install", "url": self.URL})["error"], "reboot pending")
        pid = int(o.log().split("reboot: spawned pid=")[1].split()[0])
        self.assertTrue(pgroup_alive(pid))
        self.assertEqual(o.stop(), 0)
        self.assertFalse(pgroup_alive(pid))

    def test_empty_reboot_cmd(self):
        o = Otad(os.path.join(WORKDIR, self._testMethodName), ["-r", "  "])
        self.addCleanup(o.kill)
        r = subprocess.run([OTAD, *otadenv.otad_args(o.dir), "-r", "  "], capture_output=True, text=True)
        self.assertEqual(r.returncode, 2)
        self.assertIn("--reboot-cmd must not be empty", r.stderr)


def manifest(version, url=None, **extra):
    return {"version": version, "url": url or f"http://host:8000/release/x-{version}.swu",
            "size": 1234, "sha256": "ab" * 32, **extra}


@unittest.skipUnless(HAVE_MQTT, "mosquitto / mosquitto_pub not installed")
class MqttTest(OtadTestCase):
    def broker(self, port=None):
        b = Broker(os.path.join(WORKDIR, self._testMethodName), port)
        self.addCleanup(b.stop)
        return b.start()

    def otad_with(self, broker, *args):
        return self.otad("-f", "-m", "127.0.0.1", "-P", str(broker.port), *args)

    def test_retained_manifest_then_install(self):
        b = self.broker()
        b.publish(manifest("0.3.0"))
        c = self.connect(self.otad_with(b))
        avail = c.wait_type("available")["ota"]["available"]
        self.assertEqual(avail, manifest("0.3.0"))

        self.assertTrue(c.cmd({"cmd": "install"})["ok"])
        done = c.wait_state("done")[-1]["ota"]
        self.assertEqual(done["url"], manifest("0.3.0")["url"])
        self.assertIsNone(done["available"])

    def test_live_updates(self):
        b = self.broker()
        o = self.otad_with(b)
        c = self.connect(o)
        self.assertIsNone(c.hello["ota"]["available"])
        self.wait_log(o, "mqtt: connected")

        b.publish(manifest("0.10.0"))
        self.assertEqual(c.wait_type("available")["ota"]["available"]["version"], "0.10.0")
        b.publish(manifest("0.2.0"))
        self.assertIsNone(c.wait_type("available")["ota"]["available"])
        b.publish(manifest("0.3.0"))
        self.assertEqual(c.wait_type("available")["ota"]["available"]["version"], "0.3.0")
        b.publish("")
        self.assertIsNone(c.wait_type("available")["ota"]["available"])
        self.assertIn("manifest not newer version=0.2.0 running=0.2.0", o.log())

    def test_invalid_manifest_keeps_previous(self):
        b = self.broker()
        o = self.otad_with(b)
        c = self.connect(o)
        b.publish(manifest("0.3.0"))
        c.wait_type("available")
        bad = ["not json", "[1]", json.dumps({"url": "http://h/x.swu"}), json.dumps(manifest("abc")),
               json.dumps(manifest("0.4.0", url="http://h/x.swu -e stable,copy1")),
               json.dumps(manifest("0.4.0", sha256="xyz")), json.dumps(manifest("0.4.0", size=-1)),
               "x" * 5000]
        for p in bad:
            b.publish(p)
        b.publish(manifest("0.5.0"))
        self.assertEqual(c.wait_type("available")["ota"]["available"]["version"], "0.5.0")
        self.assertEqual(o.log().count("manifest rejected"), len(bad))
        self.assertEqual(c.cmd({"cmd": "status"})["ota"]["available"]["version"], "0.5.0")

    def test_install_without_manifest(self):
        b = self.broker()
        c = self.connect(self.otad_with(b))
        self.assertEqual(c.cmd({"cmd": "install"})["error"], "no update available")

    def test_reconnect_resubscribes(self):
        b = self.broker()
        o = self.otad_with(b)
        c = self.connect(o)
        self.wait_log(o, "mqtt: connected")
        b.stop()
        self.wait_log(o, "mqtt: disconnected")
        b.start()
        b.publish(manifest("0.3.0"))
        self.assertEqual(c.wait_type("available", 15)["ota"]["available"]["version"], "0.3.0")
        self.assertEqual(o.log().count("mqtt: connected"), 2)

    def test_broker_down_at_start(self):
        port = free_port()
        o = self.otad("-f", "-m", "127.0.0.1", "-P", str(port))
        c = self.connect(o)
        self.wait_log(o, "mqtt: disconnected")
        self.assertTrue(c.cmd({"cmd": "ping"})["ok"])
        b = self.broker(port)
        b.publish(manifest("0.3.0"))
        self.assertEqual(c.wait_type("available", 15)["ota"]["available"]["version"], "0.3.0")

    def test_unknown_running_version(self):
        b = self.broker()
        o = Otad(os.path.join(WORKDIR, self._testMethodName),
                 ["-f", "-m", "127.0.0.1", "-P", str(b.port)])
        self.addCleanup(o.kill)
        os.remove(os.path.join(o.dir, "sw-versions"))
        c = self.connect(o.start())
        self.wait_log(o, "mqtt: connected")
        b.publish(manifest("0.3.0"))
        self.wait_log(o, "manifest ignored: running version unknown")
        self.assertIsNone(c.cmd({"cmd": "status"})["ota"]["available"])

    def wait_log(self, otad, text, timeout=10.0):
        deadline = time.monotonic() + timeout
        while text not in otad.log():
            if time.monotonic() > deadline:
                self.fail(f"'{text}' not in otad log")
            time.sleep(0.05)


def main():
    global OTAD, WORKDIR
    p = argparse.ArgumentParser(add_help=False)
    p.add_argument("--otad", default=OTAD)
    p.add_argument("--workdir", default=WORKDIR)
    a, rest = p.parse_known_args()
    OTAD, WORKDIR = os.path.abspath(a.otad), os.path.abspath(a.workdir)

    if not os.access(OTAD, os.X_OK):
        print(f"error: {OTAD} not found, build first or pass --otad", file=sys.stderr)
        return 2

    prog = unittest.main(argv=[sys.argv[0], *rest], exit=False, verbosity=2)
    return 0 if prog.result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
