from PIL import Image, ImageDraw, ImageFont, ImageFilter
PANEL = Image.open('/root/Projects/TowerOfBabel/assets/ui/panel.png').convert('RGBA')
FONT = '/tmp/fonts/fellsc.ttf'

def nine(w, h, sb=60, db=30):   # the game's parchment panel, nine-sliced to w x h
    sw, sh = PANEL.size; out = Image.new('RGBA', (w, h))
    xs = [0, sb, sw - sb, sw]; ys = [0, sb, sh - sb, sh]
    dx = [0, db, w - db, w]; dy = [0, db, h - db, h]
    for i in range(3):
        for j in range(3):
            tile = PANEL.crop((xs[i], ys[j], xs[i + 1], ys[j + 1])).resize((max(1, dx[i + 1] - dx[i]), max(1, dy[j + 1] - dy[j])), Image.LANCZOS)
            out.paste(tile, (dx[i], dy[j]))
    return out

def plaque(img, x, y, h, text='Babel'):
    font = ImageFont.truetype(FONT, int(h * 0.62))
    tw = font.getbbox(text)[2] - font.getbbox(text)[0]
    w = int(tw + h * 0.9)
    shadow = Image.new('RGBA', img.size, (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle((x + 6, y + 10, x + w + 6, y + h + 10), radius=int(h * 0.1), fill=(30, 20, 10, 130))
    img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(h * 0.08)))
    img.alpha_composite(nine(w, h, 60, int(h * 0.22)), (x, y))
    d = ImageDraw.Draw(img)
    bb = font.getbbox(text); tx = x + (w - (bb[2] - bb[0])) / 2 - bb[0]; ty = y + (h - (bb[3] - bb[1])) / 2 - bb[1] - h * 0.02
    off = max(2, int(h * 0.03))
    d.text((tx + off, ty + off), text, font=font, fill=(138, 106, 74, 255))   # the game's title shadow
    d.text((tx, ty), text, font=font, fill=(168, 56, 42, 255))                # and its vermilion

def crop_to(img, ar, anchor_y=0.5):
    w, h = img.size
    if w / h > ar: nw = int(h * ar); x = (w - nw) // 2; return img.crop((x, 0, x + nw, h))
    nh = int(w / ar); y = int((h - nh) * anchor_y); return img.crop((0, y, w, y + nh))

# cover: 630x500 and a 2x version
cov = crop_to(Image.open('/tmp/itch/ludo/cover_up.webp').convert('RGBA'), 630 / 500, 0.5)
for W, name in [(1260, 'cover_1260x1000.png'), (630, 'cover_630x500.png')]:
    c = cov.resize((W, int(W * 500 / 630)), Image.LANCZOS)
    plaque(c, int(W * 0.035), int(W * 0.03), int(W * 0.135))
    c.convert('RGB').save('/tmp/itch/out/' + name)
# banner: 1920 wide (itch shows up to 960-1200), 2.4:1, keep the sky
ban = Image.open('/tmp/itch/ludo/banner_up.webp').convert('RGBA')
ban = ban.resize((1920, int(1920 * ban.height / ban.width)), Image.LANCZOS)
ban = crop_to(ban, 2.4, 0.1)
plaque(ban, 70, 70, 190)
ban.convert('RGB').save('/tmp/itch/out/banner_1920x800.png')
ban.convert('RGB').resize((960, 400), Image.LANCZOS).save('/tmp/itch/out/banner_960x400.png')
print('ok', ban.size)
