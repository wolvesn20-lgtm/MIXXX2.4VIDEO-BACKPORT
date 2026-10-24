#!/usr/bin/env python3
"""Finish a macOS .app bundle after macdeployqt and fail the build if it is broken.

What it does, in order:
  1. Deploys the Qt plugins macdeployqt skipped (cocoa platform, SQLite driver, ...).
  2. Finds every dependency that points outside the bundle (or cannot be found
     inside it), copies the library into Contents/Frameworks and rewrites the
     reference. Repeats until nothing changes, so transitive dependencies such
     as libutf8_validity (needed by libprotobuf-lite) are picked up too.
  3. Verifies the result and exits non-zero if a required dependency or plugin
     is still missing, so a broken DMG can no longer be published silently.

It reads Mach-O load commands itself (no otool needed) and uses only
install_name_tool for edits, so run it on the macOS runner.

usage:
  finalize_bundle.py Mixxx.app --qt-dir "$QT_ROOT_DIR" [--search DIR ...]
  finalize_bundle.py Mixxx.app --verify-only --min-macos 10.13
  finalize_bundle.py --scan-dir DEPS/lib --min-macos 10.13
"""
import argparse
import glob
import os
import plistlib
import shutil
import struct
import subprocess
import sys

# --------------------------------------------------------------------------
# Minimal Mach-O reader
# --------------------------------------------------------------------------
LC_LOAD_DYLIB, LC_ID_DYLIB, LC_LAZY_LOAD = 0xC, 0xD, 0x20
LC_LOAD_WEAK, LC_REEXPORT, LC_UPWARD = 0x80000018, 0x8000001F, 0x80000023
LC_RPATH = 0x8000001C
DEP_CMDS = (LC_LOAD_DYLIB, LC_LOAD_WEAK, LC_REEXPORT, LC_LAZY_LOAD, LC_UPWARD)
THIN = {0xFEEDFACE: ("<", 32), 0xFEEDFACF: ("<", 64),
        0xCEFAEDFE: (">", 32), 0xCFFAEDFE: (">", 64)}
MAGIC_BYTES = {struct.pack(">I", m) for m in
               list(THIN) + [0xCAFEBABE, 0xCAFEBABF]} | \
              {struct.pack("<I", m) for m in THIN}


def _thin(d):
    if len(d) < 32:
        return None
    m = struct.unpack("<I", d[:4])[0]
    if m not in THIN:
        return None
    end, bits = THIN[m]
    off = 32 if bits == 64 else 28
    ncmds = struct.unpack(end + "I", d[16:20])[0]
    info = {"deps": [], "rpaths": []}
    for _ in range(ncmds):
        cmd, size = struct.unpack(end + "II", d[off:off + 8])
        if cmd in DEP_CMDS or cmd == LC_RPATH:
            name_off = struct.unpack(end + "I", d[off + 8:off + 12])[0]
            name = d[off + name_off:off + size].split(b"\0")[0].decode()
            if cmd == LC_RPATH:
                info["rpaths"].append(name)
            else:
                info["deps"].append((cmd, name))
        off += size
    return info


def parse(path):
    """Return load-command info for the first architecture, or None."""
    with open(path, "rb") as f:
        d = f.read()
    if struct.unpack(">I", d[:4])[0] in (0xCAFEBABE, 0xCAFEBABF):
        wide = struct.unpack(">I", d[:4])[0] == 0xCAFEBABF
        if wide:
            off, size = struct.unpack(">QQ", d[16:32])
        else:
            off, size = struct.unpack(">II", d[16:24])
        return _thin(d[off:off + size])
    return _thin(d)


def bundle_machos(app):
    for root, _dirs, files in os.walk(app):
        for fn in files:
            p = os.path.join(root, fn)
            if os.path.islink(p) or not os.path.isfile(p):
                continue
            try:
                with open(p, "rb") as f:
                    if f.read(4) not in MAGIC_BYTES:
                        continue
                info = parse(p)
            except (OSError, struct.error):
                continue
            if info:
                yield p, info


