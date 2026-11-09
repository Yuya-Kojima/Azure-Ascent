"""区間2の岩の柱（機械に侵された塔）と背景の岩を組み立て、エンジン用の .gltf に書き出す。

使い方（Blender のバックグラウンド実行）:
    blender -b --factory-startup --python build_rock_towers.py -- <出力フォルダ>

書き出すファイル（どれも .gltf + .bin、テクスチャ・マテリアルなし）:
    rock_tower_s<段階>_<a〜c>_rock     岩の柱
    rock_tower_s<段階>_<a〜c>_machine  機械の部品（輪・板・配管・割れ目の板・置き換わった塔）
    rock_tower_s<段階>_<a〜c>_light    灯り（縦一列の灯りと、割れ目の光の筋）
    rock_small_<1〜>                   背景の岩（機械なし）
    段階 s0〜s4 は侵され具合 0・25・50・75・100%。s0 は岩だけ

侵され具合:
    s1 割れ目が光り、上の方に輪がはまる
    s2 輪が増え、割れ目から機械の板が突き出す
    s3 上が折れて機械の塔に置き換わり始め、残った岩に輪が残る
    s4 ほぼ機械の塔。根元に岩が残る

形の決まり:
    - 座標はエンジン（glTF）の向きで組み立てる: 上 +Y・前 +Z。Blender へは (x, -z, y) で入れる
    - 大きさは m のまま書き出す。scale 1 で、幅 約 16〜24m・雲海から上 約 130m の塔になる
    - 原点は雲海の面の高さ（Y = 0）の、岩の柱の中心。岩は雲海の下 kBottom まで埋まる。
      1 本の塔の 3 つの部品は同じ原点なので、同じ位置・向き・大きさで置くと重なる
    - 灯りは塔の ±X と -Z の 3 面に縦一列で置く。どちら側に立てても、レールから見える面に灯りがある
"""
import bpy
import bmesh
import math
import os
import random
import sys

from mathutils import Vector, noise

# 塔の雲海から上の高さ・幅・雲海の下に埋まる深さ（m）
kHeight = 130.0
kWidth = 16.0
kBottom = -20.0

# 岩の柱: 周りの角の数・高さ方向の輪の数・上へ向かって細くなる割合・でこぼこの強さ
kRockSides = 14
kRockRings = 40
kRockTaper = 0.45
kRockBump = 0.28
kRockBumpFine = 0.12
# 層の段: 高さ方向の段の数・段の張り出し・段の厚み（段の間隔に対する割合）
kRockLedgeCount = 9
kRockLedgeOut = 0.10
kRockLedgeThickness = 0.18
# 高さごとの中心のずれ（幅に対する割合）
kRockLean = 0.15
# 天辺の丸め: 始める高さ（割合）と、細くなる速さ
kRockTipStart = 0.92
kRockTipSharpness = 9.0

# 輪: 上下の間隔（m）・厚み・高さ・角の数・岩からの離れ具合
kRingGapMin = 14.0
kRingGapMax = 22.0
kRingThickness = 1.2
kRingHeight = 1.8
kRingSides = 8
kRingOut = 1.15
# 縦の板の数・配管の 1 区切りの長さ（m）・灯りの上下の間隔（m）
kPanelCount = 5
kPipeStep = 3.0
kLightStep = 9.0
# 灯りを置く向き（ラジアン、+X から +Z へ回る向き）: +X・-X・-Z
kLightFacings = (0.0, math.pi, -math.pi * 0.5)

# 割れ目の光の筋: 幅・奥行き・長さ（m）
kCrackWidth = 0.9
kCrackDepth = 0.6
kCrackLength = 11.0

# 段階ごとの中身（侵され具合 0〜1）
kLevels = (0.0, 0.25, 0.5, 0.75, 1.0)
kVariants = "abc"
# 上が置き換わり始める侵され具合と、侵され具合 1 のときに機械に置き換わる高さの割合
kReplaceFrom = 0.55
kReplaceDepth = 0.7

# 背景の岩: (高さ m, 幅 m, 乱数)
kSmallRocks = (
    (70.0, 12.0, 101),
    (85.0, 16.0, 102),
    (55.0, 14.0, 103),
    (100.0, 13.0, 104),
    (60.0, 18.0, 105),
)


def to_blender(p):
    """エンジン（glTF）の座標を Blender の座標にする（glTF 書き出しの +Y 上の変換の逆）"""
    return Vector((p[0], -p[2], p[1]))


