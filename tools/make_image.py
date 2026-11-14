#!/usr/bin/env python3
"""make_image.py DUMP.bin PACKAGE.fwsc OUT.bin

A full 1 MiB flash image for `esp32_recover.py restore --image OUT.bin`: the unit's own dump, with only its
application region (0x4000..0x92FFF) taken from PACKAGE. For a package whose identity the recovery script does not
know (Arcturus S6 is FM-1_67686970): the head and the device data stay the unit's own, byte for byte, so the
script's rules still hold (the head must match the unit's; only the application region is ever written).
Uses MvaveFM1Unbricker's own package reader (third_party/MvaveFM1Unbricker/fm1_unbrick.py)."""
import hashlib
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "third_party" / "MvaveFM1Unbricker"))
import fm1_unbrick as fu  # noqa: E402

APP_START, APP_END, SIZE = 0x4000, 0x93000, 0x100000


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    dump = Path(sys.argv[1]).read_bytes()
    if len(dump) != SIZE:
        print("the dump must be exactly 1 MiB (0x100000 bytes); it is 0x%X" % len(dump))
        return 1
    identity, image = fu.parse_fwsc(Path(sys.argv[2]).read_bytes())
    if len(image) < APP_END:
        print("the package's flash image ends at 0x%X, before 0x%X: not a complete FM-1 package" % (len(image), APP_END))
        return 1
    out = dump[:APP_START] + bytes(image[APP_START:APP_END]) + dump[APP_END:]
    assert len(out) == SIZE
    Path(sys.argv[3]).write_bytes(out)
    differ = sum(1 for a in range(APP_START, APP_END, 0x1000) if dump[a:a + 0x1000] != out[a:a + 0x1000])
    print("package   %s (%s)" % (sys.argv[2], identity))
    print("image     %s: the dump's head and data, the package's application region" % sys.argv[3])
    print("sectors   %d of %d in the application region differ from the dump" % (differ, (APP_END - APP_START) // 0x1000))
    print("sha256    %s" % hashlib.sha256(out).hexdigest())
    return 0


if __name__ == "__main__":
    sys.exit(main())
