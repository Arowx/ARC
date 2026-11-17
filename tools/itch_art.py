#!/usr/bin/env python3
"""itch.io page art: a cover and a banner, cropped from your pictures, with the game's title on a plaque.

    python tools/itch_art.py --title "Babel" --cover art/cover.png --banner art/wide.png --out dist/itch \\
        --font fonts/FellSC.ttf --panel assets/ui/panel.png --title-color A8382A --shadow-color 8A6A4A

Writes (any you give a picture for):
    cover_630x500.png and cover_1260x1000.png    the game's thumbnail (itch.io asks for 630x500; 2x for sharp screens)
    banner_1920x800.png and banner_960x400.png   the page's top banner (2.4:1)
Pictures are scaled and cropped to fit (centred; --cover-y / --banner-y 0..1 choose which part is kept vertically,
0 = the top, e.g. 0.1 keeps the sky). Screenshots straight from a game work: `shot cover.png` with `camera ...`.

The plaque: --panel PNG is nine-sliced (its corners kept, --slice N pixels of the source, default 60) behind the title;
without it a rounded parchment box is drawn (--plaque-color, --border-color). --font is a .ttf/.otf (default: Pillow's
own font). --corner tl|tr|bl|br places it (default tl). --no-title leaves the pictures plain.

Needs Python 3 with Pillow. (From the Babel project's compose.py.)
"""
import argparse, os, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont


def rgb(h, a=255):
    h = h.lstrip('#'); return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a)


def crop_to(img, ar, anchor_y=0.5):
    w, h = img.size
    if w / h > ar:
        nw = int(h * ar); x = (w - nw) // 2; return img.crop((x, 0, x + nw, h))
    nh = int(w / ar); y = int((h - nh) * anchor_y); return img.crop((0, y, w, y + nh))


def nine(panel, w, h, sb, db):
    """the panel nine-sliced to w x h: source border sb pixels, drawn border db pixels"""
    sw, sh = panel.size; out = Image.new('RGBA', (w, h))
    xs = [0, sb, sw - sb, sw]; ys = [0, sb, sh - sb, sh]; dx = [0, db, w - db, w]; dy = [0, db, h - db, h]
    for i in range(3):
        for j in range(3):
            tile = panel.crop((xs[i], ys[j], xs[i + 1], ys[j + 1])).resize((max(1, dx[i + 1] - dx[i]), max(1, dy[j + 1] - dy[j])), Image.LANCZOS)
            out.paste(tile, (dx[i], dy[j]))
    return out


def font_at(path, size):
    if path: return ImageFont.truetype(path, size)
    try: return ImageFont.load_default(size)          # Pillow 10.1+
    except TypeError: return ImageFont.load_default()


def plaque(img, a, h):
    font = font_at(a.font, int(h * 0.62))
    bb = font.getbbox(a.title); tw = bb[2] - bb[0]
    w = int(tw + h * 0.9); m = int(min(img.size) * 0.05)
    x = m if a.corner in ('tl', 'bl') else img.width - w - m
    y = m if a.corner in ('tl', 'tr') else img.height - h - m
    shadow = Image.new('RGBA', img.size, (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle((x + 6, y + 10, x + w + 6, y + h + 10), radius=int(h * 0.1), fill=(30, 20, 10, 130))
    img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(h * 0.08)))
    if a.panel:
        img.alpha_composite(nine(Image.open(a.panel).convert('RGBA'), w, h, a.slice, int(h * 0.22)), (x, y))
    else:
        box = Image.new('RGBA', (w, h), (0, 0, 0, 0))
        ImageDraw.Draw(box).rounded_rectangle((0, 0, w - 1, h - 1), radius=int(h * 0.12), fill=rgb(a.plaque_color),
                                              outline=rgb(a.border_color), width=max(2, int(h * 0.05)))
        img.alpha_composite(box, (x, y))
    d = ImageDraw.Draw(img)
    tx = x + (w - tw) / 2 - bb[0]; ty = y + (h - (bb[3] - bb[1])) / 2 - bb[1] - h * 0.02
    off = max(2, int(h * 0.03))
    d.text((tx + off, ty + off), a.title, font=font, fill=rgb(a.shadow_color))
    d.text((tx, ty), a.title, font=font, fill=rgb(a.title_color))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--title', required=True)
    ap.add_argument('--cover', help='picture for the cover (630x500)')
    ap.add_argument('--banner', help='picture for the banner (1920x800)')
    ap.add_argument('--out', default='dist/itch')
    ap.add_argument('--font'); ap.add_argument('--panel'); ap.add_argument('--slice', type=int, default=60)
    ap.add_argument('--title-color', default='A8382A'); ap.add_argument('--shadow-color', default='8A6A4A')
    ap.add_argument('--plaque-color', default='F2E8D2'); ap.add_argument('--border-color', default='8A6A4A')
    ap.add_argument('--corner', choices=['tl', 'tr', 'bl', 'br'], default='tl')
    ap.add_argument('--cover-y', type=float, default=0.5); ap.add_argument('--banner-y', type=float, default=0.1)
    ap.add_argument('--no-title', action='store_true')
    a = ap.parse_args()
    if not a.cover and not a.banner: sys.exit('give --cover and/or --banner')
    os.makedirs(a.out, exist_ok=True)
    made = []
    if a.cover:
        cov = crop_to(Image.open(a.cover).convert('RGBA'), 630 / 500, a.cover_y)
        for W in (1260, 630):
            c = cov.resize((W, int(W * 500 / 630)), Image.LANCZOS)
            if not a.no_title: plaque(c, a, int(W * 0.135))
            p = os.path.join(a.out, 'cover_%dx%d.png' % (W, int(W * 500 / 630))); c.convert('RGB').save(p); made.append(p)
    if a.banner:
        ban = Image.open(a.banner).convert('RGBA')
        ban = crop_to(ban.resize((1920, max(1, int(1920 * ban.height / ban.width))), Image.LANCZOS), 2.4, a.banner_y)
        ban = ban.resize((1920, 800), Image.LANCZOS)
        if not a.no_title: plaque(ban, a, 190)
        for W, H in ((1920, 800), (960, 400)):
            p = os.path.join(a.out, 'banner_%dx%d.png' % (W, H)); ban.convert('RGB').resize((W, H), Image.LANCZOS).save(p); made.append(p)
    for p in made: print('wrote', p)


if __name__ == '__main__':
    main()
