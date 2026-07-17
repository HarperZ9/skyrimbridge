# =============================================================================
#  skyrimbridge_push.py — Blender "Push to Game" addon for SkyrimBridge
#
#  One button: export the selection as glTF and hand it to a RUNNING Skyrim SE
#  through SkyrimBridge's shared-memory command channel (model.spawn). The
#  mesh appears at the player, loaded by the engine's own model loader, in
#  seconds; no Creation Kit, no restart.
#
#  Requirements
#  - Windows, same machine as the game (the channel is named shared memory).
#  - Skyrim SE running with SkyrimBridge.dll and [Native] CommandSurface = true.
#  - A loaded save (the spawn needs the player). MUTATES the save: dynamic
#    form + placed reference. Use a disposable save while iterating; clean up
#    with the console (click the ref, "markfordelete").
#
#  Install: Edit > Preferences > Add-ons > Install..., pick this file, enable
#  "SkyrimBridge Push to Game". Panel: View3D sidebar (N) > SkyrimBridge.
#
#  Coordinates: exported with +Z up (export_yup=False). SkyrimBridge's
#  ModelCodec passes glTF coordinates through unchanged into the NIF, and NIF
#  space is Z-up, so a Z-up export arrives upright.
#
#  Scope, honestly: static single-shape meshes (first primitive/group), no
#  collision on the spawned object, material fidelity bounded by what the
#  exporter carries. Conversion runs on the game's frame thread: a very large
#  mesh will hitch the game for the duration of the convert.
#
#  Author: Zain Dana Harper
#  License: MIT
# =============================================================================

bl_info = {
    "name": "SkyrimBridge Push to Game",
    "author": "Zain Dana Harper",
    "version": (1, 1, 0),
    "blender": (3, 0, 0),
    "location": "View3D > Sidebar > SkyrimBridge",
    "description": "Push the selected mesh into a running Skyrim SE via SkyrimBridge",
    "category": "Import-Export",
}

import mmap
import os
import re
import struct
import sys
import tempfile
import time

try:
    import bpy
except ImportError:          # imported outside Blender (tests): protocol only
    bpy = None

# ── SB_CommandLayout protocol (mirrors src/SB_CommandLayout.h) ──────────────
SIZE = 5184
MAGIC = 0x53424331           # 'SBC1'
CHANNEL_NAME = "SkyrimBridge_Command"

OFF_MAGIC, OFF_REQ, OFF_RESP = 0, 8, 12
OFF_ARGINT, OFF_STATUS, OFF_RESINT = 16, 20, 24
OFF_VERB, OFF_ARG0, OFF_ARG1, OFF_TEXT = 32, 64, 576, 1088

STATUS_TEXT = {0: "OK", -1: "unknown verb", -2: "bad argument",
               -3: "not found", -4: "failed"}


class ChannelError(RuntimeError):
    pass


class SBChannel:
    """Single-slot sequence-gated mailbox client (one request in flight)."""

    def __init__(self, name=CHANNEL_NAME):
        self.name = name
        self.m = None

    def open(self):
        if sys.platform != "win32":
            raise ChannelError("the SkyrimBridge channel is Windows named "
                               "shared memory; run Blender on the game machine")
        try:
            self.m = mmap.mmap(-1, SIZE, tagname=self.name)
        except (OSError, ValueError) as e:
            raise ChannelError(f"could not open shared memory: {e}")
        if self._u32(OFF_MAGIC) != MAGIC:
            self.m.close()
            self.m = None
            raise ChannelError("no live channel (bad magic). Is the game "
                               "running with [Native] CommandSurface = true?")
        return self

    def close(self):
        if self.m:
            self.m.close()
            self.m = None

    def connected(self):
        return self.m is not None and self._u32(OFF_MAGIC) == MAGIC

    def _u32(self, off):
        return struct.unpack_from("<I", self.m, off)[0]

    def _i32(self, off):
        return struct.unpack_from("<i", self.m, off)[0]

    def request(self, verb, arg0="", arg1="", arg_int=0, timeout=30.0):
        """Send one request, wait for the response.
        Returns (status, resultInt, resultText)."""
        if not self.connected():
            raise ChannelError("channel is not open")
        req = self._u32(OFF_REQ) + 1
        struct.pack_into("32s", self.m, OFF_VERB, verb.encode()[:31])
        struct.pack_into("512s", self.m, OFF_ARG0, arg0.encode()[:511])
        struct.pack_into("512s", self.m, OFF_ARG1, arg1.encode()[:511])
        struct.pack_into("<i", self.m, OFF_ARGINT, arg_int)
        struct.pack_into("<I", self.m, OFF_REQ, req)     # publish last
        deadline = time.monotonic() + timeout
        while self._u32(OFF_RESP) != req:
            if time.monotonic() > deadline:
                raise ChannelError(
                    f"timeout after {timeout:.0f}s. The plugin dispatches one "
                    "request per frame: unpause the game (close menus and "
                    "console) and retry.")
            time.sleep(0.005)
        text = self.m[OFF_TEXT:OFF_TEXT + 4096].split(b"\0", 1)[0].decode(errors="replace")
        return self._i32(OFF_STATUS), self._i32(OFF_RESINT), text