class Builder:
    """点と四角形・多角形の面を、エンジンの座標のまま集める"""

    def __init__(self):
        self.points = []
        self.faces = []

    def add_point(self, p):
        self.points.append(tuple(p))
        return len(self.points) - 1

    def add_face(self, indices):
        self.faces.append(tuple(indices))

    def add_box(self, center, size, yaw, tilt=0.0):
        """中心・大きさ（横 X, 高さ Y, 奥 Z）の箱。tilt で外（+X）へ倒し、yaw で Y 軸まわりに回す"""
        corners = []
        for sx in (-0.5, 0.5):
            for sy in (-0.5, 0.5):
                for sz in (-0.5, 0.5):
                    x, y, z = sx * size[0], sy * size[1], sz * size[2]
                    x, y = x * math.cos(tilt) + y * math.sin(tilt), -x * math.sin(tilt) + y * math.cos(tilt)
                    x, z = x * math.cos(yaw) - z * math.sin(yaw), x * math.sin(yaw) + z * math.cos(yaw)
                    corners.append(self.add_point((center[0] + x, center[1] + y, center[2] + z)))
        c = corners
        # 角の番号は (sx, sy, sz) の 2 進数。面の向きは to_mesh で外向きにそろえる
        for quad in ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)):
            self.add_face([c[i] for i in quad])


def radial(a):
    """Y 軸まわりの角度 a の外向き（X, Z）"""
    return math.cos(a), math.sin(a)


class RockShape:
    """岩の柱の太さと中心のずれ。高さ top までの柱を、周りの角度と高さの割合で表す"""

    def __init__(self, top, width, seed):
        self.top = top
        self.width = width
        self.seed = Vector((seed * 7.1, seed * 3.3, 0.0))

    def base_radius(self, u):
        return self.width * 0.5 * (1.0 - kRockTaper * u ** 1.5)

    def radius(self, u, a):
        q = Vector((math.cos(a) * 1.3, math.sin(a) * 1.3, u * 6.0)) + self.seed
        bumpy = 1.0 + kRockBump * noise.noise(q) + kRockBumpFine * noise.noise(q * 3.0)
        ledge = 1.0 + (kRockLedgeOut if (u * kRockLedgeCount) % 1.0 < kRockLedgeThickness else 0.0)
        tip = 1.0
        if u >= kRockTipStart:
            tip = max(0.25, 1.0 - (u - kRockTipStart) * kRockTipSharpness * (0.6 + 0.4 * noise.noise(q * 2.0)))
        return self.base_radius(u) * bumpy * ledge * tip

    def offset(self, u):
        ox = self.width * kRockLean * noise.noise(Vector((u * 2.0, 0.0, 0.0)) + self.seed)
        oz = self.width * kRockLean * noise.noise(Vector((0.0, u * 2.0, 5.0)) + self.seed)
        return ox, oz

    def u_of(self, y):
        return min(max((y - kBottom) / (self.top - kBottom), 0.0), 0.95)

    def surface(self, y):
        """高さ y の、ならした太さと中心（X, Z）"""
        u = self.u_of(y)
        ox, oz = self.offset(u)
        return self.base_radius(u), ox, oz

    def build(self, b):
        """周りを kRockSides 角、高さを kRockRings 段に分けた、上下にふたのある閉じた柱"""
        loops = []
        for i in range(kRockRings + 1):
            u = i / kRockRings
            y = kBottom + (self.top - kBottom) * u
            ox, oz = self.offset(u)
            loop = []
            for s in range(kRockSides):
                a = 2.0 * math.pi * s / kRockSides
                r = self.radius(u, a)
                cx, cz = radial(a)
                loop.append(b.add_point((ox + r * cx, y, oz + r * cz)))
            loops.append(loop)
        for i in range(kRockRings):
            for s in range(kRockSides):
                t = (s + 1) % kRockSides
                b.add_face([loops[i][s], loops[i + 1][s], loops[i + 1][t], loops[i][t]])
        b.add_face(loops[-1])
        b.add_face(list(reversed(loops[0])))


