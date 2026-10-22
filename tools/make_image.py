#!/usr/bin/env python3
"""Assemble exactly 1,474,560 bytes floppy image, never access physical drives."""
import sys
from pathlib import Path
boot, kernel, target = (Path(x) for x in sys.argv[1:4])
bs, ks = boot.read_bytes(), kernel.read_bytes()
assert len(bs) == 512 and bs[510:512] == b'\x55\xaa', 'BIOS sector invalid'
assert 0 < len(ks) <= 60 * 1024, 'kernel exceeds low-memory loader capacity'
assert (len(ks) + 511)//512 <= 120
img = bytearray(1474560)
img[:512] = bs
img[512:512+len(ks)] = ks
target.write_bytes(img)
print(f'Built BIOS floppy image: {target} ({len(img)} bytes; kernel {len(ks)} bytes)')
