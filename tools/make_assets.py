#!/usr/bin/env python3
"""
Gera os PNGs que viram textura no 3DS:

  build_assets/cards.png   atlas 1024x1024: 78 cartas 64x108 (16 colunas), pixeladas
  build_assets/ui.png      atlas 256x256: verso da carta, tile de feltro, icones dos decks
  icon.png                 icone 48x48 do homebrew (só se ainda não existir)

Fonte das cartas: Rider-Waite-Smith de 1909 (Pamela Colman Smith), dominio publico,
baixada do Wikimedia Commons. Ordem dos ids (igual ao source/tarot.h):
  0..21 Maiores | 22..35 Paus | 36..49 Copas | 50..63 Espadas | 64..77 Ouros

Prioridade por carta:  cards_src/NN.(png|jpg)  >  cache local  >  download  >  placeholder
Uso:  python3 tools/make_assets.py [--out build_assets] [--cache build_assets/cache] [--allow-missing]
Requer: Pillow.
"""
import argparse, io, json, os, random, sys, time, urllib.parse, urllib.request, urllib.error
from PIL import Image, ImageDraw, ImageEnhance, ImageFont

CARD_W, CARD_H = 64, 108
ART_W, ART_H = 54, 98
COLS = 16

OUTLINE = (30, 8, 14, 255)
CREAM = (244, 232, 200, 255)
GOLD = (255, 205, 80, 255)

MAJOR_FILES = [
    "RWS_Tarot_00_Fool.jpg", "RWS_Tarot_01_Magician.jpg", "RWS_Tarot_02_High_Priestess.jpg",
    "RWS_Tarot_03_Empress.jpg", "RWS_Tarot_04_Emperor.jpg", "RWS_Tarot_05_Hierophant.jpg",
    "RWS_Tarot_06_Lovers.jpg", "RWS_Tarot_07_Chariot.jpg", "RWS_Tarot_08_Strength.jpg",
    "RWS_Tarot_09_Hermit.jpg", "RWS_Tarot_10_Wheel_of_Fortune.jpg", "RWS_Tarot_11_Justice.jpg",
    "RWS_Tarot_12_Hanged_Man.jpg", "RWS_Tarot_13_Death.jpg", "RWS_Tarot_14_Temperance.jpg",
    "RWS_Tarot_15_Devil.jpg", "RWS_Tarot_16_Tower.jpg", "RWS_Tarot_17_Star.jpg",
    "RWS_Tarot_18_Moon.jpg", "RWS_Tarot_19_Sun.jpg", "RWS_Tarot_20_Judgement.jpg",
    "RWS_Tarot_21_World.jpg",
]
SUITS = ["Wands", "Cups", "Swords", "Pents"]   # Paus, Copas, Espadas, Ouros

OFFLINE = False
UA = "tarot3ds-assets/1.0 (https://github.com/%s; homebrew build script)" % os.environ.get("GITHUB_REPOSITORY", "tarot3ds")


def card_basename(cid):
    """Nome-base do arquivo no Commons (sem extensao)."""
    if cid < 22:
        return MAJOR_FILES[cid].rsplit(".", 1)[0]
    s, r = divmod(cid - 22, 14)
    return "%s%02d" % (SUITS[s], r + 1)


# ---------------------------------------------------------------- download
def http_get(url, tries=5):
    last = None
    for i in range(tries):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": UA})
            with urllib.request.urlopen(req, timeout=60) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            last = e
            if e.code == 404:
                raise
            time.sleep(2 * (i + 1) + random.random())
        except Exception as e:  # rede instavel
            last = e
            time.sleep(2 * (i + 1))
    raise last


def commons_file(name, width=420):
    url = "https://commons.wikimedia.org/wiki/Special:FilePath/%s?width=%d" % (urllib.parse.quote(name), width)
    return http_get(url)


def commons_search(base):
    """Procura pelo nome na API quando o nome direto não existe."""
    q = urllib.parse.urlencode({
        "action": "query", "list": "search", "srsearch": base, "srnamespace": 6,
        "srlimit": 15, "format": "json"})
    data = json.loads(http_get("https://commons.wikimedia.org/w/api.php?" + q))
    key = base.lower().replace("_", "").replace(" ", "")
    for hit in data.get("query", {}).get("search", []):
        title = hit["title"].split(":", 1)[1]
        flat = title.lower().replace("_", "").replace(" ", "")
        if flat.startswith(key) and flat.rsplit(".", 1)[-1] in ("jpg", "jpeg", "png"):
            return title
    return None


