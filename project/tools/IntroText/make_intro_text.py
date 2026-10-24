"""文字カード（ステージ開始演出の目標・ゲームオーバー）の文字画像を書き出す

エンジンのフォントは ASCII しか焼いていないので、日本語の文字は画像にして Sprite で出す。
M PLUS 1p Black で文字を描き、上から下への色のグラデーションと、縁のうっすらした光をつける。

使い方（project/ から）:
  python tools/IntroText/make_intro_text.py            すべて書き出す
  python tools/IntroText/make_intro_text.py gameover_title gameover_result   名前を指定した物だけ書き出す
出力: TEXTS の書き出し先の *.png（等倍で描くので、画像の大きさ = 画面上の px）
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[2]
FONT_PATH = ROOT / "Application/resources/Fonts/MPLUS1p-Black.ttf"
INTRO_DIR = ROOT / "Application/resources/UI/StageIntro"
GAME_OVER_DIR = ROOT / "Application/resources/UI/GameOver"

# 文字の色: (上の色, 下の色, 縁の光の色)。上 → 下のグラデーション
GOLD = ((255, 247, 220), (232, 196, 110), (255, 214, 130))  # 淡い金色（出撃）
# 赤（GAME OVER）。下の色は、見下ろしの暗い雲海（平均の明るさ 57）より 40 以上明るくしてある
RED = ((255, 150, 130), (230, 72, 58), (255, 70, 50))
FADED_GOLD = ((214, 206, 186), (160, 140, 100), (190, 170, 120))  # 金の彩度と明るさを落とした色（奪還ならず）
# 縁の光の強さ（0〜1）
GLOW_STRENGTH = 0.55

# 書き出す文字: (書き出し先, ファイル名, 文字, 文字の大きさ px, 字間（文字の大きさに対する割合）, 縁の光のぼかし半径 px, 色)
TEXTS = [
    (INTRO_DIR, "stage_name", "第一空域", 30, 0.45, 4, GOLD),
    (INTRO_DIR, "objective", "天空を奪還せよ", 60, 0.12, 8, GOLD),
    (GAME_OVER_DIR, "gameover_title", "GAME OVER", 76, 0.28, 10, RED),
    (GAME_OVER_DIR, "gameover_result", "第一空域　奪還ならず", 24, 0.36, 4, FADED_GOLD),
]


def render_text(text, size, spacing, glow_radius, colors):
    top_color, bottom_color, glow_color = colors
    font = ImageFont.truetype(str(FONT_PATH), size)
    gap = round(size * spacing)

    # 1文字ずつの幅を足して全体の幅を出す（最後の文字の後ろには字間を入れない）
    widths = [font.getbbox(ch)[2] for ch in text]
    ascent, descent = font.getmetrics()
    text_w = sum(widths) + gap * (len(text) - 1)
    text_h = ascent + descent

    # 縁の光がはみ出す分の余白
    pad = glow_radius * 3
    w, h = text_w + pad * 2, text_h + pad * 2

    # 文字の形（白黒のマスク）
    mask = Image.new("L", (w, h), 0)
    draw = ImageDraw.Draw(mask)
    x = pad
    for ch, cw in zip(text, widths):
        draw.text((x, pad), ch, font=font, fill=255)
        x += cw + gap

    # 文字の色：上から下へのグラデーション（文字の高さの範囲で）
    gradient = Image.new("RGB", (1, h))
    for y in range(h):
        t = min(max((y - pad) / text_h, 0.0), 1.0)
        gradient.putpixel((0, y), tuple(round(a + (b - a) * t) for a, b in zip(top_color, bottom_color)))
    gradient = gradient.resize((w, h))

    # 縁の光：文字の形をぼかしたものを、光の色で下に敷く
    glow_alpha = mask.filter(ImageFilter.GaussianBlur(glow_radius)).point(lambda v: round(v * GLOW_STRENGTH))
    out = Image.new("RGBA", (w, h), glow_color + (0,))
    out.putalpha(glow_alpha)
    body = gradient.convert("RGBA")
    body.putalpha(mask)
    return Image.alpha_composite(out, body)


def main():
    names = sys.argv[1:]
    for out_dir, name, text, size, spacing, glow, colors in TEXTS:
        if names and name not in names:
            continue
        out_dir.mkdir(parents=True, exist_ok=True)
        image = render_text(text, size, spacing, glow, colors)
        path = out_dir / f"{name}.png"
        image.save(path)
        print(f"{path.relative_to(ROOT)}  {image.width}x{image.height}")


if __name__ == "__main__":
    main()
