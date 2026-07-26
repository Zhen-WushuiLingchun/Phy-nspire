"""Parse Zehn flag + reloc tables to audit REFERENCE_CORPUS.md section 3."""
import struct
import sys
from collections import Counter

FLAG = {0: "NDLESS_VERSION_MIN", 1: "NDLESS_VERSION_MAX", 2: "NDLESS_REVISION_MIN",
        3: "NDLESS_REVISION_MAX", 4: "RUNS_ON_COLOR", 5: "RUNS_ON_CLICKPAD",
        6: "RUNS_ON_TOUCHPAD", 7: "RUNS_ON_32MB", 8: "EXECUTABLE_NAME",
        9: "EXECUTABLE_AUTHOR", 10: "EXECUTABLE_VERSION", 11: "EXECUTABLE_NOTICE",
        12: "RUNS_ON_HWW", 13: "USES_LCD_BLIT"}
RELOC = {0: "ADD_BASE", 1: "ADD_BASE_GOT", 2: "SET_ZERO", 3: "FILE_COMPRESSED",
         4: "UNALIGNED_RELOC"}


def go(path):
    d = open(path, "rb").read()
    i = d.find(b"Zehn")
    off = None
    while i >= 0:
        if len(d) - i >= 32 and i + struct.unpack_from("<I", d, i + 8)[0] == len(d):
            off = i
        i = d.find(b"Zehn", i + 1)
    _, ver, fsz, nrel, nflag, extra, alloc, entry = struct.unpack_from("<8I", d, off)

    rel_off = off + 32
    flag_off = rel_off + 4 * nrel
    extra_off = flag_off + 4 * nflag
    extra_data = d[extra_off:extra_off + extra]

    print("=" * 68)
    print(path.split("/")[-1])

    # Relocs: packed type:8 + offset:24, little endian -> low byte is type
    types = Counter()
    for k in range(nrel):
        w = struct.unpack_from("<I", d, rel_off + 4 * k)[0]
        types[w & 0xFF] += 1
    first = struct.unpack_from("<I", d, rel_off)[0]
    print("  reloc_count      %d" % nrel)
    print("  reloc[0]         type=%s offset=%d"
          % (RELOC.get(first & 0xFF, first & 0xFF), first >> 8))
    print("  reloc type mix   %s"
          % {RELOC.get(t, t): c for t, c in sorted(types.items())})

    print("  flags (%d):" % nflag)
    for k in range(nflag):
        w = struct.unpack_from("<I", d, flag_off + 4 * k)[0]
        t, data = w & 0xFF, w >> 8
        name = FLAG.get(t, "UNKNOWN(%d)" % t)
        extra_note = ""
        if name in ("EXECUTABLE_NAME", "EXECUTABLE_AUTHOR", "EXECUTABLE_NOTICE"):
            s = extra_data[data:]
            s = s.split(b"\x00")[0]
            extra_note = "  -> %r" % s
        print("    %-22s = %d%s" % (name, data, extra_note))
    present = {FLAG.get(struct.unpack_from("<I", d, flag_off + 4 * k)[0] & 0xFF)
               for k in range(nflag)}
    for want in ("NDLESS_VERSION_MIN", "NDLESS_REVISION_MIN", "RUNS_ON_HWW",
                 "USES_LCD_BLIT"):
        print("    [%s] %s" % ("present" if want in present else "ABSENT ", want))


for p in sys.argv[1:]:
    go(p)