def fetch_card(cid, cache, local_dir):
    # 1) override local
    for ext in ("png", "jpg", "jpeg", "webp"):
        p = os.path.join(local_dir, "%02d.%s" % (cid, ext))
        if os.path.exists(p):
            return Image.open(p).convert("RGB"), "local"
    # 2) cache
    cp = os.path.join(cache, "%02d.bin" % cid)
    if os.path.exists(cp):
        return Image.open(cp).convert("RGB"), "cache"
    # 3) Commons
    if OFFLINE:
        return None, "offline"
    base = card_basename(cid)
    names = [base + ".jpg"]
    try:
        try:
            raw = commons_file(names[0])
        except urllib.error.HTTPError:
            alt = commons_search(base)
            if not alt:
                raise
            raw = commons_file(alt)
        os.makedirs(cache, exist_ok=True)
        with open(cp, "wb") as f:
            f.write(raw)
        time.sleep(0.4)  # educado com o Commons
        return Image.open(io.BytesIO(raw)).convert("RGB"), "download"
    except Exception as e:
        print("::warning::carta %d (%s) nao encontrada: %s" % (cid, base, e), flush=True)
        return None, "missing"


# ---------------------------------------------------------------- pixel art
def rounded_corners(img):
    w, h = img.size
    px = img.load()
    for (x, y) in ((0, 0), (1, 0), (0, 1), (w - 1, 0), (w - 2, 0), (w - 1, 1),
                   (0, h - 1), (1, h - 1), (0, h - 2), (w - 1, h - 1), (w - 2, h - 1), (w - 1, h - 2)):
        px[x, y] = (0, 0, 0, 0)
    return img


def frame_card(art):
    """art 54x98 RGBA -> carta 64x108 com contorno, moldura creme e cantos arredondados."""
    c = Image.new("RGBA", (CARD_W, CARD_H), OUTLINE)
    d = ImageDraw.Draw(c)
    d.rectangle([1, 1, CARD_W - 2, CARD_H - 2], fill=CREAM)
    d.rectangle([4, 4, CARD_W - 5, CARD_H - 5], fill=OUTLINE)
    c.paste(art, (5, 5))
    return rounded_corners(c)


def pixelate(src):
    w, h = src.size
    mx, my = int(w * 0.045), int(h * 0.035)
    src = src.crop((mx, my, w - mx, h - my))
    src = ImageEnhance.Color(src).enhance(1.3)
    src = ImageEnhance.Contrast(src).enhance(1.15)
    small = src.resize((ART_W, ART_H), Image.Resampling.BOX)
    q = small.quantize(colors=20, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).convert("RGBA")
    return frame_card(q)


def placeholder(cid):
    art = Image.new("RGBA", (ART_W, ART_H), (150, 40, 60, 255))
    d = ImageDraw.Draw(art)
    for y in range(0, ART_H, 6):
        d.line([(0, y), (ART_W, y)], fill=(130, 30, 50, 255))
    d.text((8, 40), "#%02d" % cid, fill=CREAM, font=ImageFont.load_default())
    return frame_card(art)


def felt_tile():
    rnd = random.Random(7)
    t = Image.new("RGBA", (32, 32))
    px = t.load()
    for y in range(32):
        for x in range(32):
            c = (122, 20, 34)
            if (x + y) % 8 == 0:
                c = (112, 17, 30)
            r = rnd.random()
            if r < 0.14:
                c = (102, 13, 26)
            elif r > 0.93:
                c = (138, 28, 44)
            px[x, y] = c + (255,)
    return t


