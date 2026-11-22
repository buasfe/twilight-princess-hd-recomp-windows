#!/usr/bin/env python3
"""Release guard: fails if a release artifact contains anything derived from the game or any key.

usage: guard.py [--windows-setup] ARTIFACT.zip|DIR [...]

Release packages may contain only this project's runtime, tools and installer (plus third-party
licenses). This check rejects, by name and by content:
  - generated/recompiled game code (code_NNN.c, funcs.h with guest functions, table.c, imports.c,
    imports.json, report.txt from tools/recomp), and real-looking generated code inside any file;
  - game files: *.rpx, *.rpl, *.wux, *.wud, disc keys (*.key), tickets/TMDs (title.tik, title.tmd),
    saves (cking*.sav, *.sav), anything under a game/ code/ content/ meta/ tree;
  - shader caches and head starts (shaders.bin, headstart.bin, template.bin, testcache.bin, *.metallib);
  - in every text file: a 32-hex-digit string (the shape of a Wii U key; SHA-256 sums are 64 digits
    and git hashes 40, which do not match).
The Windows release's tools/python/ (the official embeddable Python) must hold exactly the files listed
in tools/release/python-windows-files.json, each with its listed SHA-256.
Exit status 1 lists every problem.
"""
import hashlib
import json
import os
import re
import sys
import zipfile

BAD_NAME = [
    (re.compile(r"(^|/)code_\d+\.c$"), "generated game code"),
    (re.compile(r"(^|/)(table|imports)\.c$"), "generated game code tables"),
    (re.compile(r"(^|/)imports\.json$"), "generated import table"),
    (re.compile(r"(^|/)funcs\.h$"), "generated function list"),
    (re.compile(r"\.(rpx|rpl|wux|wud|iso|wua|rvz|gci)$", re.I), "game executable or disc image"),
    (re.compile(r"\.key$", re.I), "key file"),
    (re.compile(r"(^|/)title\.(tik|tmd|cert)$", re.I), "disc ticket/metadata"),
    (re.compile(r"\.sav$", re.I), "save file"),
    (re.compile(r"(^|/)(shaders|headstart|template|testcache|mycache)\.bin$", re.I), "shader cache"),
    (re.compile(r"\.metallib$", re.I), "compiled shader cache"),
    (re.compile(r"(^|/)(game|content|meta)/"), "game file tree"),
    (re.compile(r"(^|/)build/gen"), "recompiler output"),
    (re.compile(r"(^|/)(lib)?gamecode\.(a|lib)$"), "compiled game code"),
    (re.compile(r"(^|/)common\.key$", re.I), "Wii U common key"),
]
TEXT_EXT = {".py", ".txt", ".md", ".json", ".sh", ".command", ".bat", ".ps1", ".h", ".hpp", ".c", ".cpp", ".inl",
            ".cfg", ".ini", ".xml", ".plist", ".toml", ".yml", ".yaml", ".rsp", ""}
KEYLIKE = re.compile(rb"(?<![0-9A-Fa-f])[0-9A-Fa-f]{32}(?![0-9A-Fa-f])")
# a function body as tools/recomp/recomp.py emits it (stubgen placeholders call ppc_unimplemented)
GEN_CODE = re.compile(rb"void f_[0-9A-F]{8}\(Cpu\* __restrict c\) \{\n")


PYTHON_FILES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "python-windows-files.json")
PYTHON_DIR = re.compile(r"(?:^|/)tools/python/(.+)$")


def check_python(name, data, seen, problems):
    """tools/python/...: only the pinned embeddable Python's own files, unmodified. Returns True when it was one."""
    m = PYTHON_DIR.search(name.replace("\\", "/"))
    if not m:
        return False
    with open(PYTHON_FILES) as f:
        expected = json.load(f)["files"]
    rel = m.group(1)
    if rel not in expected:
        problems.append("%s: not a file of the pinned embeddable Python" % name)
    elif hashlib.sha256(data).hexdigest() != expected[rel]:
        problems.append("%s: differs from the pinned embeddable Python" % name)
    seen.add(rel)
    return True


def check_entry(name, data, problems):
    n = name.replace("\\", "/")
    for rx, why in BAD_NAME:
        if rx.search(n):
            problems.append("%s: %s" % (name, why))
    ext = os.path.splitext(n)[1].lower()
    if ext in TEXT_EXT and b"\0" not in data[:8192]:
        for m in KEYLIKE.finditer(data):
            line = data.count(b"\n", 0, m.start()) + 1
            problems.append("%s:%d: 32-hex-digit string (key-like)" % (name, line))
    if GEN_CODE.search(data):
        problems.append("%s: contains recompiled game functions" % name)


