"""Rasterize assets/icons/*.svg into 4bpp coverage masks for the firmware.

Each icon is rendered black-on-white with resvg, converted to coverage
(255 - luminance) and quantized to 16 levels (0 = empty, 15 = full ink).
Two pixels per byte, high nibble first, each row padded to a whole byte.
The firmware blends ink over the background with this mask, so the same
bitmap works on white, gray tiles and inverted (black) frames.

Outputs:
  src/ui/icons_data.h / icons_data.cpp   generated; do not edit
  build/icons_preview.png                contact sheet for a visual check

Usage:  conda run -n taskpal python tools/gen_icons.py
"""
import io
import re
from pathlib import Path

import resvg_py
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
ICON_DIR = ROOT / "assets" / "icons"
OUT_H = ROOT / "src" / "ui" / "icons_data.h"
OUT_CPP = ROOT / "src" / "ui" / "icons_data.cpp"
PREVIEW = ROOT / "build" / "icons_preview.png"

TASK_ICONS = ["shirt", "tooth", "bowl", "drop", "comb", "backpack", "shoe", "bed",
              "book", "pencil", "box", "glass", "plant", "clock", "music", "star",
              # Home & chores (D26)
              "table", "meal", "toilet", "hands", "tv", "toys", "broom", "laundry"]
LIST_ICONS = ["sun", "house", "moon", "book", "star"]

# Sizes in px, from DESIGN_SPEC.md §3 and the screen specs.
SIZES = {}
for key in TASK_ICONS:
    SIZES.setdefault(key, set()).update({36, 40, 44, 48})      # edit row, change-picture preview, new-task tile, checklist + change-picture tile
for key in LIST_ICONS:
    SIZES.setdefault(key, set()).update({38, 40, 64, 68, 72})  # admin row, list picture, cards, header
SIZES["star_fill"] = {22, 28, 40, 42}                            # decorations, star rows, footer
SIZES["star"].update({34, 42})                                   # outline decoration, empty star-row star
SIZES.update({
    "ui_sliders": {30}, "ui_chevron_right": {32}, "ui_back": {28, 32},
    "ui_check": {34, 40, 56}, "ui_lock": {34}, "ui_up": {28}, "ui_down": {28},
    "ui_trash": {28}, "ui_plus": {28}, "ui_shift": {32}, "ui_backspace": {32}, "ui_arrow_right": {28},
    "fx_burst": {120}, "fx_celebrate_a": {140, 280}, "fx_celebrate_b": {280},
    "celeb_corgi": {140}, "celeb_poop": {140},                   # Celebration picker thumbnails (D27)
    "about_logo": {168},                                         # About screen (D29)
})

# Dance poses (D27): non-square stage pictures, halftoned with the screen-anchored 4x4 Bayer matrix
# (D18) so every pixel is pure black or white and DU can animate them. (w, h, screen x, screen y);
# the stage rects must match tools/extract_mockup.py and artFor() in src/screens/checklist.cpp.
STAGES = {
    "celeb_corgi_a": (432, 380, 54, 77), "celeb_corgi_b": (432, 380, 54, 77),
    "celeb_poop_a": (432, 330, 54, 102), "celeb_poop_b": (432, 330, 54, 102),  # poop v2 (DonePoop.dc.html)
}
BAYER4 = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def load_svg(key):
    if key == "star_fill":
        svg = (ICON_DIR / "star.svg").read_text(encoding="utf-8")
        return svg.replace('fill="none"', 'fill="#000"')
    return (ICON_DIR / f"{key}.svg").read_text(encoding="utf-8")


def render(svg, size, height=None):
    height = height or size
    png = resvg_py.svg_to_bytes(svg_string=svg, width=size, height=height, background="#ffffff")
    img = Image.open(io.BytesIO(bytes(png))).convert("L")
    if img.size != (size, height):
        img = img.resize((size, height), Image.LANCZOS)
    return img


def halftone(img, ox, oy):
    """Near-black -> black, near-white -> white, grays -> Bayer dots anchored at screen (ox, oy)."""
    w, h = img.size
    src = img.load()
    out = Image.new("L", (w, h), 255)
    dst = out.load()
    for y in range(h):
        for x in range(w):
            v = src[x, y]
            if v < 64:
                dst[x, y] = 0
            elif v <= 232 and (255 - v) * 16 / 255 > BAYER4[(y + oy) % 4][(x + ox) % 4] + 0.5:
                dst[x, y] = 0
    return out


