#!/usr/bin/env python3
"""Modlist smoke tour (lane G22). No compiler needed.

The pain: after a modlist change, "did anything break" means launching the
game and wandering. This drives the running game through a list of cells
over the SkyrimBridge command channel and collects evidence at every stop:
the cell census summary, the Papyrus VM summary, and load timing. A crash
or hang shows up as a channel timeout and is reported at the exact stop.

Usage (game running, [Native] CommandSurface = true, DISPOSABLE save loaded,
game unpaused):

  python sb_smoke_tour.py --cells Riverwood WhiterunBanneredMare Solitude
  python sb_smoke_tour.py --cells-file mytour.txt --settle 8 --out tour.txt

Cell names are console `coc` targets and vary by modlist; test yours in the
console first. The tour MUTATES game state (teleports; the census and VM
reads are read-only): use a save you will discard.

Exit codes: 0 = all stops passed, 2 = some stops failed (teleport did not
land), 3 = crash/hang suspected (channel died mid-tour).
"""

import argparse
import mmap
import struct
import sys
import time

SIZE = 5184
MAGIC = 0x53424331
CHANNEL_NAME = "SkyrimBridge_Command"

OFF_MAGIC, OFF_REQ, OFF_RESP = 0, 8, 12
OFF_ARGINT, OFF_STATUS, OFF_RESINT = 16, 20, 24
OFF_VERB, OFF_ARG0, OFF_ARG1, OFF_TEXT = 32, 64, 576, 1088


class ChannelDead(RuntimeError):
    """Timeout or lost magic: crash or hard hang suspected."""


class Channel:
    def __init__(self, name=CHANNEL_NAME):
        self.m = mmap.mmap(-1, SIZE, tagname=name)
        if self._u32(OFF_MAGIC) != MAGIC:
            raise ChannelDead("no live channel (game not running, or "
                              "[Native] CommandSurface = false)")

    def _u32(self, off):
        return struct.unpack_from("<I", self.m, off)[0]

    def _i32(self, off):
        return struct.unpack_from("<i", self.m, off)[0]

    def request(self, verb, arg0="", arg_int=0, timeout=30.0):
        if self._u32(OFF_MAGIC) != MAGIC:
            raise ChannelDead("channel magic lost (game exited?)")
        req = self._u32(OFF_REQ) + 1
        struct.pack_into("32s", self.m, OFF_VERB, verb.encode()[:31])
        struct.pack_into("512s", self.m, OFF_ARG0, arg0.encode()[:511])
        struct.pack_into("512s", self.m, OFF_ARG1, b"")
        struct.pack_into("<i", self.m, OFF_ARGINT, arg_int)
        struct.pack_into("<I", self.m, OFF_REQ, req)
        deadline = time.monotonic() + timeout
        while self._u32(OFF_RESP) != req:
            if time.monotonic() > deadline:
                raise ChannelDead(f"no response to {verb} in {timeout:.0f}s")
            time.sleep(0.01)
        text = self.m[OFF_TEXT:OFF_TEXT + 4096].split(b"\0", 1)[0].decode(errors="replace")
        return self._i32(OFF_STATUS), self._i32(OFF_RESINT), text


def first_lines(text, n):
    return " | ".join(line.strip() for line in text.splitlines()[:n] if line.strip())


def run_tour(cells, settle=6.0, load_timeout=90.0, channel_name=CHANNEL_NAME,
             log=print):
    """Returns (verdict, rows). verdict in {'PASS', 'FAILURES', 'CRASH'}."""
    ch = Channel(channel_name)
    status, _, text = ch.request("ping", timeout=15.0)
    if status != 0:
        raise ChannelDead("ping failed")
    _, start_cell, start_text = ch.request("game.status")
    log(f"start: {start_text}")

    rows = []
    verdict = "PASS"
    prev_cell = start_cell
    for name in cells:
        row = {"cell": name, "result": "?", "load_s": None,
               "census": "", "vm": ""}
        rows.append(row)
        try:
            ch.request("game.coc", name)
            t0 = time.monotonic()
            landed = 0
            while time.monotonic() - t0 < load_timeout:
                _, cid, _ = ch.request("game.status", timeout=load_timeout)
                if cid and cid != prev_cell:
                    landed = cid
                    break
                time.sleep(0.25)
            if not landed:
                row["result"] = "FAILED (never landed; bad coc target?)"
                verdict = "FAILURES" if verdict == "PASS" else verdict
                log(f"  {name}: FAILED to land within {load_timeout:.0f}s")
                continue
            row["load_s"] = time.monotonic() - t0
            prev_cell = landed
            time.sleep(settle)                      # let scripts/streaming settle
            _, _, census = ch.request("cell.report", timeout=60.0)
            _, _, vm = ch.request("script.report", timeout=60.0)
            row["census"] = first_lines(census, 2)
            row["vm"] = first_lines(vm, 4)
            row["result"] = f"OK (0x{landed & 0xFFFFFFFF:08X}, load {row['load_s']:.1f}s)"
            log(f"  {name}: {row['result']}")
        except ChannelDead as e:
            row["result"] = f"CRASH/HANG suspected: {e}"
            log(f"  {name}: {row['result']}")
            verdict = "CRASH"
            break
    return verdict, rows


def write_report(path, verdict, rows):
    with open(path, "w", encoding="utf-8") as f:
        f.write(f"SkyrimBridge smoke tour: {verdict}\n")
        f.write("=" * 60 + "\n")
        for row in rows:
            f.write(f"\n[{row['cell']}] {row['result']}\n")
            if row["census"]:
                f.write(f"  census: {row['census']}\n")
            if row["vm"]:
                f.write(f"  vm:     {row['vm']}\n")


def main():
    p = argparse.ArgumentParser(description="SkyrimBridge modlist smoke tour")
    p.add_argument("--cells", nargs="*", default=[],
                   help="coc targets, in tour order")
    p.add_argument("--cells-file", help="file with one coc target per line")
    p.add_argument("--settle", type=float, default=6.0,
                   help="seconds to settle after each load")
    p.add_argument("--load-timeout", type=float, default=90.0)
    p.add_argument("--out", default="skyrimbridge-tour.txt")
    a = p.parse_args()

    cells = list(a.cells)
    if a.cells_file:
        with open(a.cells_file, encoding="utf-8") as f:
            cells += [ln.strip() for ln in f if ln.strip() and not ln.startswith("#")]
    if not cells:
        sys.exit("no cells given (--cells or --cells-file); coc targets vary "
                 "by modlist, test yours in the console first")

    try:
        verdict, rows = run_tour(cells, a.settle, a.load_timeout)
    except ChannelDead as e:
        sys.exit(f"could not start: {e}")
    write_report(a.out, verdict, rows)
    print(f"\n{verdict} — report: {a.out}")
    sys.exit({"PASS": 0, "FAILURES": 2, "CRASH": 3}[verdict])


if __name__ == "__main__":
    main()
