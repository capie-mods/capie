"""Tiny Mach-O helper for the HOI4 macOS binary (classic dyld info: pointers are unslid VAs)."""
import bisect
import re
import struct
import subprocess
import sys
from functools import lru_cache
from pathlib import Path

HERE = Path(__file__).resolve().parent
BIN = HERE / "hoi4"
DATA = BIN.read_bytes()
OD = "/Library/Developer/CommandLineTools/usr/bin/llvm-objdump"


def _segments():
    ncmds = struct.unpack_from("<I", DATA, 16)[0]
    off = 32
    segs, sects = [], {}
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", DATA, off)
        if cmd == 0x19:
            name = DATA[off + 8 : off + 24].rstrip(b"\0").decode()
            vm, vms, fo, fs = struct.unpack_from("<QQQQ", DATA, off + 24)
            segs.append((vm, vms, fo, fs, name))
            nsects = struct.unpack_from("<I", DATA, off + 64)[0]
            for i in range(nsects):
                s = off + 72 + i * 80
                sn = DATA[s : s + 16].rstrip(b"\0").decode()
                addr, sz, so = struct.unpack_from("<QQI", DATA, s + 32)
                sects[(name, sn)] = (addr, sz, so)
        off += size
    return segs, sects


SEGS, SECTS = _segments()


def fo(va):
    for vm, vms, f, fs, _ in SEGS:
        if vm <= va < vm + fs:
            return f + va - vm
    raise ValueError(hex(va))


def q(va):
    return struct.unpack_from("<Q", DATA, fo(va))[0]


def d(va):
    return struct.unpack_from("<I", DATA, fo(va))[0]


def raw(va, n):
    return DATA[fo(va) : fo(va) + n]


@lru_cache(None)
def symbols():
    out = subprocess.run(["nm", "-n", str(BIN)], capture_output=True, text=True).stdout
    by_name, by_addr = {}, {}
    for line in out.splitlines():
        p = line.split(" ", 2)
        if len(p) == 3 and p[0].strip():
            a = int(p[0], 16)
            by_name[p[2]] = a
            by_addr.setdefault(a, p[2])
    return by_name, by_addr


@lru_cache(None)
def demangled():
    by_name, _ = symbols()
    names = list(by_name)
    out = subprocess.run(["c++filt"], input="\n".join(names), capture_output=True, text=True).stdout.splitlines()
    return dict(zip(names, out))


@lru_cache(None)
def func_starts():
    out = subprocess.run([OD, "--macho", "--function-starts", str(BIN)], capture_output=True, text=True).stdout
    return sorted(int(x, 16) for x in out.split() if re.fullmatch(r"[0-9a-f]{16}", x))


def func_of(va):
    fs = func_starts()
    i = bisect.bisect_right(fs, va) - 1
    return fs[i] if i >= 0 else None


def func_end(start):
    fs = func_starts()
    i = bisect.bisect_right(fs, start)
    return fs[i] if i < len(fs) else start + 0x1000


def name_of(va):
    _, by_addr = symbols()
    n = by_addr.get(va)
    return demangled().get(n, n) if n else None


@lru_cache(None)
def _dconst():
    a, sz, so = SECTS[("__DATA", "__const")]
    return a, DATA[so : so + sz]


def find_pointers(target, section=("__DATA", "__const")):
    a, sz, so = SECTS[section]
    blob = DATA[so : so + sz]
    needle = struct.pack("<Q", target)
    res, i = [], blob.find(needle)
    while i >= 0:
        if i % 8 == 0:
            res.append(a + i)
        i = blob.find(needle, i + 1)
    return res


def typeinfo(cls):
    """Typeinfo VA of a class; `cls` is a plain name or an Itanium-mangled type name (e.g. N4NFoo4CBarE)."""
    mangled = cls if cls[0].isdigit() or cls.startswith("N") and cls.endswith("E") else f"{len(cls)}{cls}"
    by_name, _ = symbols()
    if f"__ZTI{mangled}" in by_name:
        return by_name[f"__ZTI{mangled}"]
    a, sz, so = SECTS[("__TEXT", "__const")]
    blob = DATA[so : so + sz]
    i = blob.find(b"\0" + mangled.encode() + b"\0")
    while i >= 0:
        for p in find_pointers(a + i + 1):
            return p - 8
        i = blob.find(b"\0" + mangled.encode() + b"\0", i + 1)
    return None