def star_points(cx, cy, r_out, r_in, n=8):
    import math
    pts = []
    for i in range(n * 2):
        a = math.pi * i / n - math.pi / 2
        r = r_out if i % 2 == 0 else r_in
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def card_back():
    c = Image.new("RGBA", (CARD_W, CARD_H), OUTLINE)
    d = ImageDraw.Draw(c)
    d.rectangle([1, 1, CARD_W - 2, CARD_H - 2], fill=CREAM)
    d.rectangle([4, 4, CARD_W - 5, CARD_H - 5], fill=(86, 18, 44, 255))
    px = c.load()
    for y in range(5, CARD_H - 5):          # losangos
        for x in range(5, CARD_W - 5):
            if ((x + y) % 8 == 0) or ((x - y) % 8 == 0):
                px[x, y] = (112, 30, 60, 255)
    d.rectangle([8, 8, CARD_W - 9, CARD_H - 9], outline=(214, 160, 60, 255))
    cx, cy = CARD_W // 2, CARD_H // 2
    d.polygon(star_points(cx, cy, 17, 8), fill=GOLD, outline=(160, 100, 30, 255))
    d.ellipse([cx - 5, cy - 5, cx + 5, cy + 5], fill=(86, 18, 44, 255), outline=(160, 100, 30, 255))
    return rounded_corners(c)


def deck_icon(kind):
    """48x64, marrom, pilha de cartas. kind 0 = estrela (maiores), 1 = moeda/pentaculo (menores)."""
    W, H = 48, 64
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    edge = [(62, 34, 18, 255), (74, 42, 22, 255), (86, 50, 26, 255)]
    for i, off in enumerate((3, 2, 1)):          # camadas de baixo
        d.rectangle([off, off, off + 43, off + 59], fill=edge[i], outline=OUTLINE)
    d.rectangle([0, 0, 43, 59], fill=(214, 160, 60, 255), outline=OUTLINE)
    d.rectangle([2, 2, 41, 57], fill=(112, 64, 34, 255))
    px = img.load()
    for y in range(3, 57):
        for x in range(3, 41):
            if ((x + y) % 6 == 0) or ((x - y) % 6 == 0):
                px[x, y] = (128, 76, 40, 255)
    cx, cy = 22, 30
    if kind == 0:
        d.polygon(star_points(cx, cy, 13, 6), fill=GOLD, outline=(160, 100, 30, 255))
    else:
        d.ellipse([cx - 11, cy - 11, cx + 11, cy + 11], outline=GOLD, width=2)
        d.polygon(star_points(cx, cy, 9, 3.5, 5), outline=GOLD, fill=None)
    for (x, y) in ((0, 0), (43, 0), (0, 59), (43, 59)):
        px[x, y] = (0, 0, 0, 0)
    return img


def make_icon(back):
    icon = Image.new("RGBA", (48, 48), (122, 20, 34, 255))
    px = icon.load()
    rnd = random.Random(3)
    for y in range(48):
        for x in range(48):
            if rnd.random() < 0.15:
                px[x, y] = (102, 13, 26, 255)
    card = back.resize((28, 48), Image.Resampling.NEAREST)
    icon.alpha_composite(card, (10, 0))
    return icon


# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="build_assets")
    ap.add_argument("--cache", default=None)
    ap.add_argument("--src", default="cards_src")
    ap.add_argument("--offline", action="store_true", help="não baixa nada (usa só cards_src/cache)")
    ap.add_argument("--allow-missing", action="store_true", help="usa placeholder sem falhar o build")
    a = ap.parse_args()
    global OFFLINE
    OFFLINE = a.offline
    cache = a.cache or os.path.join(a.out, "cache")
    os.makedirs(a.out, exist_ok=True)

    atlas = Image.new("RGBA", (1024, 1024), (0, 0, 0, 0))
    missing = []
    for cid in range(78):
        raw, how = fetch_card(cid, cache, a.src)
        if raw is None:
            card = placeholder(cid)
            missing.append(cid)
        else:
            card = pixelate(raw)
        atlas.paste(card, ((cid % COLS) * CARD_W, (cid // COLS) * CARD_H))
        print("carta %02d  %-9s %s" % (cid, how, card_basename(cid)), flush=True)
    atlas.save(os.path.join(a.out, "cards.png"))

    ui = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
    back = card_back()
    ui.paste(back, (0, 0))
    ui.paste(felt_tile(), (64, 0))
    ui.paste(deck_icon(0), (96, 0))
    ui.paste(deck_icon(1), (144, 0))
    ui.save(os.path.join(a.out, "ui.png"))

    if not os.path.exists("icon.png"):
        make_icon(back).save("icon.png")

    print("\nOK: %d/78 cartas reais, %d placeholders %s" % (78 - len(missing), len(missing), missing))
    if missing and not a.allow_missing:
        sys.exit(1)


if __name__ == "__main__":
    main()
