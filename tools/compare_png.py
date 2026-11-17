#!/usr/bin/env python3
"""Compare deux images PNG (8 bits, RGB ou RGBA, non entrelacées) : outil de référence d'images (TOOL-03, RHI-14).

    compare_png.py REFERENCE CANDIDAT [--tolerance T] [--max-diff-pixels N] [--max-diff-percent P] [--diff-out FICHIER]

- T : écart maximal toléré par canal (0-255, défaut 0) ; un pixel « diffère » si un canal dépasse T.
- Le script sort avec 1 si les dimensions diffèrent ou si le nombre (ou le pourcentage) de pixels qui diffèrent
  dépasse la limite (défaut : 0 pixel). --diff-out écrit une image : noir = identique, rouge = écart (amplifié).
Aucune dépendance (zlib de la bibliothèque standard).
"""
import argparse, struct, sys, zlib


def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: pas un PNG")
    pos, idat, ihdr = 8, b"", None
    while pos < len(data):
        n, = struct.unpack(">I", data[pos:pos + 4]); kind = data[pos + 4:pos + 8]; body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR": ihdr = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT": idat += body
        pos += 12 + n
    w, h, depth, ctype, _, _, interlace = ihdr
    if depth != 8 or ctype not in (2, 6) or interlace != 0:
        raise ValueError(f"{path}: seuls les PNG 8 bits RGB/RGBA non entrelacés sont pris en charge")
    bpp = 3 if ctype == 2 else 4
    raw = zlib.decompress(idat); stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]; line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if f == 1:
            for i in range(bpp, stride): line[i] = (line[i] + line[i - bpp]) & 255
        elif f == 2:
            for i in range(stride): line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride): line[i] = (line[i] + (((line[i - bpp] if i >= bpp else 0) + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0; b = prev[i]; c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        rows.append(line); prev = line
    return w, h, bpp, rows


def write_png(path, w, h, rows):
    def chunk(k, b): c = struct.pack(">I", len(b)) + k + b; return c + struct.pack(">I", zlib.crc32(k + b) & 0xffffffff)
    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("reference"); ap.add_argument("candidate")
    ap.add_argument("--tolerance", type=int, default=0)
    ap.add_argument("--max-diff-pixels", type=int, default=0)
    ap.add_argument("--max-diff-percent", type=float)
    ap.add_argument("--diff-out")
    a = ap.parse_args()
    w1, h1, b1, r1 = read_png(a.reference); w2, h2, b2, r2 = read_png(a.candidate)
    if (w1, h1) != (w2, h2):
        print(f"DIFFERENT: dimensions {w1}x{h1} vs {w2}x{h2}"); return 1
    diff = 0; maxd = 0; total = 0; out = []
    for y in range(h1):
        ra, rb = r1[y], r2[y]; line = bytearray(w1 * 3)
        for x in range(w1):
            d = 0
            for c in range(3):
                v = abs(ra[x * b1 + c] - rb[x * b2 + c]); d = v if v > d else d; total += v
            if d > a.tolerance: diff += 1
            if d > maxd: maxd = d
            if a.diff_out and d: line[x * 3] = min(255, 64 + d * 4)
        out.append(line)
    pct = 100.0 * diff / (w1 * h1)
    limit_ok = diff <= a.max_diff_pixels if a.max_diff_percent is None else pct <= a.max_diff_percent
    print(f"{'OK' if limit_ok else 'DIFFERENT'}: {w1}x{h1}, {diff} pixel(s) au-delà de la tolérance {a.tolerance} ({pct:.4f} %), "
          f"écart max {maxd}/255, écart moyen {total / (w1 * h1 * 3):.4f}")
    if a.diff_out:
        write_png(a.diff_out, w1, h1, out)
    return 0 if limit_ok else 1


if __name__ == "__main__":
    sys.exit(main())
