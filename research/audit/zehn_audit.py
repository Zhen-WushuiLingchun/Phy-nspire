"""Independent Zehn/.tns parser for auditing REFERENCE_CORPUS.md sections 3-4.

Deliberately does NOT reuse the draft's formulas. It locates the Zehn signature
by scanning, dumps the raw header words, and cross-checks every derived figure
against an invariant the draft did not use:
    container_bytes == zehn_offset + file_size
"""
import hashlib
import struct
import sys
import zlib

SIG = b"Zehn"


def parse(path):
    data = open(path, "rb").read()
    out = {"path": path, "container": len(data)}
    out["sha256"] = hashlib.sha256(data).hexdigest()

    # "Zehn" also occurs inside the PRG stub, so select the occurrence that
    # satisfies the span invariant rather than the first hit.
    cands, i = [], data.find(SIG)
    while i >= 0:
        cands.append(i)
        i = data.find(SIG, i + 1)
    if not cands:
        raise SystemExit("no Zehn signature in %s" % path)
    out["sig_candidates"] = cands
    real = [c for c in cands
            if len(data) - c >= 32
            and c + struct.unpack_from("<I", data, c + 8)[0] == len(data)]
    if not real:
        raise SystemExit("no Zehn candidate satisfies span invariant in %s: %r"
                         % (path, cands))
    off = real[-1]
    out["zehn_offset"] = off
    out["stub_magic"] = data[:4].hex(" ")

    # Raw header words, no interpretation yet.
    words = struct.unpack_from("<16I", data, off)
    out["raw_words"] = words

    # Field order established empirically from three binaries (see audit note):
    # signature, version, file_size, reloc_count, flag_count, extra_size,
    # alloc_size, entry_offset  (8 x u32 = 32 bytes)
    (sig, version, file_size, reloc_count,
     flag_count, extra_size, alloc_size, entry_offset) = words[:8]
    out.update(version=version, file_size=file_size, alloc_size=alloc_size,
               flag_count=flag_count, reloc_count=reloc_count,
               extra_size=extra_size, entry_offset=entry_offset)

    # INVARIANT the draft never checks: the Zehn image must span to EOF.
    out["invariant_span"] = (off + file_size == len(data))

    hdr = 32
    reloc_bytes = 4 * reloc_count
    flag_bytes = 4 * flag_count
    tables = hdr + reloc_bytes + flag_bytes + extra_size
    out.update(reloc_bytes=reloc_bytes, flag_bytes=flag_bytes, tables=tables)

    out["resident"] = alloc_size - tables
    out["zlib_buf"] = file_size - tables
    out["peak"] = out["resident"] + out["zlib_buf"] + reloc_bytes

    # Independent confirmation: payload must actually start with a zlib header
    # at exactly off+tables if the tables formula is right.
    pay = off + tables
    out["payload_first2"] = data[pay:pay + 2].hex(" ")
    out["payload_is_zlib"] = data[pay:pay + 1] == b"\x78"
    out["extra_region"] = data[off + hdr + reloc_bytes + flag_bytes:pay]

    # Decisive test: actually decompress. Confirms both the offset and that the
    # decompressed size equals `resident` (which the draft asserts but never
    # verifies by decompression).
    try:
        raw = zlib.decompress(data[pay:])
        out["inflate_ok"] = True
        out["inflate_size"] = len(raw)
        out["inflate_matches_resident"] = (len(raw) == out["resident"])
    except Exception as e:  # noqa: BLE001
        out["inflate_ok"] = False
        out["inflate_err"] = repr(e)
    return out


def report(r):
    print("=" * 72)
    print(r["path"])
    print("  container bytes      %d" % r["container"])
    print("  sha256               %s" % r["sha256"])
    print("  stub magic           %s" % r["stub_magic"])
    print("  zehn offset          %d" % r["zehn_offset"])
    print("  version              %d" % r["version"])
    print("  file_size            %d" % r["file_size"])
    print("  alloc_size           %d" % r["alloc_size"])
    print("  flag_count           %d" % r["flag_count"])
    print("  reloc_count          %d" % r["reloc_count"])
    print("  extra_size           %d" % r["extra_size"])
    print("  entry_offset         %d" % r["entry_offset"])
    print("  INVARIANT off+file_size==len : %s" % r["invariant_span"])
    print("  tables               %d" % r["tables"])
    print("  resident (alloc-tab) %d" % r["resident"])
    print("  zlib buf (file-tab)  %d" % r["zlib_buf"])
    print("  reloc bytes          %d" % r["reloc_bytes"])
    print("  peak                 %d" % r["peak"])
    print("  payload first bytes  %s  (zlib=%s)"
          % (r["payload_first2"], r["payload_is_zlib"]))
    print("  extra region bytes   %r" % (r["extra_region"][:80],))
    if r["inflate_ok"]:
        print("  INFLATE ok           %d bytes" % r["inflate_size"])
        print("  inflate==resident    %s" % r["inflate_matches_resident"])
        d = r["inflate_size"] - r["resident"]
        print("  delta (inflate-res)  %d" % d)
    else:
        print("  INFLATE FAILED       %s" % r["inflate_err"])


if __name__ == "__main__":
    results = [parse(p) for p in sys.argv[1:]]
    for r in results:
        report(r)
    print("=" * 72)
    tot_flash = sum(r["container"] for r in results)
    tot_res = sum(r["resident"] for r in results)
    tot_peak = sum(r["peak"] for r in results)
    print("SUM flash    %d" % tot_flash)
    print("SUM resident %d" % tot_res)
    print("SUM peak     %d" % tot_peak)
    print("expansion    %.4f" % (tot_res / tot_flash))
    print("6MiB - flash %d" % (6 * 1024 * 1024 - tot_flash))
