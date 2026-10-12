"""Decode the TaskPal HTML mockup bundles and extract the icon SVG paths.

The mockups in docs/ are bundles: a JSON manifest of gzip+base64 entries, where each
artboard is itself a page whose template is stored as a JSON string literal.

Outputs:
  build/mockup/<n>_<title>.html   readable artboard sources (git-ignored, for reference)
  assets/icons/<key>.svg          one 48x48 stroke icon per ICONS entry
  assets/icons/icons.json         {key: path-data} merged from every artboard

Usage:  conda run -n taskpal python tools/extract_mockup.py
"""
import base64
import gzip
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCES = [
    ROOT / "docs" / "TaskPal for M5Paper.html",
    ROOT / "docs" / "TaskPal for M5Paper_Keyboard.html",
    ROOT / "docs" / "TaskPal for M5Paper_Battery_NewIcons_.html",  # Change picture: Home & chores (D26)
    ROOT / "docs" / "TaskPal for M5Paper_Celebration_Variants.html",  # dancing corgi / poop (D27)
    # Plain (unbundled) artboard pages, read after the bundles so a newer version wins.
    ROOT / "docs" / "DonePoop.dc.html",  # dancing poop v2: wider viewBox, left arm no longer cut (D27)
]

# Celebration variants (D27). Each artboard's hero SVG plus its floating notes becomes a "stage"
# picture per dance pose: pose a shows the class="fa" parts, pose b the class="fb" parts, with the
# artboard's sway/boogie transform baked in. The stage rect is in screen coordinates and must match
# STAGES in tools/gen_icons.py and artFor() in src/screens/checklist.cpp.
STAGE_X, STAGE_W = 54, 432
# The All done column is centred and identical apart from the hero, so the hero's centre is always
# at y 267 (corgi 380 px tall -> top 77; poop v2 330 px -> top 102).
HERO_CENTER_Y = 267
CELEBRATIONS = {
    # artboard title fragment: (key, pose a transform, pose b transform)
    "dancing corgi": ("corgi", "rotate(-7 140 305)", "rotate(7 140 305)"),
    "dancing poop": ("poop",
                     "translate(140 262) scale(1.06 0.94) rotate(-5) translate(-140 -262)",
                     "translate(140 262) translate(0 -18) scale(0.96 1.05) rotate(5) translate(-140 -262)"),
}
HERO_RE = re.compile(r'<svg width="(\d+)" height="(\d+)" (?:sc-camel-view-box|viewBox)="([^"]+)" role="img"[^>]*>'
                     r'(.*?)</svg>', re.S)
NOTE_RE = re.compile(r'<svg class="(fa|fb)"([^>]*)>(.*?)</svg>', re.S)


def celebration_svgs(html, key, transforms):
    """Returns {name: svg} for the two stage poses and a hero-only thumbnail (pose a, no notes)."""
    hero = HERO_RE.search(html)
    hero_w, hero_h, view_box, inner = int(hero.group(1)), int(hero.group(2)), hero.group(3), hero.group(4)
    hero_y = HERO_CENTER_Y - hero_h // 2
    print(f"celebration {key}: hero {hero_w}x{hero_h} at ({(540 - hero_w) // 2}, {hero_y})")
    out = {}
    for pose, hide, transform in (("a", "fb", transforms[0]), ("b", "fa", transforms[1])):
        body = re.sub(rf'<g class="{hide}"[^>]*>.*?</g>', "", inner, flags=re.S)
        body = re.sub(r'<g class="(?:sway|boogie)">', f'<g transform="{transform}">', body)
        hero_svg = (f'<svg x="{(540 - hero_w) // 2 - STAGE_X}" y="0" width="{hero_w}" height="{hero_h}" '
                    f'viewBox="{view_box}">{body}</svg>')
        notes = []
        for cls, attrs, note_inner in NOTE_RE.findall(html):
            if cls == hide:
                continue
            w = int(re.search(r'width="(\d+)"', attrs).group(1))
            style = re.search(r'style="([^"]*)"', attrs).group(1)
            left = re.search(r"left:\s*(\d+)px", style)
            right = re.search(r"right:\s*(\d+)px", style)
            top = int(re.search(r"top:\s*(\d+)px", style).group(1))
            x = int(left.group(1)) if left else 540 - int(right.group(1)) - w
            attrs = re.sub(r'\s*style="[^"]*"', "", attrs).replace("sc-camel-view-box", "viewBox")
            notes.append(f'<svg x="{x - STAGE_X}" y="{top - hero_y}"{attrs}>{note_inner}</svg>')
        out[f"celeb_{key}_{pose}"] = (
            f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {STAGE_W} {hero_h}" width="{STAGE_W}" '
            f'height="{hero_h}">{hero_svg}{"".join(notes)}</svg>\n')
        if pose == "a":
            out[f"celeb_{key}"] = f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{view_box}">{body}</svg>\n'
    return out