LC_VERSION_MIN_MACOSX, LC_BUILD_VERSION = 0x24, 0x32
LC_DYLD_CHAINED_FIXUPS = 0x80000034   # required by dyld; unknown to macOS < 12


def _ver(v):
    return (v >> 16, (v >> 8) & 0xFF)


def target_info(path):
    """Return ((major, minor) minimum macOS or None, uses_chained_fixups)."""
    with open(path, "rb") as f:
        d = f.read()
    if struct.unpack(">I", d[:4])[0] in (0xCAFEBABE, 0xCAFEBABF):
        d = d[struct.unpack(">I", d[16:20])[0]:]
    end, bits = THIN[struct.unpack("<I", d[:4])[0]]
    off = 32 if bits == 64 else 28
    minos, chained = None, False
    for _ in range(struct.unpack(end + "I", d[16:20])[0]):
        cmd, size = struct.unpack(end + "II", d[off:off + 8])
        if cmd == LC_BUILD_VERSION:
            minos = _ver(struct.unpack(end + "I", d[off + 12:off + 16])[0])
        elif cmd == LC_VERSION_MIN_MACOSX:
            minos = _ver(struct.unpack(end + "I", d[off + 8:off + 12])[0])
        elif cmd == LC_DYLD_CHAINED_FIXUPS:
            chained = True
        off += size
    return minos, chained


# --------------------------------------------------------------------------
# Dependency resolution (mimics what dyld does)
# --------------------------------------------------------------------------
FW_REL = "@executable_path/../Frameworks/"


def is_system(name):
    return name.startswith(("/usr/lib/", "/System/"))


class Bundle:
    def __init__(self, app):
        self.app = os.path.abspath(app)
        self.app_real = os.path.realpath(self.app)
        self.fw = os.path.join(self.app, "Contents", "Frameworks")
        self.exe = self._find_exe()
        self.exe_dir = os.path.dirname(self.exe)
        info = parse(self.exe)
        self.exe_rpaths = info["rpaths"] if info else []

    def _find_exe(self):
        plist = os.path.join(self.app, "Contents", "Info.plist")
        with open(plist, "rb") as f:
            name = plistlib.load(f)["CFBundleExecutable"]
        exe = os.path.join(self.app, "Contents", "MacOS", name)
        if not os.path.isfile(exe):
            sys.exit("ERROR: main executable not found: " + exe)
        return exe

    def _expand(self, rp, loader):
        if rp.startswith("@executable_path"):
            return self.exe_dir + rp[len("@executable_path"):]
        if rp.startswith("@loader_path"):
            return os.path.dirname(loader) + rp[len("@loader_path"):]
        return rp

    def resolves(self, name, loader, info):
        """True if `name`, as referenced from `loader`, lands inside the bundle."""
        if name.startswith("@executable_path/"):
            cands = [os.path.join(self.exe_dir, name[17:])]
        elif name.startswith("@loader_path/"):
            cands = [os.path.join(os.path.dirname(loader), name[13:])]
        elif name.startswith("@rpath/"):
            # dyld tries the loader's rpaths, then the main executable's
            cands = [os.path.join(self._expand(rp, loader), name[7:])
                     for rp in info["rpaths"] + self.exe_rpaths]
        else:
            cands = [name]
        for c in cands:
            c = os.path.normpath(c)
            if os.path.exists(c) and \
               os.path.realpath(c).startswith(self.app_real + os.sep):
                return True
        return False

    def unresolved(self):
        out = []
        for path, info in bundle_machos(self.app):
            for kind, name in info["deps"]:
                if is_system(name) or self.resolves(name, path, info):
                    continue
                out.append((path, kind, name))
        return out


# --------------------------------------------------------------------------
# Helpers
# --------------------------------------------------------------------------
def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print("  ! %s failed: %s" % (" ".join(cmd[:2]), r.stderr.strip()))
    return r.returncode == 0


def rel(app, p):
    return os.path.relpath(p, app)


