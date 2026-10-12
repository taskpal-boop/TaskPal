"""Convert the TaskPal TTF fonts into VLW smooth fonts for M5GFX.

VLW layout, as read by M5GFX VLWfont::loadFont (lgfx_fonts.inl), all big-endian int32:
  header  : glyphCount, version(11), fontSize, 0, ascent, descent
  glyphs  : per glyph, sorted by code point:
            unicode, height, width, xAdvance, dY (bitmap top above baseline), dX (left bearing), 0
  bitmaps : 8-bit alpha, width*height bytes per glyph, in glyph order

Outputs:
  assets/vlw/<name>.vlw        embedded in the firmware (board_build.embed_files), not LittleFS,
                               so a font update never wipes the user's data (D15)
  fonts.ini                    [fonts] embed = ... (pulled in by platformio.ini extra_configs)
  src/ui/font_ids.h            generated enum + embedded-symbol lookup; do not edit
  build/fonts_preview.png      every font decoded back from its VLW and drawn

Usage:  conda run -n taskpal python tools/gen_vlw.py
"""
import struct
from pathlib import Path

import freetype
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
FONT_DIR = ROOT / "assets" / "fonts"
OUT_DIR = ROOT / "assets" / "vlw"
OUT_INI = ROOT / "fonts.ini"
OUT_IDS = ROOT / "src" / "ui" / "font_ids.h"
PREVIEW = ROOT / "build" / "fonts_preview.png"

FREDOKA = FONT_DIR / "Fredoka[wdth,wght].ttf"
ATK_REG = FONT_DIR / "AtkinsonHyperlegible-Regular.ttf"
ATK_BOLD = FONT_DIR / "AtkinsonHyperlegible-Bold.ttf"

# (id, file, size px, variable weight or None, use). Sizes snapped per DESIGN_SPEC.md §2.
FONTS = [
    ("fredoka_68", FREDOKA, 68, 700, "You did it!"),
    ("fredoka_52", FREDOKA, 52, 600, "greeting"),
    ("fredoka_46", FREDOKA, 46, 600, "checklist title"),
    ("fredoka_44", FREDOKA, 44, 600, "greeting with a name (fit)"),
    ("fredoka_40", FREDOKA, 40, 600, "greeting with a name (fit)"),
    ("fredoka_38", FREDOKA, 38, 600, "greeting with a name (fit)"),
    ("fredoka_34", FREDOKA, 34, 600, "admin titles"),
    ("fredoka_30", FREDOKA, 30, 600, "sheet titles"),
    ("atkinson_42b", ATK_BOLD, 42, None, "task label (user choice, wraps to 2 lines)"),
    ("atkinson_38b", ATK_BOLD, 38, None, "list card name"),
    ("atkinson_34b", ATK_BOLD, 34, None, "PIN numpad"),
    ("atkinson_30b", ATK_BOLD, 30, None, "big buttons, sheet body"),
    ("atkinson_26b", ATK_BOLD, 26, None, "subtitles, inputs, keys, compact label"),
    ("atkinson_22b", ATK_BOLD, 22, None, "buttons, meta"),
    ("atkinson_20b", ATK_BOLD, 20, None, "labels, footers"),
    ("atkinson_18b", ATK_BOLD, 18, None, "section labels, counters (smallest size)"),
]

# Printable ASCII plus the few typographic characters the UI strings use.
CHARS = list(range(0x20, 0x7F)) + [0x00B7, 0x2013, 0x2019, 0x201C, 0x201D, 0x2026]

WGHT_TAG = b"wght"


def open_face(path, weight):
    face = freetype.Face(str(path))
    if weight is not None:
        axes = face.get_variation_info().axes
        coords = [weight if a.tag == "wght" else a.default for a in axes]
        face.set_var_design_coords(coords)
    return face


def build_vlw(face, size):
    face.set_pixel_sizes(0, size)
    glyphs = []
    for cp in CHARS:
        if face.get_char_index(cp) == 0:
            continue
        face.load_char(cp, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_LIGHT)
        g = face.glyph
        bm = g.bitmap
        w, h, pitch = bm.width, bm.rows, bm.pitch
        buf = bytes(bm.buffer)
        data = b"".join(buf[r * pitch:r * pitch + w] for r in range(h))
        adv = (g.advance.x + 32) >> 6
        glyphs.append((cp, h, w, adv, g.bitmap_top, g.bitmap_left, data))

    def top(cp):
        return next((gl[4] for gl in glyphs if gl[0] == cp), size)

    def bottom(cp):
        return next((gl[1] - gl[4] for gl in glyphs if gl[0] == cp), 0)

    ascent, descent = top(ord("d")), bottom(ord("p"))
    for cp, h, w, adv, dy, dx, _ in glyphs:
        if w > 255 or adv > 255 or not -128 <= dx <= 127:
            raise ValueError(f"glyph U+{cp:04X} too large for VLW uint8/int8 fields")

    out = bytearray(struct.pack(">6i", len(glyphs), 11, size, 0, ascent, descent))
    for cp, h, w, adv, dy, dx, _ in glyphs:
        out += struct.pack(">7i", cp, h, w, adv, dy, dx, 0)
    for *_, data in glyphs:
        out += data
    return bytes(out)