OUT_MOCKUP = ROOT / "build" / "mockup"
OUT_ICONS = ROOT / "assets" / "icons"

ICONS_RE = re.compile(r"ICONS\s*=\s*\{(.*?)\};", re.S)
ENTRY_RE = re.compile(r"""['"]?([A-Za-z_][\w-]*)['"]?\s*:\s*'([^']*)'""")
# The Change picture artboard lists its pictures as arrays: var HOME = [['key', 'Label', 'path'], ...];
ARRAY_RE = re.compile(r"var\s+(?:EVERYDAY|HOME)\s*=\s*\[(.*?)\];", re.S)
ARRAY_ENTRY_RE = re.compile(r"\[\s*'([\w-]+)'\s*,\s*'[^']*'\s*,\s*'([^']*)'\s*\]")
# UI glyphs are inline <svg> in the artboard templates, not in ICONS. Path data copied
# from the artboards named in the comments. shift/backspace are not drawn in the mockup
# (the keyboard uses them), so they are drawn here in the same 48x48 stroke style.
UI_GLYPHS = {
    "ui_sliders": "M8 14 H40 M8 24 H40 M8 34 H40 M16 9 V19 M32 19 V29 M20 29 V39",  # Pick a list
    "ui_chevron_right": "M18 10 L32 24 L18 38",  # Pick a list
    "ui_back": "M30 10 L16 24 L30 38",  # Morning checklist
    "ui_check": "M12 25 L21 34 L37 15",  # Morning checklist
    "ui_lock": "M12 22 H36 V42 H12 Z M17 22 V15 A7 7 0 0 1 31 15 V22 M24 30 V34",  # Parent settings
    "ui_up": "M12 30 L24 18 L36 30",  # Parent settings
    "ui_down": "M12 18 L24 30 L36 18",  # Parent settings
    "ui_trash": "M10 14 H38 M20 14 V9 H28 V14 M14 14 L16 40 H32 L34 14 M21 21 V33 M27 21 V33",
    "ui_plus": "M24 10 V38 M10 24 H38",  # Parent settings
    "ui_shift": "M24 8 L8 26 H17 V40 H31 V26 H40 Z",  # drawn here
    "ui_backspace": "M16 10 H42 V38 H16 L4 24 Z M22 18 L34 30 M34 18 L22 30",  # drawn here
    "ui_arrow_right": "M8 24 H38 M28 14 L38 24 L28 34",  # Change picture (old -> new)
}

# Large graphics with their own viewBox (Morning checklist / Motion on e-ink artboards).
BURST_120 = "M60 4 V18 M60 102 V116 M4 60 H18 M102 60 H116 M20 20 L30 30 M90 90 L100 100 M100 20 L90 30 M30 90 L20 100"
CELEB_RAYS = "M140 10 V44 M140 236 V270 M10 140 H44 M236 140 H270 M48 48 L72 72 M208 208 L232 232 M232 48 L208 72 M72 208 L48 232"
CELEB_STAR_A = "M140 62 L162 110 L214 116 L175 151 L186 203 L140 177 L94 203 L105 151 L66 116 L118 110 Z"
CELEB_STAR_B = "M140 44 L162 92 L214 98 L175 133 L186 185 L140 159 L94 185 L105 133 L66 98 L118 92 Z"
CELEB_TICK = "M122 140 L135 153 L160 126"  # All done celebration artboard (white, stroke 8)
STAR_48 = "M24 5 L29.5 17 L42 18.5 L32.5 27 L35 40 L24 33.5 L13 40 L15.5 27 L6 18.5 L18.5 17 Z"
LARGE_SVGS = {
    # About screen logo (docs/About.dc.html): 148 px black tile, radius 40, white 96 px check, and a
    # 40 px white star badge sticking out 10 px at the top right. Drawn as a 168 px square.
    "about_logo": (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 168 168" width="168" height="168">'
        '<rect x="0" y="20" width="148" height="148" rx="40" fill="#000"/>'
        '<svg x="26" y="46" width="96" height="96" viewBox="0 0 48 48"><path d="M11 25 L20 34 L37 14" fill="none" '
        'stroke="#fff" stroke-width="5.5" stroke-linecap="round" stroke-linejoin="round"/></svg>'
        f'<svg x="118" y="10" width="40" height="40" viewBox="0 0 48 48"><path d="{STAR_48}" fill="#fff" '
        'stroke="#000" stroke-width="3" stroke-linejoin="round"/></svg></svg>\n'
    ),
    "fx_burst": (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 120 120" width="120" height="120">'
        f'<path d="{BURST_120}" fill="none" stroke="#000" stroke-width="5" stroke-linecap="round"/></svg>\n'
    ),
    "fx_celebrate_a": (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 280 280" width="280" height="280">'
        f'<path d="{CELEB_RAYS}" fill="none" stroke="#000" stroke-width="8" stroke-linecap="round"/>'
        f'<path d="{CELEB_STAR_A}" fill="#000" stroke="#000" stroke-width="6" stroke-linejoin="round"/>'
        f'<path d="{CELEB_TICK}" fill="none" stroke="#fff" stroke-width="8" stroke-linecap="round" stroke-linejoin="round"/></svg>\n'
    ),
    "fx_celebrate_b": (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 280 280" width="280" height="280">'
        f'<path d="{CELEB_RAYS}" fill="none" stroke="#000" stroke-width="8" stroke-linecap="round" transform="rotate(22.5 140 140)"/>'
        f'<path d="{CELEB_STAR_B}" fill="#000" stroke="#000" stroke-width="6" stroke-linejoin="round"/>'
        f'<path d="{CELEB_TICK}" fill="none" stroke="#fff" stroke-width="8" stroke-linecap="round" stroke-linejoin="round" transform="translate(0 -18)"/></svg>\n'
    ),
}

