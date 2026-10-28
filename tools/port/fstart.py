"""Function start via .pdata for RVAs. Usage: python -I fstart.py <exe> rva,..."""
import sys, struct, bisect, pefile
pe = pefile.PE(sys.argv[1]); pe.parse_data_directories()
ents = sorted((e.struct.BeginAddress, e.struct.EndAddress) for e in pe.DIRECTORY_ENTRY_EXCEPTION)
begins = [b for b, _ in ents]
for a in sys.argv[2].split(","):
    a = int(a, 16); i = bisect.bisect_right(begins, a) - 1
    print(hex(a), "in", hex(ents[i][0]), "-", hex(ents[i][1]))
