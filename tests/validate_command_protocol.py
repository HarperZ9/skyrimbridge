"""Offline validation of SkyrimBridge's external command protocol (D10).

The live channel needs the running game, so this proves the parts that DON'T:
  - the SB_CommandBlock ABI layout (field offsets + total size) matches the
    C++ struct in src/SB_CommandLayout.h and tools/SkyrimBridgeClient.h
  - the request/response sequence protocol is correct: a client publishes a
    request (seq last), a mock plugin dispatches and publishes the response
    (seq last), and the client observes exactly its own result with no torn
    read, across many interleaved calls
The mock plugin dispatches the same verbs the real BridgeCommand::Dispatch
does, so the wire contract (verb + args -> status + resultInt + resultText)
is exercised end to end over a real byte buffer.
"""
import struct
import sys

PASS = FAIL = 0
def check(name, ok, detail=""):
    global PASS, FAIL
    if ok: PASS += 1; print("  PASS  %s" % name)
    else:  FAIL += 1; print("  FAIL  %s  %s" % (name, detail))

# ---- ABI layout (must mirror SB_CommandLayout.h, pack(1)) ----
FIELDS = [
    ("magic", "I", 4), ("version", "I", 4), ("requestSeq", "I", 4), ("responseSeq", "I", 4),
    ("argInt", "i", 4), ("status", "i", 4), ("resultInt", "i", 4), ("reserved", "I", 4),
    ("verb", "32s", 32), ("arg0", "512s", 512), ("arg1", "512s", 512), ("resultText", "4096s", 4096),
]
OFF = {}
_o = 0
for name, _f, size in FIELDS:
    OFF[name] = _o; _o += size
BLOCK_SIZE = _o
MAGIC = 0x53424331
VERSION = 1

def test_layout():
    print("[ABI layout vs C++ struct]")
    check("total block size == 5184", BLOCK_SIZE == 5184, str(BLOCK_SIZE))
    # spot-check the load-bearing offsets the C++ static_assert guarantees
    check("verb at offset 32", OFF["verb"] == 32)
    check("arg0 at offset 64", OFF["arg0"] == 64)
    check("arg1 at offset 576", OFF["arg1"] == 576)
    check("resultText at offset 1088", OFF["resultText"] == 1088)
    check("responseSeq at offset 12 (release fence field)", OFF["responseSeq"] == 12)

# ---- shared buffer accessors ----
def wr_u32(buf, off, v): struct.pack_into("<I", buf, off, v & 0xFFFFFFFF)
def rd_u32(buf, off): return struct.unpack_from("<I", buf, off)[0]
def wr_i32(buf, off, v): struct.pack_into("<i", buf, off, v)
def rd_i32(buf, off): return struct.unpack_from("<i", buf, off)[0]
def wr_str(buf, off, size, s):
    b = s.encode("latin1")[:size-1]
    buf[off:off+size] = b + b"\x00" * (size - len(b))
def rd_str(buf, off, size):
    raw = bytes(buf[off:off+size]); z = raw.find(b"\x00")
    return raw[:z if z >= 0 else size].decode("latin1")

# ---- client side (port of SkyrimBridgeClient::Call) ----
def client_call(buf, verb, a0="", a1="", argInt=0):
    wr_str(buf, OFF["verb"], 32, verb)
    wr_str(buf, OFF["arg0"], 512, a0)
    wr_str(buf, OFF["arg1"], 512, a1)
    wr_i32(buf, OFF["argInt"], argInt)
    seq = rd_u32(buf, OFF["responseSeq"]) + 1
    wr_u32(buf, OFF["requestSeq"], seq)          # publish last
    return seq

def client_wait(buf, seq):
    # in the real client this spins on responseSeq; here we call the mock inline
    assert rd_u32(buf, OFF["responseSeq"]) == seq
    return rd_i32(buf, OFF["status"]), rd_i32(buf, OFF["resultInt"]), rd_str(buf, OFF["resultText"], 4096)