SVG_TEMPLATE = (
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 48 48" width="48" height="48">'
    '<path d="{d}" fill="none" stroke="#000" stroke-width="3" '
    'stroke-linecap="round" stroke-linejoin="round"/></svg>\n'
)


def decode_entry(entry):
    raw = base64.b64decode(entry["data"])
    if entry.get("compressed"):
        raw = gzip.decompress(raw)
    return raw.decode("utf-8", "replace")


def manifests(text):
    """Yield every bundle manifest (a JSON object line of {uuid: {mime, data}})."""
    for line in text.split("\n"):
        line = line.strip()
        if line.startswith('{"') and '"data"' in line[:400]:
            try:
                yield json.loads(line)
            except json.JSONDecodeError:
                pass


def templates(text):
    """Yield page templates stored as JSON string literals ("<!DOCTYPE html>...")."""
    for line in text.split("\n"):
        line = line.strip()
        if line.startswith('"<!DOCTYPE') and line.endswith('"'):
            yield json.loads(line)


def artboards(path):
    outer = path.read_text(encoding="utf-8")
    if path.name.endswith(".dc.html"):  # a plain artboard page, not a bundle
        return [outer]
    found = []
    for man in manifests(outer):
        for entry in man.values():
            if not entry.get("mime", "").startswith("text/html"):
                continue
            page = decode_entry(entry)
            found.extend(templates(page) or [page])
            # Artboards may carry their own nested bundle.
            for inner in manifests(page):
                for sub in inner.values():
                    if sub.get("mime", "").startswith("text/html"):
                        found.extend(templates(decode_entry(sub)))
    if not found:
        found.extend(templates(outer))
    return found


def main():
    OUT_MOCKUP.mkdir(parents=True, exist_ok=True)
    OUT_ICONS.mkdir(parents=True, exist_ok=True)
    icons = {}
    seen = set()
    n = 0
    for src in SOURCES:
        for html in artboards(src):
            if html in seen:
                continue
            seen.add(html)
            n += 1
            title = re.search(r"<title>(.*?)</title>", html)
            name = re.sub(r"[^\w]+", "_", title.group(1) if title else "artboard").strip("_")
            (OUT_MOCKUP / f"{n:02d}_{name}.html").write_text(html, encoding="utf-8")
            for fragment, (key, pose_a, pose_b) in CELEBRATIONS.items():
                if title and fragment in title.group(1).lower():
                    for ckey, svg in celebration_svgs(html, key, (pose_a, pose_b)).items():
                        (OUT_ICONS / f"{ckey}.svg").write_text(svg, encoding="utf-8")
                        print(f"celebration -> {ckey}.svg")
            entries = [e for block in ICONS_RE.findall(html) for e in ENTRY_RE.findall(block)]
            entries += [e for block in ARRAY_RE.findall(html) for e in ARRAY_ENTRY_RE.findall(block)]
            if entries:
                for key, d in entries:
                    if key in icons and icons[key] != d:
                        print(f"warning: icon '{key}' differs between artboards; keeping first")
                        continue
                    icons[key] = d
    all_paths = {**icons, **UI_GLYPHS}
    for key, d in sorted(all_paths.items()):
        (OUT_ICONS / f"{key}.svg").write_text(SVG_TEMPLATE.format(d=d), encoding="utf-8")
    for key, svg in LARGE_SVGS.items():
        (OUT_ICONS / f"{key}.svg").write_text(svg, encoding="utf-8")
    (OUT_ICONS / "icons.json").write_text(json.dumps(all_paths, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"{n} artboards -> {OUT_MOCKUP.relative_to(ROOT)}")
    print(f"{len(icons)} icons -> {OUT_ICONS.relative_to(ROOT)}: {', '.join(sorted(icons))}")
    print(f"{len(UI_GLYPHS)} UI glyphs + {len(LARGE_SVGS)} large graphics")


if __name__ == "__main__":
    main()
