#!/usr/bin/env python3
"""Offline validation of the Blender push-to-game addon
(tools/blender/skyrimbridge_push.py).

The addon's protocol layer is importable without bpy by design; this harness
imports it standalone and runs it against a SIMULATED plugin dispatcher on a
test-named shared-memory mapping (real Windows named shared memory, same OS
mechanism as the live channel, but never the live channel name, so a running
game is untouched).

Scope, stated honestly: this proves the addon's mailbox client (open, magic
check, sequence-gated round-trip, error and timeout paths) and the name
sanitizer. The Blender-side export UI and the in-game spawn are game-bound:
docs/VALIDATION-PROTOCOL.md section 7.
"""

import importlib.util
import os
import struct
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ADDON = os.path.join(ROOT, "tools", "blender", "skyrimbridge_push.py")

spec = importlib.util.spec_from_file_location("skyrimbridge_push", ADDON)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

_passed = _failed = 0


def check(label, ok, detail=""):
    global _passed, _failed
    if ok:
        _passed += 1
        print(f"  PASS  {label}")
    else:
        _failed += 1
        print(f"  FAIL  {label}  {detail}")


print("[import and helpers]")
check("addon imports without bpy (protocol layer standalone)", mod.bpy is None)
check("sanitize_name strips unsafe characters",
      mod.sanitize_name("My Tree (v2)!") == "My_Tree_v2")
check("sanitize_name empty fallback", mod.sanitize_name("///") == "pushed")

print("[mailbox client vs simulated dispatcher]")
TEST_NAME = "SkyrimBridge_Command_AddonTest"
DEAD_NAME = "SkyrimBridge_Command_AddonTestDead"

# No dispatcher: the mapping exists but is zeroed -> magic check must refuse.
try:
    mod.SBChannel(DEAD_NAME).open()
    check("open() refuses a dead channel", False, "no exception")
except mod.ChannelError as e:
    check("open() refuses a dead channel", "bad magic" in str(e), str(e))


class Dispatcher(threading.Thread):
    """Simulates BridgeCommand::Poll on a test-named mapping."""

    def __init__(self, name):
        super().__init__(daemon=True)
        import mmap
        self.m = mmap.mmap(-1, mod.SIZE, tagname=name)
        struct.pack_into("<II", self.m, 0, mod.MAGIC, 1)   # magic + version
        self.stop = threading.Event()
        self.handled = 0

    def run(self):
        while not self.stop.is_set():
            req = struct.unpack_from("<I", self.m, mod.OFF_REQ)[0]
            resp = struct.unpack_from("<I", self.m, mod.OFF_RESP)[0]
            if req == resp:
                time.sleep(0.001)
                continue
            verb = self.m[mod.OFF_VERB:mod.OFF_VERB + 32].split(b"\0", 1)[0].decode()
            arg0 = self.m[mod.OFF_ARG0:mod.OFF_ARG0 + 512].split(b"\0", 1)[0].decode()
            status, result, text = -1, 0, ""
            if verb == "ping":
                status, result, text = 0, 1, "pong"
            elif verb == "model.spawn":
                if arg0.endswith(".glb"):
                    status, result, text = 0, 0xFF000ABC, "placed 0xFF000ABC"
                else:
                    status, result, text = -4, 0, "could not materialize " + arg0
            struct.pack_into("<i", self.m, mod.OFF_STATUS, status)
            # same bytes the C++ writes: the int32 bit pattern of the u32
            struct.pack_into("<I", self.m, mod.OFF_RESINT, result & 0xFFFFFFFF)
            struct.pack_into("4096s", self.m, mod.OFF_TEXT, text.encode())
            self.handled += 1
            struct.pack_into("<I", self.m, mod.OFF_RESP, req)   # publish last


disp = Dispatcher(TEST_NAME)
disp.start()

ch = mod.SBChannel(TEST_NAME).open()
check("open() accepts a live channel", ch.connected())

status, result, text = ch.request("ping", timeout=5.0)
check("ping round-trip", (status, result, text) == (0, 1, "pong"),
      f"{status} {result} {text!r}")

status, result, text = ch.request("model.spawn", r"C:\tmp\tree.glb", timeout=5.0)
check("model.spawn: 0xFF-prefixed FormID crosses as the int32 bit pattern",
      status == 0 and (result & 0xFFFFFFFF) == 0xFF000ABC and result < 0
      and text == "placed 0xFF000ABC",
      f"{status} {result} {text!r}")

status, result, text = ch.request("bogus.verb", timeout=5.0)
check("unknown verb -> status -1", status == -1)

ok = True
for i in range(50):
    s, r, t = ch.request("ping", timeout=5.0)
    if (s, r, t) != (0, 1, "pong"):
        ok = False
        break
check("50 sequential requests: no stale or torn responses", ok)
ch.close()

print("[push_glb wrapper]")
fid, msg = mod.push_glb(r"C:\tmp\tree.glb", timeout=5.0, channel_name=TEST_NAME)
check("push_glb success returns (FormID, message)",
      fid == 0xFF000ABC and msg == "placed 0xFF000ABC")

try:
    mod.push_glb(r"C:\tmp\broken.obj", timeout=5.0, channel_name=TEST_NAME)
    check("push_glb surfaces the plugin's failure text", False, "no exception")
except mod.ChannelError as e:
    check("push_glb surfaces the plugin's failure text",
          "could not materialize" in str(e), str(e))

disp.stop.set()
disp.join(timeout=2.0)

ch = mod.SBChannel(TEST_NAME).open()      # magic still present, dispatcher gone
try:
    ch.request("ping", timeout=0.3)
    check("timeout raises with the unpause hint", False, "no exception")
except mod.ChannelError as e:
    check("timeout raises with the unpause hint", "unpause" in str(e), str(e))
ch.close()

print(f"\n{_passed} passed, {_failed} failed")
sys.exit(1 if _failed else 0)
