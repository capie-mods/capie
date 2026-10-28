"""MSVC x64 RTTI: class name -> vtable RVAs. Usage: python -I rtti.py <exe> Name [Name...]"""
import struct, sys, pefile
pe = pefile.PE(sys.argv[1], fast_load=True)
img = pe.get_memory_mapped_image(); IB = pe.OPTIONAL_HEADER.ImageBase
def all_find(b, start=0):
    i = img.find(b, start)
    while i != -1:
        yield i; i = img.find(b, i + 1)
for name in sys.argv[2:]:
    mangled = b".?AV" + name.encode() + b"@@\0"
    for s in all_find(mangled):
        td = s - 0x10
        cols = [c - 0xC for c in all_find(struct.pack("<I", td)) if struct.unpack_from("<I", img, c - 0xC)[0] == 1]
        vts = []
        for col in cols:
            off = struct.unpack_from("<I", img, col + 4)[0]
            for p in all_find(struct.pack("<Q", IB + col)):
                vts.append((hex(p + 8), hex(off)))
        print(name, "td", hex(td), "vtables(rva,offset)", vts)