def search_dirs(extra, qt_dir):
    dirs = list(extra or [])
    if qt_dir:
        dirs.append(os.path.join(qt_dir, "lib"))
    dirs += sorted(glob.glob("/usr/local/opt/*/lib"))
    dirs += sorted(glob.glob("/opt/homebrew/opt/*/lib"))
    dirs += ["/usr/local/lib", "/opt/homebrew/lib"]
    return dirs


def find_source(name, dirs):
    if os.path.isabs(name) and os.path.exists(name):
        return name
    base = os.path.basename(name)
    for d in dirs:
        p = os.path.join(d, base)
        if os.path.exists(p):
            return p
    return None


# --------------------------------------------------------------------------
# Step 1: Qt plugins
# --------------------------------------------------------------------------
PLUGINS = {
    "platforms": ["libqcocoa.dylib"],
    "styles": ["*.dylib"],
    "imageformats": ["*.dylib"],
    "sqldrivers": ["libqsqlite.dylib"],   # the other SQL drivers need DB client libs
    "iconengines": ["*.dylib"],
    "printsupport": ["*.dylib"],
    "bearer": ["*.dylib"],
    "tls": ["*.dylib"],
}
REQUIRED_PLUGINS = ["platforms/libqcocoa.dylib",
                    "sqldrivers/libqsqlite.dylib",
                    "imageformats/libqsvg.dylib"]


def deploy_plugins(b, qt_dir):
    if not qt_dir:
        print("No --qt-dir given; skipping plugin deployment.")
        return
    dst_root = os.path.join(b.app, "Contents", "PlugIns")
    for sub, patterns in PLUGINS.items():
        for pat in patterns:
            for src in sorted(glob.glob(os.path.join(qt_dir, "plugins", sub, pat))):
                if src.endswith("_debug.dylib"):
                    continue
                dst = os.path.join(dst_root, sub, os.path.basename(src))
                if os.path.exists(dst):
                    continue
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copy2(src, dst)
                os.chmod(dst, 0o755)
                # harmless if the rpath is already present
                subprocess.run(["install_name_tool", "-add_rpath",
                                "@loader_path/../../Frameworks", dst],
                               capture_output=True)
                print("plugin: " + rel(b.app, dst))


# --------------------------------------------------------------------------
# Step 2: copy missing libraries and rewrite references
# --------------------------------------------------------------------------
def fix_dependencies(b, dirs, qt_dir, max_passes):
    os.makedirs(b.fw, exist_ok=True)
    for n in range(1, max_passes + 1):
        changed = False
        for path, info in list(bundle_machos(b.app)):
            for kind, name in info["deps"]:
                if kind == LC_LOAD_WEAK or is_system(name):
                    continue
                if b.resolves(name, path, info):
                    continue
                # Qt frameworks: copy the whole framework, leave the @rpath ref alone
                if ".framework/" in name:
                    fw_name = next(p for p in name.split("/") if p.endswith(".framework"))
                    dest = os.path.join(b.fw, fw_name)
                    src = os.path.join(qt_dir or "", "lib", fw_name)
                    if not os.path.exists(dest) and os.path.isdir(src):
                        shutil.copytree(src, dest, symlinks=True)
                        print("framework: " + fw_name)
                        changed = True
                    continue
                base = os.path.basename(name)
                dest = os.path.join(b.fw, base)
                if not os.path.exists(dest):
                    src = find_source(name, dirs)
                    if not src:
                        continue          # reported by verify()
                    shutil.copy2(os.path.realpath(src), dest)
                    os.chmod(dest, 0o755)
                    run(["install_name_tool", "-id", FW_REL + base, dest])
                    print("bundled: %s  (needed by %s)" % (base, rel(b.app, path)))
                    changed = True
                # point the reference at the bundled copy if it still doesn't resolve
                if not b.resolves(name, path, info):
                    if run(["install_name_tool", "-change", name, FW_REL + base, path]):
                        changed = True
        if not changed:
            return
    print("WARNING: dependency fixing did not settle after %d passes" % max_passes)


def ensure_exe_rpath(b):
    if "@executable_path/../Frameworks" not in b.exe_rpaths:
        run(["install_name_tool", "-add_rpath",
             "@executable_path/../Frameworks", b.exe])
        b.exe_rpaths.append("@executable_path/../Frameworks")


