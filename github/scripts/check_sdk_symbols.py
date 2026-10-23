#!/usr/bin/env python3
"""Check that binaries only use system symbols that exist in an older macOS SDK."""
import argparse
import collections
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from finalize_bundle import DEP_CMDS, LC_LOAD_WEAK, THIN, bundle_machos  # noqa: E402

CPU_X86_64 = 0x01000007
MH_TWOLEVEL = 0x80
LC_SYMTAB = 0x2
N_WEAK_REF = 0x0040

def _x86_64_slice(d):
    magic = struct.unpack(">I", d[:4])[0]
    if magic not in (0xCAFEBABE, 0xCAFEBABF):
        return d
    wide = magic == 0xCAFEBABF
    n = struct.unpack(">I", d[4:8])[0]
    size = 32 if wide else 20
    first = None
    for i in range(n):
        e = d[8 + i * size:8 + (i + 1) * size]
        if wide:
            cpu, _sub, off, length, _al, _r = struct.unpack(">iiQQII", e)
        else:
            cpu, _sub, off, length, _al = struct.unpack(">iiIII", e)
        first = first or (off, length)
        if cpu == CPU_X86_64:
            return d[off:off + length]
    return d[first[0]:first[0] + first[1]] if first else None

def read_macho(path):
    with open(path, "rb") as f:
        d = _x86_64_slice(f.read())
    if not d or len(d) < 32:
        return None
    magic = struct.unpack("<I", d[:4])[0]
    if magic not in THIN:
        return None
    end, bits = THIN[magic]
    cputype, _sub, _ft, ncmds, _sz, flags = struct.unpack(end + "iiIIII", d[4:28])
    if cputype != CPU_X86_64 or not flags & MH_TWOLEVEL:
        return None
    off = 32 if bits == 64 else 28
    deps, symtab = [], None
    for _ in range(ncmds):
        cmd, size = struct.unpack(end + "II", d[off:off + 8])
        if cmd in DEP_CMDS:
            name_off = struct.unpack(end + "I", d[off + 8:off + 12])[0]
            deps.append((cmd, d[off + name_off:off + size].split(b"\0")[0].decode()))
        elif cmd == LC_SYMTAB:
            symtab = struct.unpack(end + "IIII", d[off + 8:off + 24])
        off += size
    if not symtab:
        return deps, []
    symoff, nsyms, stroff, strsize = symtab
    strtab = d[stroff:stroff + strsize]
    entry = 16 if bits == 64 else 12
    out = []
    for i in range(nsyms):
        e = d[symoff + i * entry:symoff + (i + 1) * entry]
        strx, ntype, _sect, desc = struct.unpack(end + "IBBH", e[:8])
        if ntype & 0xE0 or ntype & 0x0E or not ntype & 1:
            continue
        value = struct.unpack(end + ("Q" if bits == 64 else "I"), e[8:entry])[0]
        if value:
            continue
        name = strtab[strx:strtab.index(b"\0", strx)].decode("utf-8", "replace")
        out.append((name, (desc >> 8) & 0xFF, bool(desc & N_WEAK_REF)))
    return deps, out

def _read_flow(lines, i, rest):
    buf = rest.strip()
    if not buf.startswith("["):
        return ([buf.strip("'\"")] if buf else []), i + 1
    while "]" not in buf and i + 1 < len(lines):
        i += 1
        buf += " " + lines[i].strip()
    inner = buf[buf.index("[") + 1:buf.rindex("]")] if "]" in buf else buf[1:]
    return [x.strip().strip("'\"") for x in inner.split(",") if x.strip()], i + 1

def _arch_ok(archs):
    return not archs or any(a.split("-")[0] == "x86_64" for a in archs)

SYMBOL_KEYS = ("symbols", "weak-def-symbols", "weak-symbols", "thread-local-symbols")

def parse_tbd_doc(lines):
    install, items, section = None, [], None
    i = 0
    while i < len(lines):
        raw = lines[i]
        s = raw.strip()
        if not s or s.startswith("#"):
            i += 1
            continue
        if not raw[0].isspace():
            key, _, rest = s.partition(":")
            section = key
            if key == "install-name":
                install = rest.strip().strip("'\"")
            i += 1
            continue
        if section in ("exports", "reexported-libraries"):
            if s.startswith("- "):
                items.append({"section": section, "archs": [], "lists": {}})
                s = s[2:].strip()
            key, _, rest = s.partition(":")
            vals, i = _read_flow(lines, i, rest)
            if items:
                if key in ("archs", "targets"):
                    items[-1]["archs"] = vals
                else:
                    items[-1]["lists"][key] = vals
            continue
        i += 1
    if not install:
        return None
    symbols, reexports = set(), []
    for it in items:
        if not _arch_ok(it["archs"]):
            continue
        for key in SYMBOL_KEYS:
            symbols.update(it["lists"].get(key, []))
        for c in it["lists"].get("objc-classes", []):
            symbols.add("_OBJC_CLASS_$_" + c)
            symbols.add("_OBJC_METACLASS_$_" + c)
        for c in it["lists"].get("objc-eh-types", []):
            symbols.add("_OBJC_EHTYPE_$_" + c)
        for c in it["lists"].get("objc-ivars", []):
            symbols.add("_OBJC_IVAR_$_" + c)
        reexports += it["lists"].get("re-exports", [])
        reexports += it["lists"].get("libraries", [])
    return {"install": install, "symbols": symbols, "reexports": reexports}

