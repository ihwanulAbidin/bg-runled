#!/usr/bin/env python3
"""Konversi font BDF (dipakai CircuitPython) ke header GFXfont (Adafruit GFX, C++).

Pemakaian:
    python3 tools/bdf2gfx.py fonts-src/helvB22.bdf src/fonts/helvB22.h helvB22

Hanya karakter ASCII 0x20..0x7E yang diambil (karakter lain ditampilkan '?' oleh firmware).
"""
import sys

FIRST, LAST = 0x20, 0x7E


def parse_bdf(path):
    with open(path, encoding="latin-1") as f:
        lines = [ln.rstrip("\r\n") for ln in f]

    props, notices, glyphs = {}, [], {}
    i = 0
    while i < len(lines):
        parts = lines[i].split()
        if not parts:
            i += 1
            continue
        key = parts[0]
        # Seluruh COMMENT + COPYRIGHT ikut disalin: lisensi Adobe/DEC mewajibkan teks izinnya disertakan.
        if key == "COMMENT":
            notices.append(lines[i][len("COMMENT"):].strip())
        elif key == "COPYRIGHT":
            notices.append(lines[i].split(None, 1)[1].strip('"'))
        elif key in ("FONT", "PIXEL_SIZE", "FONT_ASCENT", "FONT_DESCENT"):
            props[key] = lines[i].split(None, 1)[1]
        elif key == "STARTCHAR":
            g = {}
            i += 1
            while not lines[i].startswith("BITMAP"):
                p = lines[i].split()
                if p and p[0] == "ENCODING":
                    g["enc"] = int(p[1])
                elif p and p[0] == "DWIDTH":
                    g["adv"] = int(p[1])
                elif p and p[0] == "BBX":
                    g["w"], g["h"], g["xo"], g["yo"] = map(int, p[1:5])
                i += 1
            rows = []
            i += 1
            while not lines[i].startswith("ENDCHAR"):
                rows.append(lines[i].strip())
                i += 1
            g["rows"] = rows
            if FIRST <= g.get("enc", -1) <= LAST:
                glyphs[g["enc"]] = g
        i += 1
    return props, notices, glyphs


def pack(g):
    """Bit glyph disusun berurutan baris demi baris, MSB dulu, tanpa padding per baris."""
    bits = []
    for row in g["rows"][: g["h"]]:
        val = int(row, 16) if row else 0
        nbits = len(row) * 4
        for x in range(g["w"]):
            bits.append((val >> (nbits - 1 - x)) & 1)
    out = []
    for k in range(0, len(bits), 8):
        chunk = bits[k : k + 8] + [0] * (8 - len(bits[k : k + 8]))
        out.append(sum(b << (7 - j) for j, b in enumerate(chunk)))
    return out


def main(src, dst, name):
    props, notices, glyphs = parse_bdf(src)
    missing = [c for c in range(FIRST, LAST + 1) if c not in glyphs]
    if missing:
        sys.exit("glyph hilang di {}: {}".format(src, missing))

    bitmap, table = [], []
    for c in range(FIRST, LAST + 1):
        g = glyphs[c]
        data = pack(g) if g["w"] and g["h"] else []
        # GFX: yOffset = jarak dari baseline ke baris teratas glyph (negatif = di atas baseline)
        table.append((len(bitmap), g["w"], g["h"], g["adv"], g["xo"], -(g["yo"] + g["h"]), c))
        bitmap.extend(data)

    if len(bitmap) > 0xFFFF:
        sys.exit("bitmap terlalu besar untuk GFXglyph.bitmapOffset (uint16)")
    for off, w, h, adv, xo, yo, c in table:
        if not (w < 256 and h < 256 and adv < 256 and -128 <= xo < 128 and -128 <= yo < 128):
            sys.exit("glyph 0x{:02X} di luar batas GFXglyph".format(c))

    y_adv = int(props["FONT_ASCENT"]) + int(props["FONT_DESCENT"])
    guard = "FONT_{}_H".format(name.upper())
    with open(dst, "w") as f:
        f.write("// Dibuat otomatis oleh tools/bdf2gfx.py dari {} — jangan diedit manual.\n".format(src.split("/")[-1]))
        f.write("// Sumber: {}\n".format(props.get("FONT", "?")))
        for n in notices:
            f.write("// {}\n".format(n).replace("// \n", "//\n"))
        f.write("// Karakter: ASCII 0x{:02X}..0x{:02X}\n".format(FIRST, LAST))
        f.write("#ifndef {0}\n#define {0}\n\n#include <gfxfont.h>\n\n#ifndef PROGMEM\n#define PROGMEM\n#endif\n\n".format(guard))
        f.write("const uint8_t {}Bitmaps[] PROGMEM = {{\n".format(name))
        for k in range(0, len(bitmap), 16):
            f.write("  " + ", ".join("0x{:02X}".format(b) for b in bitmap[k : k + 16]) + ",\n")
        f.write("};\n\n")
        f.write("const GFXglyph {}Glyphs[] PROGMEM = {{\n".format(name))
        for off, w, h, adv, xo, yo, c in table:
            ch = chr(c) if chr(c) not in "\\" else "\\\\"
            f.write("  {{ {:5d}, {:3d}, {:3d}, {:3d}, {:4d}, {:4d} }},  // 0x{:02X} '{}'\n".format(off, w, h, adv, xo, yo, c, ch))
        f.write("};\n\n")
        f.write("const GFXfont {0} PROGMEM = {{ (uint8_t *){0}Bitmaps, (GFXglyph *){0}Glyphs, 0x{1:02X}, 0x{2:02X}, {3} }};\n\n".format(name, FIRST, LAST, y_adv))
        f.write("#endif\n")

    m, gch = glyphs[ord("M")], glyphs[ord("g")]
    asc = max(-(t[5]) for t in table)
    desc = max(t[5] + t[2] for t in table)
    print("{:8s} glyph={} bitmap={}B  kapital(M)={}px  descender(g)={}px  maks atas={}px  maks bawah={}px".format(
        name, len(table), len(bitmap), m["h"] + m["yo"], -gch["yo"], asc, desc))


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:])