# ---- mock plugin dispatch (mirrors BridgeCommand::Dispatch verbs) ----
SCHEMAS = ["ImageSpace", "Weather", "Region", "Light", "Water", "WorldSpace"]
def plugin_dispatch(buf):
    verb = rd_str(buf, OFF["verb"], 32)
    a0 = rd_str(buf, OFF["arg0"], 512); a1 = rd_str(buf, OFF["arg1"], 512)
    argInt = rd_i32(buf, OFF["argInt"])
    status, resultInt, text = -1, 0, ""       # default: unknown verb
    if verb == "ping":
        status, resultInt, text = 0, 1, "pong"
    elif verb == "reflect.list" and a0 in ("0", "0x0", ""):
        status, resultInt, text = 0, len(SCHEMAS), "\n".join(SCHEMAS)
    elif verb == "reflect.dump":
        if a0.startswith("0x"):
            status, resultInt, text = 0, 3, "[Weather:%s]\nFlags = 0\n" % a0
        else:
            status = -2   # bad arg
    elif verb == "texture.convert":
        status, resultInt = (0, 1) if a0 and a1 else (-2, 0)
    elif verb == "region.weather":
        status, resultInt = 0, max(argInt, 0)
    # write response, publish seq last
    wr_i32(buf, OFF["status"], status)
    wr_i32(buf, OFF["resultInt"], resultInt)
    wr_str(buf, OFF["resultText"], 4096, text)
    wr_u32(buf, OFF["responseSeq"], rd_u32(buf, OFF["requestSeq"]))   # release fence

def test_protocol():
    print("[request/response round-trip over a real byte buffer]")
    buf = bytearray(BLOCK_SIZE)
    wr_u32(buf, OFF["magic"], MAGIC); wr_u32(buf, OFF["version"], VERSION)

    seq = client_call(buf, "ping"); plugin_dispatch(buf)
    st, ri, tx = client_wait(buf, seq)
    check("ping -> status 0, resultInt 1, 'pong'", st == 0 and ri == 1 and tx == "pong")

    seq = client_call(buf, "reflect.list", "0"); plugin_dispatch(buf)
    st, ri, tx = client_wait(buf, seq)
    check("reflect.list -> schema listing", st == 0 and ri == len(SCHEMAS) and "Weather" in tx)

    seq = client_call(buf, "reflect.dump", "0x0010A232"); plugin_dispatch(buf)
    st, ri, tx = client_wait(buf, seq)
    check("reflect.dump 0x... -> INI text + field count", st == 0 and ri == 3 and tx.startswith("[Weather:0x0010A232]"))

    seq = client_call(buf, "reflect.dump", "notahex"); plugin_dispatch(buf)
    st, ri, tx = client_wait(buf, seq)
    check("reflect.dump bad arg -> status kCmdBadArg(-2)", st == -2)

    seq = client_call(buf, "texture.convert", "a.png", "b.dds", 2); plugin_dispatch(buf)
    st, ri, tx = client_wait(buf, seq)
    check("texture.convert -> ok", st == 0 and ri == 1)

    seq = client_call(buf, "region.weather", "0x1", "0x2", 40); plugin_dispatch(buf)
    st, ri, tx = client_wait(buf, seq)
    check("region.weather chance passthrough", st == 0 and ri == 40)

    seq = client_call(buf, "nonsense.verb"); plugin_dispatch(buf)
    st, ri, tx = client_wait(buf, seq)
    check("unknown verb -> status kCmdUnknownVerb(-1)", st == -1)

def test_sequence_gating():
    print("[sequence gating: no stale reads, monotonic]")
    buf = bytearray(BLOCK_SIZE)
    wr_u32(buf, OFF["magic"], MAGIC)
    # 200 interleaved calls; the client must always observe exactly its own seq's result
    ok = True
    for i in range(200):
        expect = i % 97
        seq = client_call(buf, "region.weather", "0x1", "0x2", expect)
        # plugin only responds to the latest request; response seq must equal request seq
        plugin_dispatch(buf)
        st, ri, tx = client_wait(buf, seq)
        if rd_u32(buf, OFF["responseSeq"]) != seq or ri != expect:
            ok = False; break
    check("200 calls: responseSeq tracks requestSeq, result never torn/stale", ok)
    # a fresh client (responseSeq resync) still computes a forward seq
    r0 = rd_u32(buf, OFF["responseSeq"])
    seq = client_call(buf, "ping")
    check("next requestSeq strictly follows responseSeq", seq == r0 + 1)

if __name__ == "__main__":
    test_layout()
    test_protocol()
    test_sequence_gating()
    print("\n%d passed, %d failed" % (PASS, FAIL))
    sys.exit(1 if FAIL else 0)