def parse_tbd_text(text):
    docs, cur = [], None
    for line in text.splitlines():
        if line.startswith("--- "):
            if cur is not None:
                docs.append(cur)
            cur = []
        elif line.startswith("...") and cur is not None:
            docs.append(cur)
            cur = None
        elif cur is not None:
            cur.append(line)
    if cur is not None:
        docs.append(cur)
    return [d for d in (parse_tbd_doc(x) for x in docs) if d]

def candidates(install):
    rel = install.lstrip("/")
    m = re.match(r"^(.*\.framework)/(?:Versions/[^/]+/)?([^/]+)$", rel)
    if m:
        fw, name = m.groups()
        return ["%s/%s.tbd" % (fw, name), "%s/Versions/A/%s.tbd" % (fw, name)]
    base, name = os.path.split(rel)
    out = []
    if name.endswith(".dylib"):
        stem = name[:-6]
        out.append(os.path.join(base, stem + ".tbd"))
        m2 = re.match(r"^(lib[^.]+)\.", stem)
        if m2:
            out.append(os.path.join(base, m2.group(1) + ".tbd"))
    return out

class Sdk:
    def __init__(self, root):
        self.root = root
        self.docs, self.closure, self.tried = {}, {}, set()
        self.unresolved_reexports = set()

    def _load_file(self, path):
        with open(path, encoding="utf-8", errors="replace") as f:
            for d in parse_tbd_text(f.read()):
                self.docs.setdefault(d["install"], d)

    def doc(self, install):
        if install in self.docs:
            return self.docs[install]
        for rel in candidates(install):
            p = os.path.join(self.root, rel)
            if p in self.tried:
                continue
            self.tried.add(p)
            if os.path.isfile(p):
                self._load_file(p)
                if install in self.docs:
                    return self.docs[install]
        return None

    def exports(self, install, stack=()):
        if install in self.closure:
            return self.closure[install]
        d = self.doc(install)
        if d is None:
            return None
        out = set(d["symbols"])
        for r in d["reexports"]:
            if r in stack or r == install:
                continue
            sub = self.exports(r, stack + (install,))
            if sub is None:
                self.unresolved_reexports.add(r)
            else:
                out |= sub
        self.closure[install] = out
        return out

    def framework_exists(self, install):
        m = re.match(r"^(.*\.framework)/", install.lstrip("/"))
        return bool(m) and os.path.isdir(os.path.join(self.root, m.group(1)))

def is_system(name):
    return name.startswith(("/usr/lib/", "/System/Library/"))

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+", help=".app bundles or directories to scan")
    ap.add_argument("--sdk", required=True, help="path to MacOSX10.13.sdk")
    ap.add_argument("--max-files", type=int, default=6,
                    help="files to list per missing symbol (default 6)")
    a = ap.parse_args()

    if not os.path.isdir(os.path.join(a.sdk, "usr", "lib")):
        sys.exit("ERROR: %s does not look like an SDK (no usr/lib)" % a.sdk)
    sdk = Sdk(a.sdk)

    libsystem = sdk.exports("/usr/lib/libSystem.B.dylib")
    if not libsystem or len(libsystem) < 2000:
        sys.exit("ERROR: could not read libSystem's symbols from the SDK (got %s). "
                 "The SDK layout is not what this tool expects; refusing to "
                 "report a result." % (len(libsystem) if libsystem else "none"))
    print("SDK ok: libSystem exports %d symbols (incl. re-exports)" % len(libsystem))

    missing_sym = collections.defaultdict(set)
    missing_lib = collections.defaultdict(set)
    scanned = 0
    for root in a.paths:
        for path, _info in bundle_machos(root):
            r = read_macho(path)
            if r is None:
                continue
            scanned += 1
            deps, undefined = r
            rel = os.path.relpath(path, root)
            checked_libs = set()
            for name, ordinal, weak_ref in undefined:
                if ordinal in (0, 0xFE, 0xFF) or ordinal > len(deps):
                    continue
                cmd, lib = deps[ordinal - 1]
                if not is_system(lib):
                    continue
                exports = sdk.exports(lib)
                if exports is None:
                    if lib not in checked_libs:
                        checked_libs.add(lib)
                        gone = lib.startswith("/System/Library/Frameworks/") \
                            and not sdk.framework_exists(lib)
                        fatal = gone and cmd != LC_LOAD_WEAK
                        missing_lib[(lib, fatal, gone)].add(rel)
                    continue
                if name not in exports and not weak_ref:
                    missing_sym[(name, lib)].add(rel)

    print("Scanned %d x86_64 Mach-O files." % scanned)
    problems = 0
    for (lib, fatal, gone), files in sorted(missing_lib.items()):
        if fatal:
            problems += 1
            print("ERROR: %s does not exist in this SDK, so it is not on that macOS "
                  "(used by %s)" % (lib, ", ".join(sorted(files)[:a.max_files])))
        else:
            print("warning: could not verify %s (no .tbd found; %d files use it)"
                  % (lib, len(files)))
    for (name, lib), files in sorted(missing_sym.items(), key=lambda kv: -len(kv[1])):
        problems += 1
        shown = ", ".join(sorted(os.path.basename(f) for f in files)[:a.max_files])
        more = " ..." if len(files) > a.max_files else ""
        print("ERROR: %s is not in %s of this SDK  (%d files: %s%s)"
              % (name, os.path.basename(lib), len(files), shown, more))
    if sdk.unresolved_reexports:
        print("note: %d re-exported libraries had no .tbd and were skipped"
              % len(sdk.unresolved_reexports))
    if problems:
        print("\nFAILED: %d problem(s). These would be 'Symbol not found' (or a missing "
              "library) on the older macOS." % problems)
        sys.exit(1)
    print("OK: every non-weak system symbol is present in the SDK.")

if __name__ == "__main__":
    main()
