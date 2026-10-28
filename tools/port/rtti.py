"""MSVC x64 RTTI: class name -> vtable RVAs.

Usage: python -I rtti.py <exe> Name [Name...]
"""

import struct
import sys
from collections.abc import Iterator
from typing import cast

import pefile  # ty: ignore[unresolved-import] - pefile is not in the ty environment

pe = pefile.PE(sys.argv[1], fast_load=True)
img = cast("bytes", pe.get_memory_mapped_image())
IMAGE_BASE = pe.OPTIONAL_HEADER.ImageBase
"""Preferred load address of the executable; RTTI pointers are stored as absolute addresses."""


def all_find(b: bytes, start: int = 0) -> Iterator[int]:
    """Yield the offset of every occurrence of a byte string in the image.

    Args:
        b: Byte string to search for.
        start: Offset in the image at which the search begins.

    Yields:
        Offsets of each occurrence, in ascending order.
    """
    i = img.find(b, start)
    while i != -1:
        yield i
        i = img.find(b, i + 1)


for name in sys.argv[2:]:
    mangled = b".?AV" + name.encode() + b"@@\0"
    for s in all_find(mangled):
        td = s - 0x10
        cols = [
            c - 0xC
            for c in all_find(struct.pack("<I", td))
            if struct.unpack_from("<I", img, c - 0xC)[0] == 1
        ]
        vts = []
        for col in cols:
            off = struct.unpack_from("<I", img, col + 4)[0]
            vts.extend(
                (hex(p + 8), hex(off)) for p in all_find(struct.pack("<Q", IMAGE_BASE + col))
            )
        print(name, "td", hex(td), "vtables(rva,offset)", vts)