# --------------------------------------------------------------------------
# Step 3: verify
# --------------------------------------------------------------------------
def verify(b, min_macos=None):
    errors, warnings = [], []
    for path, kind, name in b.unresolved():
        msg = "%s -> %s" % (rel(b.app, path), name)
        (warnings if kind == LC_LOAD_WEAK else errors).append(msg)
    for p in REQUIRED_PLUGINS:
        if not os.path.isfile(os.path.join(b.app, "Contents", "PlugIns", p)):
            errors.append("missing Qt plugin: Contents/PlugIns/" + p)
    machos = [p for p, _ in bundle_machos(b.app)]
    if min_macos:
        want = tuple(int(x) for x in min_macos.split("."))[:2]
        too_new, chained = [], []
        for p in machos:
            minos, ch = target_info(p)
            if minos and minos > want:
                too_new.append((p, minos))
            if ch and want < (12, 0):
                chained.append(p)
        if too_new:
            names = ", ".join(sorted({os.path.basename(p) for p, _ in too_new})[:6])
            errors.append(
                "%d of %d Mach-O files were built for a newer macOS than %s "
                "(highest: %d.%d), e.g. %s"
                % (len(too_new), len(machos), min_macos,
                   *max(m for _, m in too_new), names))
        if chained:
            errors.append(
                "%d files use chained fixups, which macOS before 12 cannot load "
                "(dyld rejects the unknown required load command)" % len(chained))
    n = len(machos)
    print("\nChecked %d Mach-O files." % n)
    for w in warnings:
        print("warning (weak, will not crash): " + w)
    for e in errors:
        print("ERROR: " + e)
    if not errors:
        print("Bundle OK: every required dependency and plugin is inside the app.")
    return not errors


def scan_dir(root, min_macos):
    """Check every Mach-O under `root` (e.g. a deps `lib` dir) against a target."""
    want = tuple(int(x) for x in min_macos.split("."))[:2]
    files = [p for p, _ in bundle_machos(root)]
    bad = []
    for p in files:
        minos, chained = target_info(p)
        if (minos and minos > want) or (chained and want < (12, 0)):
            bad.append((p, minos, chained))
    print("Scanned %d Mach-O files under %s" % (len(files), root))
    for p, minos, chained in sorted(bad)[:40]:
        print("ERROR: %s  (built for macOS %s%s)" % (
            os.path.relpath(p, root),
            "%d.%d" % minos if minos else "?",
            ", chained fixups" if chained else ""))
    if len(bad) > 40:
        print("... and %d more" % (len(bad) - 40))
    if not files:
        print("ERROR: no libraries found; wrong directory?")
        return False
    if not bad:
        print("OK: everything targets macOS %s or older." % min_macos)
    return not bad


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("app", nargs="?")
    ap.add_argument("--scan-dir", metavar="DIR",
                    help="only check the libraries under DIR against --min-macos")
    ap.add_argument("--qt-dir", help="Qt prefix, e.g. $QT_ROOT_DIR (has lib/ and plugins/)")
    ap.add_argument("--search", action="append", default=[],
                    help="extra directory to look for libraries (repeatable)")
    ap.add_argument("--min-macos", metavar="X.Y",
                    help="fail if any file was built for a newer macOS, e.g. 10.13")
    ap.add_argument("--verify-only", action="store_true")
    ap.add_argument("--max-passes", type=int, default=8)
    a = ap.parse_args()

    if a.scan_dir:
        if not a.min_macos:
            ap.error("--scan-dir needs --min-macos")
        sys.exit(0 if scan_dir(a.scan_dir, a.min_macos) else 1)
    if not a.app:
        ap.error("an .app path is required")
    b = Bundle(a.app)
    if not a.verify_only:
        ensure_exe_rpath(b)
        deploy_plugins(b, a.qt_dir)
        fix_dependencies(b, search_dirs(a.search, a.qt_dir), a.qt_dir, a.max_passes)
    sys.exit(0 if verify(b, a.min_macos) else 1)


if __name__ == "__main__":
    main()
