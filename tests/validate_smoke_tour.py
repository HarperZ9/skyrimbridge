#!/usr/bin/env python3
"""Offline validation of the modlist smoke tour driver
(tools/sb_smoke_tour.py, lane G22).

A simulated game runs on a TEST-named shared-memory mapping (real OS
mechanism, never the live channel name): it answers ping/game.status/
game.coc/cell.report/script.report, "loads" a known cell after a short
delay, ignores unknown coc targets exactly as the console does, and can be
killed mid-tour. The driver is held to its three verdicts:

- PASS: every stop lands, census and VM summaries captured per stop
- FAILURES: a bad coc target never lands; the tour continues and says so
- CRASH: the channel dies mid-tour; the tour stops at the right cell

The C++ verbs themselves (game.status heartbeat, game.coc console exec)
are compile-verified; live behavior is game-bound (protocol section 10).
"""

import importlib.util
import os
import struct
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DRIVER = os.path.join(ROOT, "tools", "sb_smoke_tour.py")

spec = importlib.util.spec_from_file_location("sb_smoke_tour", DRIVER)
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


CELL_IDS = {"riverwood": 0x000133A1, "whiterun": 0x000133B3,
            "solitude": 0xFF00CE11}          # one dynamic-range id on purpose


class FakeGame(threading.Thread):
    def __init__(self, name, die_after_cocs=None):
        super().__init__(daemon=True)
        import mmap
        self.m = mmap.mmap(-1, mod.SIZE, tagname=name)
        struct.pack_into("<II", self.m, 0, mod.MAGIC, 1)
        self.cell = 0x0001A270                      # start cell
        self.pending = None                         # (cell id, land_at)
        self.cocs = 0
        self.die_after = die_after_cocs
        self.stop = threading.Event()

    def run(self):
        while not self.stop.is_set():
            if self.pending and time.monotonic() >= self.pending[1]:
                self.cell = self.pending[0]
                self.pending = None
            req = struct.unpack_from("<I", self.m, mod.OFF_REQ)[0]
            resp = struct.unpack_from("<I", self.m, mod.OFF_RESP)[0]
            if req == resp:
                time.sleep(0.002)
                continue
            verb = self.m[mod.OFF_VERB:mod.OFF_VERB + 32].split(b"\0", 1)[0].decode()
            arg0 = self.m[mod.OFF_ARG0:mod.OFF_ARG0 + 512].split(b"\0", 1)[0].decode()
            status, result, text = 0, 0, ""
            if verb == "ping":
                result, text = 1, "pong"
            elif verb == "game.status":
                loading = self.pending is not None
                result = 0 if loading else self.cell
                text = ("no player cell (menu or loading)" if loading else
                        f"cell=0x{self.cell:08X} name=Fake interior=1 "
                        f"pos=0,0,0 hour=12.00")
            elif verb == "game.coc":
                self.cocs += 1
                if self.die_after and self.cocs > self.die_after:
                    return                           # simulate a crash: go silent
                target = CELL_IDS.get(arg0.lower())
                if target:
                    self.pending = (target, time.monotonic() + 0.4)
                result, text = 1, f"coc {arg0}"      # console accepts silently
            elif verb == "cell.report":
                text = (f"[Cell] 0x{self.cell:08X} Fake (interior)\n"
                        "refs=42 disabledFlag=1 lights=6 shadowLights=2\n")
                result = len(text)
            elif verb == "script.report":
                text = ("[PapyrusVM]\noverstressed=no freezeState=notFrozen\n"
                        "waitingFunctionMessages=3 (queue depth)\n"
                        "runningStacks=5 waitingLatentReturns=1 frozenStacks=0\n")
                result = len(text)
            else:
                status = -1
            struct.pack_into("<i", self.m, mod.OFF_STATUS, status)
            struct.pack_into("<I", self.m, mod.OFF_RESINT, result & 0xFFFFFFFF)
            struct.pack_into("4096s", self.m, mod.OFF_TEXT, text.encode())
            struct.pack_into("<I", self.m, mod.OFF_RESP, req)


quiet = lambda *a, **k: None

print("[full tour, all stops land]")
game = FakeGame("SkyrimBridge_Command_TourTest1")
game.start()
verdict, rows = mod.run_tour(["Riverwood", "Whiterun", "Solitude"],
                             settle=0.05, load_timeout=5.0,
                             channel_name="SkyrimBridge_Command_TourTest1",
                             log=quiet)
game.stop.set()
check("verdict PASS", verdict == "PASS", verdict)
check("three rows, all OK", len(rows) == 3 and all(r["result"].startswith("OK") for r in rows),
      str([r["result"] for r in rows]))
check("census summary captured per stop",
      all("shadowLights=2" in r["census"] for r in rows))
check("VM summary captured per stop",
      all("runningStacks=5" in r["vm"] for r in rows))
check("dynamic-range cell id printed unsigned",
      "0xFF00CE11" in rows[2]["result"], rows[2]["result"])
check("load time measured", all(r["load_s"] and r["load_s"] < 5 for r in rows))

print("[bad coc target]")
game = FakeGame("SkyrimBridge_Command_TourTest2")
game.start()
verdict, rows = mod.run_tour(["Riverwood", "NoSuchCell", "Whiterun"],
                             settle=0.05, load_timeout=1.0,
                             channel_name="SkyrimBridge_Command_TourTest2",
                             log=quiet)
game.stop.set()
check("verdict FAILURES", verdict == "FAILURES", verdict)
check("failed stop named, tour continued to the next cell",
      rows[1]["result"].startswith("FAILED") and rows[2]["result"].startswith("OK"),
      str([r["result"] for r in rows]))

print("[crash mid-tour]")
game = FakeGame("SkyrimBridge_Command_TourTest3", die_after_cocs=1)
game.start()
verdict, rows = mod.run_tour(["Riverwood", "Whiterun", "Solitude"],
                             settle=0.05, load_timeout=2.0,
                             channel_name="SkyrimBridge_Command_TourTest3",
                             log=quiet)
game.stop.set()
check("verdict CRASH", verdict == "CRASH", verdict)
check("tour stopped at the dying stop (2 rows, second is the crash)",
      len(rows) == 2 and "CRASH" in rows[1]["result"],
      str([r["result"] for r in rows]))

print("[report writer]")
out = os.path.join(os.environ.get("TEMP", "."), "sb_tour_test.txt")
mod.write_report(out, verdict, rows)
body = open(out, encoding="utf-8").read()
os.unlink(out)
check("report carries verdict and per-stop results",
      "CRASH" in body and "[Riverwood]" in body and "[Whiterun]" in body)

print(f"\n{_passed} passed, {_failed} failed")
sys.exit(1 if _failed else 0)