def add_wrap(mach, light, rock, top, cover, rnd):
    """A まとわりつく: 上から cover の割合を、輪・縦の板・配管・縦一列の灯りが覆う"""
    y_from = kBottom + (top - kBottom) * (1.0 - cover)
    y_to = top * 0.9
    y = y_from + 4.0
    while y < y_to:
        r, ox, oz = rock.surface(y)
        rr = r * kRingOut
        for s in range(kRingSides):
            a = 2.0 * math.pi * (s + 0.5) / kRingSides
            cx, cz = radial(a)
            side = 2.0 * rr * math.tan(math.pi / kRingSides) + 0.3
            mach.add_box((ox + rr * cx, y, oz + rr * cz), (kRingThickness, kRingHeight, side), a)
        y += rnd.uniform(kRingGapMin, kRingGapMax)
    for _ in range(kPanelCount):
        a = rnd.uniform(0.0, 2.0 * math.pi)
        h = rnd.uniform(12.0, 26.0)
        if y_to - h * 0.5 <= y_from + h * 0.5:
            break
        yc = rnd.uniform(y_from + h * 0.5, y_to - h * 0.5)
        r, ox, oz = rock.surface(yc)
        cx, cz = radial(a)
        mach.add_box((ox + r * 1.08 * cx, yc, oz + r * 1.08 * cz), (0.8, h, r * 0.7), a)
    for k in (-1.0, 1.0):
        a = rnd.uniform(0.0, 2.0 * math.pi) + k * 0.9
        y = y_from
        while y < y_to:
            r, ox, oz = rock.surface(y)
            cx, cz = radial(a)
            mach.add_box((ox + r * 1.2 * cx, y + kPipeStep * 0.5, oz + r * 1.2 * cz), (0.9, kPipeStep + 0.1, 0.9), a)
            y += kPipeStep
    for a in kLightFacings:
        y = y_from + 4.0
        while y < y_to:
            r, ox, oz = rock.surface(y)
            cx, cz = radial(a)
            light.add_box((ox + r * 1.22 * cx, y, oz + r * 1.22 * cz), (0.6, 1.4, 1.0), a)
            y += kLightStep


def add_cracks(mach, light, rock, top, count, plates, rnd):
    """C 割れ目から生える: 岩の表面に光の筋。plates なら、そこから機械の板が外へ突き出す"""
    for k in range(count):
        a = rnd.choice(kLightFacings) + rnd.uniform(-0.6, 0.6) if k % 3 else rnd.uniform(0.0, 2.0 * math.pi)
        y = rnd.uniform(top * 0.25, top * 0.85)
        r, ox, oz = rock.surface(y)
        size = rnd.uniform(0.8, 1.3)
        cx, cz = radial(a)
        center = (ox + r * 1.02 * cx, y, oz + r * 1.02 * cz)
        light.add_box(center, (kCrackDepth, kCrackLength * size, kCrackWidth), a, rnd.uniform(-0.25, 0.25))
        if plates:
            mach.add_box((center[0], y - 2.0, center[2]), (r * 0.8 * size, 8.0 * size, 2.0 * size), a, rnd.uniform(0.2, 0.6))


def add_replacement(mach, light, rock, cut, top, rnd):
    """B 上が置き換わる: 折れ口をつかむ爪と、段ごとに細くなる機械の塔（少し傾く）"""
    y0 = cut * 0.9
    r, ox, oz = rock.surface(y0)
    r *= 0.95
    for k in range(6):
        a = 2.0 * math.pi * k / 6.0 + rnd.uniform(-0.3, 0.3)
        cx, cz = radial(a)
        mach.add_box((ox + r * 0.95 * cx, cut * 0.88, oz + r * 0.95 * cz), (1.4, rnd.uniform(9.0, 16.0), 2.2), a, -0.25)
    lean_x = rnd.uniform(-0.08, 0.08)
    lean_z = rnd.uniform(-0.08, 0.08)
    count = max(1, int((top - y0) / rnd.uniform(11.0, 17.0)))
    w0 = r * rnd.uniform(1.4, 1.8)
    y = y0
    for i in range(count):
        h = (top - y0) / count
        w = w0 * (1.0 - 0.55 * i / count)
        yc = y + h * 0.5
        cx, cz = ox + lean_x * (yc - y0), oz + lean_z * (yc - y0)
        yaw = math.pi * 0.25 * (i % 2) + rnd.uniform(-0.1, 0.1)
        mach.add_box((cx, yc, cz), (w, h * 0.92, w * rnd.uniform(0.8, 1.0)), yaw)
        for a in kLightFacings:
            dx, dz = radial(a)
            light.add_box((cx + w * 0.5 * dx, yc, cz + w * 0.5 * dz), (0.6, 1.4, 1.0), a)
        y += h
    # 天辺のアンテナ（一番上の段の上面から立てる）
    tip_x, tip_z = ox + lean_x * (top - y0), oz + lean_z * (top - y0)
    mach.add_box((tip_x, top + 5.0, tip_z), (0.6, 12.0, 0.6), 0.0)