def sanitize_name(name):
    """Object name -> a filesystem/NIF-safe stem."""
    s = re.sub(r"[^A-Za-z0-9_\-]+", "_", name).strip("_")
    return s or "pushed"


def push_glb(glb_path, timeout=30.0, channel_name=CHANNEL_NAME, tree=False):
    """Spawn an exported mesh in the running game. tree=True converts it as a
    wind-animated tree (procedural sway weights + Tree_Anim shader).
    Returns (form_id, message). Raises ChannelError on any failure."""
    ch = SBChannel(channel_name).open()
    try:
        status, form_id, text = ch.request("model.spawn", glb_path,
                                           arg_int=1 if tree else 0, timeout=timeout)
    finally:
        ch.close()
    if status != 0 or not form_id:
        raise ChannelError(text or STATUS_TEXT.get(status, f"status {status}"))
    # resultInt is int32 on the wire; dynamic FormIDs are 0xFFxxxxxx, so the
    # bit pattern arrives negative. Normalize back to the unsigned FormID.
    return form_id & 0xFFFFFFFF, text


# ── Blender surface ─────────────────────────────────────────────────────────
if bpy is not None:

    class SKYRIMBRIDGE_OT_push(bpy.types.Operator):
        """Export the selection as glTF and spawn it at the player in a running Skyrim SE (mutates the save)"""
        bl_idname = "skyrimbridge.push"
        bl_label = "Push to Game"
        bl_options = {"REGISTER"}

        @classmethod
        def poll(cls, context):
            return context.selected_objects and any(
                o.type == "MESH" for o in context.selected_objects)

        def execute(self, context):
            wm = context.window_manager
            name = sanitize_name(context.active_object.name
                                 if context.active_object else "pushed")
            out_dir = os.path.join(tempfile.gettempdir(), "skyrimbridge_push")
            os.makedirs(out_dir, exist_ok=True)
            glb = os.path.join(out_dir, name + ".glb")

            try:
                bpy.ops.export_scene.gltf(
                    filepath=glb,
                    export_format="GLB",
                    use_selection=True,
                    export_apply=True,     # apply modifiers
                    export_yup=False,      # keep Z-up: NIF space (see header)
                )
            except Exception as e:
                wm.skyrimbridge_last = f"export failed: {e}"
                self.report({"ERROR"}, wm.skyrimbridge_last)
                return {"CANCELLED"}

            try:
                form_id, text = push_glb(glb, tree=wm.skyrimbridge_tree)
            except ChannelError as e:
                wm.skyrimbridge_last = str(e)
                self.report({"ERROR"}, wm.skyrimbridge_last)
                return {"CANCELLED"}

            wm.skyrimbridge_last = f"{name}: {text}"
            self.report({"INFO"}, wm.skyrimbridge_last)
            return {"FINISHED"}

    class SKYRIMBRIDGE_PT_panel(bpy.types.Panel):
        bl_label = "SkyrimBridge"
        bl_space_type = "VIEW_3D"
        bl_region_type = "UI"
        bl_category = "SkyrimBridge"

        def draw(self, context):
            col = self.layout.column()
            col.operator("skyrimbridge.push", icon="EXPORT")
            col.prop(context.window_manager, "skyrimbridge_tree",
                     text="Push as tree (wind sway)")
            last = getattr(context.window_manager, "skyrimbridge_last", "")
            if last:
                for chunk in [last[i:i + 38] for i in range(0, len(last), 38)]:
                    col.label(text=chunk)
            col.label(text="Needs: game running, CommandSurface on")
            col.label(text="Mutates the save: iterate on a spare save")

    _classes = (SKYRIMBRIDGE_OT_push, SKYRIMBRIDGE_PT_panel)

    def register():
        bpy.types.WindowManager.skyrimbridge_last = bpy.props.StringProperty(default="")
        bpy.types.WindowManager.skyrimbridge_tree = bpy.props.BoolProperty(
            default=False, name="Push as tree",
            description="Convert with procedural wind sway weights and the "
                        "Tree_Anim shader flag (trunk stiff, canopy sways)")
        for c in _classes:
            bpy.utils.register_class(c)

    def unregister():
        for c in reversed(_classes):
            bpy.utils.unregister_class(c)
        del bpy.types.WindowManager.skyrimbridge_last
        del bpy.types.WindowManager.skyrimbridge_tree

    if __name__ == "__main__":
        register()