def parse_vlw(blob):
    """Decode a VLW file the same way M5GFX does (used for the preview check)."""
    count, _, size, _, ascent, descent = struct.unpack_from(">6i", blob, 0)
    glyphs, ptr = {}, 24 + count * 28
    max_asc, max_desc = ascent, descent
    for i in range(count):
        cp, h, w, adv, dy, dx, _ = struct.unpack_from(">7i", blob, 24 + i * 28)
        glyphs[cp] = (h, w, adv, dy, dx, blob[ptr:ptr + w * h])
        ptr += w * h
        if cp > 0x20:
            max_asc, max_desc = max(max_asc, dy), max(max_desc, h - dy)
    if ptr != len(blob):
        raise ValueError(f"VLW size mismatch: expected {ptr} bytes, file has {len(blob)}")
    return glyphs, max_asc, max_desc


def draw_text(img, x, y_top, text, glyphs, asc):
    px = img.load()
    base = y_top + asc
    for ch in text:
        g = glyphs.get(ord(ch)) or glyphs.get(0x20)
        h, w, adv, dy, dx, data = g
        for r in range(h):
            for c in range(w):
                a = data[r * w + c]
                if a:
                    X, Y = x + dx + c, base - dy + r
                    if 0 <= X < img.width and 0 <= Y < img.height:
                        px[X, Y] = min(px[X, Y], 255 - a)
        x += adv


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    OUT_IDS.parent.mkdir(parents=True, exist_ok=True)
    rendered = []
    total = 0
    for fid, path, size, weight, use in FONTS:
        blob = build_vlw(open_face(path, weight), size)
        (OUT_DIR / f"{fid}.vlw").write_bytes(blob)
        total += len(blob)
        glyphs, asc, desc = parse_vlw(blob)
        rendered.append((fid, size, use, glyphs, asc, desc))
        print(f"  {fid:14s} {size:3d}px {len(glyphs):3d} glyphs {len(blob) / 1024:7.1f} KB  ({use})")
    print(f"{len(FONTS)} fonts, {total / 1024:.0f} KB -> {OUT_DIR.relative_to(ROOT)}")

    rel = [f"assets/vlw/{f[0]}.vlw" for f in FONTS]
    OUT_INI.write_text("; Generated by tools/gen_vlw.py. Do not edit.\n[fonts]\nembed =\n"
                       + "".join(f"  {r}\n" for r in rel), encoding="utf-8")
    syms = ["_binary_" + "".join(c if c.isalnum() else "_" for c in r) for r in rel]
    enum = ",\n  ".join(f[0] for f in FONTS)
    externs = "\n".join(f'extern const uint8_t {s}_start[] asm("{s}_start");\n'
                        f'extern const uint8_t {s}_end[] asm("{s}_end");' for s in syms)
    table = ",\n  ".join(f"{{{s}_start, {s}_end}}" for s in syms)
    names = ",\n  ".join(f'"{f[0]}"' for f in FONTS)
    OUT_IDS.write_text(f"""// Generated by tools/gen_vlw.py. Do not edit.
// VLW fonts embedded in the firmware image via board_build.embed_files (fonts.ini).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace fonts {{

enum class Id : uint8_t {{
  {enum},
  count
}};

{externs}

struct Blob {{
  const uint8_t* start;
  const uint8_t* end;
  size_t size() const {{ return size_t(end - start); }}
}};

// Indexed by Id.
inline const Blob kBlobs[] = {{
  {table},
}};

inline const char* const kNames[] = {{
  {names},
}};

}}  // namespace fonts
""", encoding="utf-8")

    # Preview, decoded from the VLW bytes (checks the format round-trips).
    sample = "Brush teeth · Pack backpack “Feed the cat” 0123456789 …"
    height = sum(a + d + 14 for *_, a, d in rendered) + 20
    img = Image.new("L", (1400, height), 255)
    y = 10
    for fid, size, use, glyphs, asc, desc in rendered:
        text = "You did it!" if fid == "fredoka_68" else sample
        draw_text(img, 10, y, f"{fid}: {text}", glyphs, asc)
        y += asc + desc + 14
    PREVIEW.parent.mkdir(parents=True, exist_ok=True)
    img.save(PREVIEW)
    print(f"preview -> {PREVIEW.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