def vtables(cls_or_ti):
    """Yield (vtable_va_of_first_slot, offset_to_top) for every vtable naming this typeinfo."""
    ti = cls_or_ti if isinstance(cls_or_ti, int) else typeinfo(cls_or_ti)
    if ti is None:
        return []
    res = []
    for p in find_pointers(ti):
        top = struct.unpack("<q", raw(p - 8, 8))[0]
        if -0x100000 < top <= 0:
            res.append((p + 8, top))
    return res


def is_code(va):
    a, sz, _ = SECTS[("__TEXT", "__text")]
    return a <= va < a + sz


def slots(vt, maxn=200):
    out = []
    for i in range(maxn):
        v = q(vt + 8 * i)
        if not is_code(v) and v != 0:
            break
        out.append(v)
    return out


def dis(start, end=None, raw_bytes=False):
    end = end or func_end(start)
    args = [OD, "--macho", "-d", "--x86-asm-syntax=intel", f"--start-address={start:#x}", f"--stop-address={end:#x}", str(BIN)]
    if not raw_bytes:
        args.insert(3, "--no-show-raw-insn")
    return subprocess.run(args, capture_output=True, text=True).stdout


def cstrings(pattern):
    a, sz, so = SECTS[("__TEXT", "__cstring")]
    blob = DATA[so : so + sz]
    rx = re.compile(pattern.encode())
    res = []
    for m in rx.finditer(blob):
        s = blob.rfind(b"\0", 0, m.start()) + 1
        e = blob.find(b"\0", m.end())
        res.append((a + s, blob[s:e].decode("latin1")))
    return sorted(set(res))


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "vt":
        for vt, top in vtables(sys.argv[2]):
            sl = slots(vt)
            print(f"vtable {vt:#x} top={top} slots={len(sl)}")
            for i, s in enumerate(sl):
                print(f"  +{8*i:#05x} {s:#x} {name_of(s) or ''}")
    elif cmd == "dis":
        print(dis(int(sys.argv[2], 16), int(sys.argv[3], 16) if len(sys.argv) > 3 else None, len(sys.argv) > 4))
    elif cmd == "str":
        for a, s in cstrings(sys.argv[2]):
            print(f"{a:#x} {s!r}")


# ---- fast disassembly from the full dump (dis.txt) -------------------------------------------
_IDX = None


def _index():
    global _IDX
    if _IDX is None:
        idx_path = HERE / "dis.idx"
        if not idx_path.exists():
            out, pos = [], 0
            with open(HERE / "dis.txt", "rb") as f:
                for n, line in enumerate(f):
                    if n % 512 == 0 and line[:1] in b"123456789abcdef" and b":" in line[:12]:
                        out.append(f"{int(line.split(b':')[0], 16)} {pos}\n")
                    pos += len(line)
            idx_path.write_text("".join(out))
        pairs = [tuple(map(int, l.split())) for l in idx_path.read_text().splitlines()]
        _IDX = ([p[0] for p in pairs], [p[1] for p in pairs])
    return _IDX


def fdis(start, end=None, annotate=True):
    end = end or func_end(start)
    addrs, offs = _index()
    i = max(bisect.bisect_right(addrs, start) - 1, 0)
    res = []
    with open(HERE / "dis.txt", "rb") as f:
        f.seek(offs[i])
        for line in f:
            s = line.decode("latin1").rstrip("\n")
            if ":" not in s[:12]:
                continue
            try:
                a = int(s.split(":")[0], 16)
            except ValueError:
                continue
            if a < start:
                continue
            if a >= end:
                break
            if annotate:
                s = _annotate(s)
            res.append(s)
    return "\n".join(res)


_RIP = re.compile(r"\[rip \+ (0x[0-9a-f]+)\]|\[rip - (0x[0-9a-f]+)\]")
_TGT = re.compile(r"\b(call|jmp)\s+(0x[0-9a-f]+)")


def _annotate(s):
    notes = []
    m = _TGT.search(s)
    if m:
        t = int(m.group(2), 16)
        n = name_of(t)
        if n:
            notes.append(n[:120])
    if "## " in s:
        try:
            t = int(s.split("## ")[1].split()[0], 16)
            n = name_of(t)
            if n:
                notes.append(n[:120])
            a, sz, _ = SECTS[("__TEXT", "__cstring")]
            if a <= t < a + sz:
                st = DATA[fo(t) : DATA.find(b"\0", fo(t))][:80]
                notes.append(repr(st.decode("latin1")))
        except Exception:
            pass
    return s + ("   ; " + " | ".join(notes) if notes else "")
