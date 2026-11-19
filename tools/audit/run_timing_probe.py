"""Build and run the TP HD timing probe from a local USA Rev 2 recompile on Windows."""
import argparse
import hashlib
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from rpx import Rpx

REV2_SHA256 = "28922198517578c9261ac0b0b04a586252033d3bbcf7980b79e37638775b12a9"


def leaf(source, name):
    match = re.search(rf"^void {name}\(Cpu\*[^\n]+\n.*?^}}", source, re.M | re.S)
    if not match:
        raise ValueError(f"Missing generated leaf {name}")
    return re.sub(r"^    MUSTTAIL return \w+\(c\);\n(?=})", "", match[0], flags=re.M)


def instructions(source, first, last):
    # Read only the addressed instructions. Reject gaps and similar but nonmatching builds.
    rows = {}
    for line in source.splitlines():
        match = re.search(r"/\* ([0-9A-F]{8}): [0-9A-F]{8} \*/$", line)
        if match and first <= int(match[1], 16) <= last:
            address = int(match[1], 16)
            if address in rows:
                raise ValueError(f"Ambiguous instruction {address:08X}")
            rows[address] = line
    return "\n".join(rows[address] for address in range(first, last + 1, 4))


def camera_clock_block(source, first, last, increments):
    body = instructions(source, first, last)
    for address in increments:
        pattern = rf"(c->r\[\d+\] = c->r\[\d+\] \+) 0x00000001u(; /\* {address:08X}:)"
        body, count = re.subn(pattern, r"\1 clock_step\2", body)
        if count != 1:
            raise ValueError(f"Missing expected camera counter increment at {address:08X}")
    return f"static void camera_clock_{first:08X}(Cpu* c, uint32_t clock_step) {{\n{body}\n}}\n"


def data_words(rpx, address, count):
    for section in rpx.sections:
        offset = address - section.addr
        if 0 <= offset <= len(section.data) - count * 4:
            return struct.unpack_from(f">{count}I", section.data, offset)
    raise ValueError(f"Data unavailable at {address:08X}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gen", required=True, type=Path, help="Existing TP HD generated C directory")
    parser.add_argument("--rpx", required=True, type=Path, help="Local USA Rev 2 Zelda.rpx")
    parser.add_argument("--compiler", default="clang++", help="LLVM MinGW C++ compiler")
    args = parser.parse_args()

    if hashlib.sha256(args.rpx.read_bytes()).hexdigest() != REV2_SHA256:
        parser.error("The timing probe expects the USA Rev 2 Zelda.rpx")
    rpx = Rpx(args.rpx)
    output = ROOT / "build" / "timing-probe"
    output.mkdir(parents=True, exist_ok=True)

    def generated(file):
        return (args.gen / file).read_text(encoding="utf-8")

    movement = generated("code_001.c")
    speed_add = instructions(movement, 0x0203244C, 0x02032458)
    speed_clamp = instructions(movement, 0x020324B0, 0x020324B0)
    camera = generated("code_076.c")
    camera_setup = instructions(camera, 0x028D8B5C, 0x028D8B7C)
    camera_update = instructions(camera, 0x028D8B80, 0x028D8B8C)
    camera_execute = generated("code_077.c")
    camera_clocks = "".join(
        camera_clock_block(camera_execute, first, last, increments)
        for first, last, increments in (
            (0x028DFD04, 0x028DFD28, (0x028DFD0C, 0x028DFD14, 0x028DFD1C)),
            (0x028DFDBC, 0x028DFDE0, (0x028DFDC4, 0x028DFDCC, 0x028DFDD4)),
            (0x028DFDE4, 0x028DFDFC, (0x028DFDEC, 0x028DFDF4)),
            (0x028DFD64, 0x028DFD84, (0x028DFD70, 0x028DFD78)),
        )
    )
    camera_coeff, = data_words(rpx, 0x100897C4, 1)
    anim_words = data_words(rpx, 0x100ABBA8, 4)

    include = output / "guest_timing.inc"
    include.write_text(
        "// Generated privately from the supplied recompile.\n"
        + leaf(generated("code_103.c"), "f_02BEFF08") + "\n"
        + leaf(generated("code_106.c"), "f_02C379DC") + "\n"
        + leaf(generated("code_098.c"), "f_02B520C4_orig") + "\n"
        + "static void advance_speed(Cpu* c) {\n" + speed_add
        + "\n    return;\nL_020324B0: ;\n" + speed_clamp + "\n}\n"
        + "static void filter_camera_distance(Cpu* c, bool half_step) {\n" + camera_setup
        + "\n    if (half_step) c->f[0].ps0 = half_gain(c->f[0].ps0);\n"
        + camera_update + "\n}\n"
        + camera_clocks
        + f"constexpr uint32_t kCameraGainWord = 0x{camera_coeff:08X}u;\n"
        + "constexpr uint32_t kFrameControllerWords[] = {"
        + ", ".join(f"0x{word:08X}u" for word in anim_words) + "};\n",
        encoding="utf-8",
    )

    exe = output / "timing_probe.exe"
    subprocess.run(
        [args.compiler, "-std=c++20", "-O2", "-ffp-contract=off",
         "-I", str(ROOT / "runtime/include"), "-I", str(output),
         str(ROOT / "runtime/tools/timing_probe.cpp"), "-o", str(exe)],
        check=True,
    )
    subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
