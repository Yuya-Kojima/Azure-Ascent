"""拠点に戻ったあとの UI の、空路の線と印の画像を書き出す

白で描き、色はエンジンで掛ける。印には形のまわりにうっすらした光をつける。
  route_glow    線の光（縦に中心が明るいぼかし。横に伸ばして使う）
  route_diamond ひし形（落ちた所・ボス）
  route_dot     点（出撃）
ロック数のひし形は、暗い縁取りをつけて色を描き込む（明るい背景の上に置く）。
  lock_on       ロックした数（白い中身。赤はエンジンで掛ける）
  lock_off      残り（暗い中身に白い細い縁）

使い方（project/ から）:
  python tools/IntroText/make_route_marks.py
出力: Application/resources/UI/GameOver/*.png、Application/resources/UI/Score/lock_*.png
"""
import math
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
OUT_DIR = ROOT / "Application/resources/UI/GameOver"
SCORE_DIR = ROOT / "Application/resources/UI/Score"
# ロック数のひし形の色（暗い縁取り・残りの中身）
LOCK_OUTLINE = (12, 14, 20)
LOCK_EMPTY_FILL = (24, 28, 40)
# ロック数のひし形の、暗い縁取りと白い縁の太さ（px）
LOCK_OUTLINE_WIDTH = 1.5
LOCK_EDGE_WIDTH = 1.0
# 縁の光の強さ（0〜1）
GLOW_STRENGTH = 0.6
# 形をなめらかに描くための拡大率
SUPERSAMPLE = 4
# ひし形の横幅（縦の長さに対する割合）
DIAMOND_WIDTH = 0.72


def with_glow(mask, glow_radius):
    """白黒の形に、まわりの光を足した白い画像にする"""
    glow = mask.filter(ImageFilter.GaussianBlur(glow_radius)).point(lambda v: round(v * GLOW_STRENGTH))
    # 形の中は形の不透明度、外は光の不透明度
    alpha = ImageChops.lighter(mask, glow)
    out = Image.new("RGBA", mask.size, (255, 255, 255, 0))
    out.putalpha(alpha)
    return out


def draw_shape(size, glow_radius, draw_fn):
    """size 四方の画像の中心に、余白 glow_radius*2 を残して形を描く"""
    big = size * SUPERSAMPLE
    mask = Image.new("L", (big, big), 0)
    draw_fn(ImageDraw.Draw(mask), big, glow_radius * 2 * SUPERSAMPLE)
    mask = mask.resize((size, size), Image.LANCZOS)
    return with_glow(mask, glow_radius)


def diamond(draw, big, pad):
    c = big / 2
    r = c - pad
    draw.polygon([(c, c - r), (c + r * DIAMOND_WIDTH, c), (c, c + r), (c - r * DIAMOND_WIDTH, c)], fill=255)


def dot(draw, big, pad):
    draw.ellipse([pad, pad, big - pad, big - pad], fill=255)


def diamond_mask(size, inset):
    """size 四方の中心に、外周から inset px 内側へ縮めたひし形の形（なめらかに描く）"""
    big = size * SUPERSAMPLE
    mask = Image.new("L", (big, big), 0)
    c = big / 2
    r = c - inset * SUPERSAMPLE
    if r > 0:
        ImageDraw.Draw(mask).polygon(
            [(c, c - r), (c + r * DIAMOND_WIDTH, c), (c, c + r), (c - r * DIAMOND_WIDTH, c)], fill=255)
    return mask.resize((size, size), Image.LANCZOS)


def lock_mark(size, is_on):
    """外側から 暗い縁取り → (残りは白い縁) → 中身 の順に重ねたひし形"""
    layers = [(0.0, LOCK_OUTLINE), (LOCK_OUTLINE_WIDTH, (255, 255, 255))]
    if not is_on:
        layers.append((LOCK_OUTLINE_WIDTH + LOCK_EDGE_WIDTH, LOCK_EMPTY_FILL))
    out = Image.new("RGBA", (size, size), LOCK_OUTLINE + (0,))
    for inset, color in layers:
        layer = Image.new("RGBA", (size, size), color + (0,))
        layer.putalpha(diamond_mask(size, inset))
        out = Image.alpha_composite(out, layer)
    return out


def glow_strip(width, height, sigma):
    """縦の中心がいちばん明るく、上下へなめらかに消える帯"""
    out = Image.new("RGBA", (width, height), (255, 255, 255, 0))
    for y in range(height):
        d = (y + 0.5 - height / 2) / sigma
        a = round(255 * math.exp(-0.5 * d * d))
        for x in range(width):
            out.putpixel((x, y), (255, 255, 255, a))
    return out


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    images = {
        "route_glow": glow_strip(4, 32, 5.0),
        "route_diamond": draw_shape(48, 5, diamond),
        "route_dot": draw_shape(24, 3, dot),
    }
    for name, image in images.items():
        path = OUT_DIR / f"{name}.png"
        image.save(path)
        print(f"{path.relative_to(ROOT)}  {image.width}x{image.height}")
    # ロック数のひし形（15px 四方）
    SCORE_DIR.mkdir(parents=True, exist_ok=True)
    for name, image in {"lock_on": lock_mark(15, True), "lock_off": lock_mark(15, False)}.items():
        path = SCORE_DIR / f"{name}.png"
        image.save(path)
        print(f"{path.relative_to(ROOT)}  {image.width}x{image.height}")


if __name__ == "__main__":
    main()