WINDOWS_REQUIRED = {
    "Twilight Princess HD.exe", "tools/bin/wwhd-extract.exe", "tools/python/python.exe",
    "tools/installer/setup.py", "tools/installer/toolchains.json", "sdk/manifest.json",
    "portable.txt", "LICENSE", "SOURCE.txt", "CREDITS.md",
}
WINDOWS_BINARIES = {"Twilight Princess HD.exe", "tools/bin/wwhd-extract.exe", "sdk/runtime/SDL3.dll"}


def check_windows_layout(names, problems):
    manifests = [n for n in names if n == "sdk/manifest.json" or n.endswith("/sdk/manifest.json")]
    if len(manifests) != 1:
        problems.append("expected one Windows setup manifest")
        return
    prefix = manifests[0][:-len("sdk/manifest.json")]
    if prefix.count("/") > 1:
        problems.append("Windows package is nested too deeply")
    entries = {n[len(prefix):] for n in names if n.startswith(prefix)}
    for missing in sorted(WINDOWS_REQUIRED - entries):
        problems.append("missing Windows setup file: " + missing)
    for name in names:
        if not name.startswith(prefix):
            problems.append("file outside the Windows setup folder: " + name)
    for name in sorted(entries):
        if name.startswith(("data/", "work/", "save/")):
            problems.append("local setup data in the Windows package: " + name)
        ext = os.path.splitext(name)[1].lower()
        if name.startswith("tools/python/") or name in WINDOWS_BINARIES:
            continue
        if ext in {".exe", ".dll", ".pyd", ".sys", ".com", ".zip", ".7z", ".tar", ".gz", ".bin", ".dat", ".log", ".dump", ".pdb"}:
            problems.append("unexpected binary or archive in the Windows setup: " + name)
        if ext in {".a", ".lib", ".o", ".obj", ".res"}:
            if not re.fullmatch(r"sdk/(?:lib/[^/]+\.(?:a|lib)|obj/(?:runtime_src_[^/]+\.(?:obj|o)|generated_wwhd\.rc\.res|generated_build_info\.cpp\.obj))", name):
                problems.append("unexpected SDK binary: " + name)


def scan(path, windows_setup=False):
    problems, count, python = [], 0, set()
    names, folded = set(), set()

    def entry(name, data):
        name = name.replace("\\", "/")
        if windows_setup:
            if any(not part or part in (".", "..") or ":" in part or part.endswith((" ", "."))
                   for part in name.split("/")):
                problems.append("unsafe Windows package path: " + name)
            if name.casefold() in folded:
                problems.append("duplicate Windows package path: " + name)
            names.add(name)
            folded.add(name.casefold())
            if data.startswith(b"MZ") != name.lower().endswith((".exe", ".dll", ".pyd")):
                problems.append("unexpected Windows executable header or extension: " + name)
            if data.startswith((b"\x7fELF", b"WUX0", b"ZArchive")):
                problems.append("unexpected executable or game archive header: " + name)
        check_python(name, data, python, problems)
        check_entry(name, data, problems)

    if os.path.isdir(path):
        for dp, _, fns in os.walk(path):
            for fn in fns:
                full = os.path.join(dp, fn)
                with open(full, "rb") as f:
                    entry(os.path.relpath(full, path), f.read())
                count += 1
    else:
        with zipfile.ZipFile(path) as z:
            for info in z.infolist():
                if info.is_dir():
                    continue
                entry(info.filename, z.read(info))
                count += 1
    if windows_setup:
        check_windows_layout(names, problems)
    if python:
        with open(PYTHON_FILES) as f:
            missing = sorted(set(json.load(f)["files"]) - python)
        if missing:
            problems.append("tools/python/ lacks files of the pinned embeddable Python: " + ", ".join(missing))
    return problems, count


def main():
    paths = sys.argv[1:]
    windows_setup = bool(paths and paths[0] == "--windows-setup")
    if windows_setup:
        paths = paths[1:]
    if not paths:
        sys.exit(__doc__)
    bad = False
    for p in paths:
        problems, count = scan(p, windows_setup=windows_setup)
        if problems:
            bad = True
            print("REJECTED %s (%d files):" % (p, count))
            for pr in problems:
                print("  " + pr)
        else:
            print("ok %s (%d files checked)" % (p, count))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
