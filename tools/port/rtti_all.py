"""Dump every MSVC x64 RTTI primary vtable: rva -> class name.

Usage: python -I rtti_all.py <exe> <out.tsv>
"""

import pathlib
import struct
import sys

import pefile  # ty: ignore[unresolved-import] - pefile is not in the ty environment

pe = pefile.PE(sys.argv[1], fast_load=True)
img = pe.get_memory_mapped_image()
IMAGE_BASE = pe.OPTIONAL_HEADER.ImageBase
"""Preferred load address of the executable; RTTI pointers are stored as absolute addresses."""
rd = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".rdata")
lo, hi = rd.VirtualAddress, rd.VirtualAddress + rd.Misc_VirtualSize
out = []
for p in range(lo, hi - 8, 8):
    q = struct.unpack_from("<Q", img, p)[0] - IMAGE_BASE
    if not (lo <= q < hi - 24):
        continue
    sig, off, cd, td = struct.unpack_from("<IIIi", img, q)
    if sig != 1 or not (0 < td < len(img) - 0x20):
        continue
    selfp = struct.unpack_from("<I", img, q + 0x14)[0]
    if selfp != q:
        continue
    name = img[td + 0x10 : td + 0x90].split(b"\0")[0]
    if not name.startswith(b".?A"):
        continue
    out.append(f"{p + 8:x}\t{off:x}\t{name.decode('latin1')}")
with pathlib.Path(sys.argv[2]).open("w") as out_file:
    out_file.write("\n".join(out))
print(len(out))