def build_tower(level, seed):
    """侵され具合 level の塔を、岩の形と、岩・機械・灯りの 3 つの Builder で返す"""
    rnd = random.Random(seed * 31 + int(level * 100))
    replace = max(0.0, (level - kReplaceFrom) / (1.0 - kReplaceFrom))
    cut = kHeight * (1.0 - kReplaceDepth * replace * rnd.uniform(0.8, 1.1)) if replace > 0.0 else kHeight
    rock_shape = RockShape(cut, kWidth, seed)
    rock, mach, light = Builder(), Builder(), Builder()
    rock_shape.build(rock)
    if level > 0.0:
        count = max(2, int(round(3 + 6 * min(level, 0.6) - 4 * replace)))
        add_cracks(mach, light, rock_shape, cut, count, level >= 0.5, rnd)
    if level >= 0.25:
        cover = min(0.65, max(0.3, level * 0.8)) * rnd.uniform(0.8, 1.1)
        if replace > 0.0:
            cover = rnd.uniform(0.25, 0.45)
        add_wrap(mach, light, rock_shape, cut, cover, rnd)
    if replace > 0.0:
        add_replacement(mach, light, rock_shape, cut, kHeight * rnd.uniform(0.95, 1.0), rnd)
    return rock_shape, rock, mach, light


def origin_of(rock_shape):
    """雲海の面の高さ（Y = 0）の、岩の柱の中心"""
    ox, oz = rock_shape.offset(rock_shape.u_of(0.0))
    return (ox, 0.0, oz)


def bounds(builders):
    pts = [p for b in builders for p in b.points]
    lo = [min(p[i] for p in pts) for i in range(3)]
    hi = [max(p[i] for p in pts) for i in range(3)]
    return lo, hi


def to_mesh(name, builder, origin):
    """原点 origin を (0, 0, 0) へ移して、三角形のメッシュにする"""
    bm = bmesh.new()
    verts = []
    for p in builder.points:
        q = [p[i] - origin[i] for i in range(3)]
        verts.append(bm.verts.new(to_blender(q)))
    for f in builder.faces:
        bm.faces.new([verts[i] for i in f])
    bmesh.ops.triangulate(bm, faces=bm.faces)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    return mesh


def is_closed(mesh):
    """どの辺もちょうど 2 つの面に使われているか"""
    edge_faces = {}
    for poly in mesh.polygons:
        for key in poly.edge_keys:
            edge_faces[key] = edge_faces.get(key, 0) + 1
    return all(count == 2 for count in edge_faces.values())


def report(name, mesh):
    xs = [v.co.x for v in mesh.vertices]
    ys = [v.co.y for v in mesh.vertices]
    zs = [v.co.z for v in mesh.vertices]
    # 表示はエンジンの軸（X・高さ Y・奥 Z）に直す
    print("[rock] %-26s 三角形 %5d  X %6.1f〜%5.1f  Y %6.1f〜%5.1f  Z %6.1f〜%5.1f m  閉じている %s" % (
        name, len(mesh.polygons), min(xs), max(xs), min(zs), max(zs), -max(ys), -min(ys), is_closed(mesh)))


def export_mesh(mesh, output_dir, name):
    """メッシュ 1 つを、原点に置いたオブジェクトとして .gltf + .bin に書き出す"""
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    for o in bpy.context.scene.objects:
        o.select_set(o == obj)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.gltf(
        filepath=os.path.join(output_dir, name + ".gltf"),
        export_format='GLTF_SEPARATE',
        use_selection=True,
        export_yup=True,
        export_apply=True,
        export_normals=True,
        export_texcoords=False,
        export_materials='NONE',
        export_animations=False,
    )
    bpy.data.objects.remove(obj)


def export_set(output_dir, name, parts, origin):
    """部品（名前の末尾, Builder）を同じ原点で、空でない物だけ書き出す"""
    used = [(suffix, b) for suffix, b in parts if b.faces]
    lo, hi = bounds([b for _, b in used])
    print("[rock] %-26s 全体の大きさ 幅 %.1f 高さ %.1f 奥 %.1f m" % (
        name, hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]))
    for suffix, b in used:
        full = name + suffix
        mesh = to_mesh(full, b, origin)
        report(full, mesh)
        export_mesh(mesh, output_dir, full)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if not argv:
        print("出力フォルダを指定してください: blender -b --factory-startup --python build_rock_towers.py -- <出力フォルダ>")
        return
    output_dir = os.path.abspath(argv[0])
    os.makedirs(output_dir, exist_ok=True)

    # 起動時のシーンにある物を消す
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj)

    for stage, level in enumerate(kLevels):
        for k, letter in enumerate(kVariants):
            rock_shape, rock, mach, light = build_tower(level, 10 * stage + k + 1)
            export_set(output_dir, "rock_tower_s%d_%s" % (stage, letter),
                       [("_rock", rock), ("_machine", mach), ("_light", light)], origin_of(rock_shape))

    for i, (height, width, seed) in enumerate(kSmallRocks):
        b = Builder()
        shape = RockShape(height, width, seed)
        shape.build(b)
        export_set(output_dir, "rock_small_%d" % (i + 1), [("", b)], origin_of(shape))


main()
