"""Find lea/add with a given 32-bit displacement, grouped by containing function.

Usage: python -I disp.py <exe> hex,hex...
"""

import bisect
import collections
import re
import sys

import pefile  # ty: ignore[unresolved-import] - pefile is not in the ty environment

pe = pefile.PE(sys.argv[1])
pe.parse_data_directories()
t = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
data = t.get_data()
base = t.VirtualAddress
ents = sorted((e.struct.BeginAddress, e.struct.EndAddress) for e in pe.DIRECTORY_ENTRY_EXCEPTION)
begins = [b for b, _ in ents]
for text in sys.argv[2].split(","):
    v = int(text, 16)
    le = v.to_bytes(4, "little")
    fn = collections.Counter()
    # lea r64,[r+disp32]: 48/49/4c/4d 8d modrm(10xxxxxx) disp ; add rcx,imm32: 48 81 c1 imm
    for m in re.finditer(
        rb"[\x48\x49\x4c\x4d]\x8d[\x80-\xbf]"
        + re.escape(le)
        + rb"|\x48\x81[\xc0-\xc7]"
        + re.escape(le),
        data,
    ):
        a = base + m.start()
        i = bisect.bisect_right(begins, a) - 1
        fn[hex(ents[i][0])] += 1
    print(hex(v), len(fn), list(fn.items())[:14])
