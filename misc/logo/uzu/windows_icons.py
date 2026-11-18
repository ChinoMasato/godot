"""Generate Windows packaging icons from the committed official U.ZU raster.

python misc/logo/uzu/windows_icons.py
python misc/logo/uzu/windows_icons.py --verify-exe bin/godot.windows.editor.x86_64.exe

Requires Pillow. The executable check is Windows-only and reads PE resources
without executing the binary. No artwork is downloaded or redrawn.
"""

import argparse
import ctypes
import io
import json
from pathlib import Path
import struct

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[3]
SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
ICONS = ("godot.ico", "godot_console.ico")


def generate():
    with Image.open(ROOT / "misc/logo/icon.png") as original:
        icon = original.convert("RGBA")
        for name in ICONS:
            destination = ROOT / "platform/windows" / name
            icon.save(destination, format="ICO", sizes=[(size, size) for size in SIZES])
            with Image.open(destination) as saved:
                assert saved.ico.sizes() == {(size, size) for size in SIZES}
                assert ImageChops.difference(saved.ico.getimage((256, 256)).convert("RGBA"), icon).getbbox() is None
    return {"ok": True, "files": list(ICONS), "sizes": SIZES}


def verify_executable(path):
    from ctypes import wintypes

    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.LoadLibraryExW.argtypes = [wintypes.LPCWSTR, wintypes.HANDLE, wintypes.DWORD]
    kernel.LoadLibraryExW.restype = wintypes.HMODULE
    kernel.FindResourceW.argtypes = [wintypes.HMODULE, ctypes.c_void_p, ctypes.c_void_p]
    kernel.FindResourceW.restype = ctypes.c_void_p
    kernel.LoadResource.argtypes = [wintypes.HMODULE, ctypes.c_void_p]
    kernel.LoadResource.restype = ctypes.c_void_p
    kernel.LockResource.argtypes = [ctypes.c_void_p]
    kernel.LockResource.restype = ctypes.c_void_p
    kernel.SizeofResource.argtypes = [wintypes.HMODULE, ctypes.c_void_p]
    kernel.SizeofResource.restype = wintypes.DWORD
    kernel.FreeLibrary.argtypes = [wintypes.HMODULE]
    module = kernel.LoadLibraryExW(str(Path(path).resolve()), None, 0x2 | 0x20)
    if not module:
        raise ctypes.WinError(ctypes.get_last_error())

    def resource(name, kind):
        pointer = ctypes.cast(ctypes.c_wchar_p(name), ctypes.c_void_p) if isinstance(name, str) else ctypes.c_void_p(name)
        entry = kernel.FindResourceW(module, pointer, ctypes.c_void_p(kind))
        if not entry:
            raise ctypes.WinError(ctypes.get_last_error())
        size = kernel.SizeofResource(module, entry)
        return ctypes.string_at(kernel.LockResource(kernel.LoadResource(module, entry)), size)

    try:
        group = resource("GODOT_ICON", 14)
        reserved, kind, count = struct.unpack_from("<HHH", group)
        entries, bodies = [], []
        offset = 6 + 16 * count
        for index in range(count):
            width, height, colors, zero, planes, bits, size, identifier = struct.unpack_from("<BBBBHHIH", group, 6 + index * 14)
            data = resource(identifier, 3)
            if len(data) != size:
                raise ValueError("icon resource length mismatch")
            entries.append(struct.pack("<BBBBHHII", width, height, colors, zero, planes, bits, size, offset))
            bodies.append(data)
            offset += size
        packed = struct.pack("<HHH", reserved, kind, count) + b"".join(entries + bodies)
        with Image.open(io.BytesIO(packed)) as actual, Image.open(ROOT / "platform/windows/godot.ico") as expected:
            matches = actual.ico.sizes() == expected.ico.sizes() and all(
                actual.ico.getimage(size).convert("RGBA").tobytes() == expected.ico.getimage(size).convert("RGBA").tobytes()
                for size in expected.ico.sizes())
        data = Path(path).read_bytes()
        splash = (ROOT / "main/splash.png").read_bytes() in data
        app_icon = (ROOT / "main/app_icon.png").read_bytes() in data
        # Console wrapper has no runtime splash/icon PNG; only its native ICO is checked.
        wrapper = str(path).endswith(".console.exe")
        return {"ok": matches and (wrapper or (splash and app_icon)),
                "native_icon_matches": matches, "sizes": count,
                "runtime_splash_matches": splash, "runtime_app_icon_matches": app_icon, "console_wrapper": wrapper}
    finally:
        kernel.FreeLibrary(module)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-exe")
    args = parser.parse_args()
    report = verify_executable(args.verify_exe) if args.verify_exe else generate()
    print(json.dumps(report))
    raise SystemExit(0 if report["ok"] else 1)
