#!/usr/bin/env python3
"""External command-channel client for SkyrimBridge. No compiler needed.

Speaks the SB_CommandLayout protocol (src/SB_CommandLayout.h) over the named
shared-memory region "SkyrimBridge_Command". Requirements: the game running
with [Native] CommandSurface = true, same machine, one client at a time (the
mailbox is single-slot by design).

Usage:
  python sb_command_client.py ping
  python sb_command_client.py reflect.list 0
  python sb_command_client.py reflect.dump 0x10A241
  python sb_command_client.py reflect.apply 0x10A241        (edits dumps/<id>.ini first)
  python sb_command_client.py reflect.verify 0x10A241        (--int 1 = strict, MUTATES)
  python sb_command_client.py reflect.chain 0x10A241          (override chain, winner last)
  python sb_command_client.py texture.info textures/foo.dds   (format/dims/mips, header-only)
  python sb_command_client.py region.dump 0x<region>
  python sb_command_client.py region.weather 0x<region> 0x<weather> --int <chance>
  python sb_command_client.py texture.convert in.png out.dds --int 2   (0/1/2/3 = RGBA8/BC1/BC3/BC7)
  python sb_command_client.py texture.foliage in.png out.dds --int 2   (+ threshold*256; 0 -> 128)
  python sb_command_client.py texture.scan --int 1                     (1 dry, 0 live)
  python sb_command_client.py model.convert in.obj out.nif    (--int bits: 1 tree, 2 collision)
  python sb_command_client.py model.spawn in.obj      (places at player; MUTATES the
                                                       save; --int bits: 1 tree, 2 collision;
                                                       high byte = collision piece count, e.g.
                                                       2 | 8<<8 = 2050 for 8-piece decomposition)

Dispatch happens one request per frame on the game thread: if the game is
paused (menus, console open), the response waits until it unpauses.
"""

import argparse
import mmap
import struct
import sys
import time

SIZE = 5184
MAGIC = 0x53424331          # 'SBC1'
NAME = "SkyrimBridge_Command"

OFF_MAGIC, OFF_VERSION, OFF_REQ, OFF_RESP = 0, 4, 8, 12
OFF_ARGINT, OFF_STATUS, OFF_RESINT = 16, 20, 24
OFF_VERB, OFF_ARG0, OFF_ARG1, OFF_TEXT = 32, 64, 576, 1088

STATUS = {0: "OK", -1: "unknown verb", -2: "bad argument",
          -3: "not found", -4: "failed"}


def u32(m, off):
    return struct.unpack_from("<I", m, off)[0]


def i32(m, off):
    return struct.unpack_from("<i", m, off)[0]


def main():
    p = argparse.ArgumentParser(description="SkyrimBridge command client")
    p.add_argument("verb")
    p.add_argument("arg0", nargs="?", default="")
    p.add_argument("arg1", nargs="?", default="")
    p.add_argument("--int", dest="arg_int", type=int, default=0,
                   help="argInt (chance / format enum / dry-run flag)")
    p.add_argument("--timeout", type=float, default=15.0)
    a = p.parse_args()

    # Opens the existing named mapping, or creates a zeroed one if the game
    # is not running (which the magic check then reports).
    m = mmap.mmap(-1, SIZE, tagname=NAME)
    if u32(m, OFF_MAGIC) != MAGIC:
        sys.exit("no live channel: bad magic. Is the game running with "
                 "[Native] CommandSurface = true?")

    req = u32(m, OFF_REQ) + 1
    struct.pack_into("32s", m, OFF_VERB, a.verb.encode()[:31])
    struct.pack_into("512s", m, OFF_ARG0, a.arg0.encode()[:511])
    struct.pack_into("512s", m, OFF_ARG1, a.arg1.encode()[:511])
    struct.pack_into("<i", m, OFF_ARGINT, a.arg_int)
    struct.pack_into("<I", m, OFF_REQ, req)      # publish last

    deadline = time.monotonic() + a.timeout
    while u32(m, OFF_RESP) != req:
        if time.monotonic() > deadline:
            sys.exit(f"timeout after {a.timeout:.0f}s. The plugin dispatches "
                     "one request per frame: unpause the game (close menus "
                     "and console) and retry.")
        time.sleep(0.005)

    status = i32(m, OFF_STATUS)
    text = m[OFF_TEXT:OFF_TEXT + 4096].split(b"\0", 1)[0].decode(errors="replace")
    res = i32(m, OFF_RESINT)
    print(f"status    {status} ({STATUS.get(status, '?')})")
    # hex shows the u32 bit pattern: FormIDs (model.spawn) are 0xFFxxxxxx
    print(f"resultInt {res} (0x{res & 0xFFFFFFFF:08X})")
    if text:
        print(text)
    sys.exit(0 if status == 0 else 2)


if __name__ == "__main__":
    main()
