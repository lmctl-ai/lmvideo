#!/usr/bin/env python3
"""Tiny PNG probe for the tests (stdlib only; run with `python3 -I`).
   pngpx.py FILE            -> prints "WIDTHxHEIGHT"
   pngpx.py FILE X Y [X Y]  -> prints "r g b a" for each pixel
   pngpx.py FILE --sha256   -> sha256 of the decoded RGBA pixels (container-independent)
Supports non-interlaced 8-bit RGBA/RGB PNGs (what Skia and ffmpeg write here)."""
import hashlib, struct, sys, zlib

def decode(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat, ihdr = 8, b"", None
    while pos < len(data):
        n, tag = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if tag == b"IHDR": ihdr = struct.unpack(">IIBBBBB", body)
        elif tag == b"IDAT": idat += body
        pos += 12 + n
    w, h, depth, ctype, _, _, interlace = ihdr
    assert depth == 8 and interlace == 0 and ctype in (2, 6), "unsupported PNG variant %r" % (ihdr,)
    bpp = 4 if ctype == 6 else 3
    raw, stride, rows, prev = zlib.decompress(idat), w * bpp, [], bytearray(w * bpp)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line)); prev = line
    if bpp == 3:  # widen to RGBA
        rows = [b"".join(r[i:i + 3] + b"\xff" for i in range(0, len(r), 3)) for r in rows]
    return w, h, rows

w, h, rows = decode(sys.argv[1])
if len(sys.argv) == 2: print("%dx%d" % (w, h))
elif sys.argv[2] == "--sha256": print(hashlib.sha256(b"".join(rows)).hexdigest())
else:
    v = list(map(int, sys.argv[2:]))
    for x, y in zip(v[0::2], v[1::2]): print(*rows[y][x * 4:x * 4 + 4])