def to_nibbles(img):
    w, h = img.size
    px = img.load()
    out = bytearray()
    for y in range(h):
        row = [(255 - px[x, y] + 8) * 15 // 255 for x in range(w)]
        if w % 2:
            row.append(0)
        for i in range(0, len(row), 2):
            out.append((min(row[i], 15) << 4) | min(row[i + 1], 15))
    return bytes(out)


def c_ident(key):
    return re.sub(r"\W", "_", key)


def main():
    keys = sorted(set(SIZES) | set(STAGES))
    entries = []  # (key, w, h, ident, data, img); find() looks bitmaps up by width
    for key in keys:
        svg = load_svg(key)
        for size in sorted(SIZES.get(key, ())):
            img = render(svg, size)
            entries.append((key, size, size, f"k_{c_ident(key)}_{size}", to_nibbles(img), img))
        if key in STAGES:
            w, h, ox, oy = STAGES[key]
            img = halftone(render(svg, w, h), ox, oy)
            entries.append((key, w, h, f"k_{c_ident(key)}_{w}x{h}", to_nibbles(img), img))

    ids = ",\n  ".join(c_ident(k) for k in keys)
    names = ", ".join(f'"{k}"' for k in keys)
    total = sum(len(e[4]) for e in entries)
    OUT_H.parent.mkdir(parents=True, exist_ok=True)

    OUT_H.write_text(f"""// Generated by tools/gen_icons.py from assets/icons/*.svg. Do not edit.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace icons {{

// 4bpp coverage mask: 0 = background, 15 = full ink. Two pixels per byte, high
// nibble first; each row is (w + 1) / 2 bytes.
struct Bitmap {{
  uint16_t w;
  uint16_t h;
  const uint8_t* data;
}};

enum class Id : uint8_t {{
  {ids},
  count
}};

// Stable string keys used in lists.json ("shirt", "sun", ...). Indexed by Id.
extern const char* const kNames[{len(keys)}];

// Returns nullptr if the icon was not generated at that size (the width, for non-square pictures).
const Bitmap* find(Id id, uint16_t size);

// Returns Id::count if the name is unknown.
Id fromName(const char* name);

}}  // namespace icons
""", encoding="utf-8")

    arrays = []
    table = []
    for key, w, h, ident, data, _ in entries:
        byte_strs = [f"0x{b:02x}" for b in data]
        lines = "\n".join("  " + ",".join(byte_strs[i:i + 20]) + "," for i in range(0, len(byte_strs), 20))
        arrays.append(f"static const uint8_t {ident}_data[] = {{\n{lines}\n}};")
        table.append(f"  {{Id::{c_ident(key)}, {{{w}, {h}, {ident}_data}}}},")

    OUT_CPP.write_text(f"""// Generated by tools/gen_icons.py from assets/icons/*.svg. Do not edit.
// {len(entries)} bitmaps, {total} bytes.
#include "icons_data.h"

#include <string.h>

namespace icons {{

const char* const kNames[{len(keys)}] = {{{names}}};

{chr(10).join(arrays)}

namespace {{
struct Entry {{
  Id id;
  Bitmap bmp;
}};
const Entry kEntries[] = {{
{chr(10).join(table)}
}};
}}  // namespace

const Bitmap* find(Id id, uint16_t size) {{
  for (const auto& e : kEntries) {{
    if (e.id == id && e.bmp.w == size) return &e.bmp;
  }}
  return nullptr;
}}

Id fromName(const char* name) {{
  for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); ++i) {{
    if (strcmp(kNames[i], name) == 0) return static_cast<Id>(i);
  }}
  return Id::count;
}}

}}  // namespace icons
""", encoding="utf-8")

    # Contact sheet: every bitmap, quantized to 16 levels as the panel will show it.
    pad, x, y, row_h, sheet_w = 12, 12, 12, 0, 1100
    placed = []
    for key, w, h, _, _, img in entries:
        if x + w + pad > sheet_w:
            x, y, row_h = pad, y + row_h + pad, 0
        placed.append((x, y, img))
        x += w + pad
        row_h = max(row_h, h)
    sheet = Image.new("L", (sheet_w, y + row_h + pad), 255)
    for px_, py_, img in placed:
        q = img.point(lambda v: ((v * 15 + 127) // 255) * 17)
        sheet.paste(q, (px_, py_))
    PREVIEW.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(PREVIEW)

    print(f"{len(entries)} bitmaps from {len(keys)} icons, {total} bytes -> {OUT_CPP.relative_to(ROOT)}")
    print(f"preview -> {PREVIEW.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
