"""雑魚敵 2 種（A 単眼・B くさび）の体と光の部品を組み立て、エンジン用の .gltf に書き出す。

使い方（Blender のバックグラウンド実行）:
    blender -b --factory-startup --python build_zako_models.py -- <出力フォルダ>

書き出すファイル（どれも .gltf + .bin、テクスチャなし）:
    zako_eye_body    A 単眼の体（角ばった丸い殻。正面が平ら）
    zako_eye_light   A 単眼の目（正面の平らな所に乗る 12 角の板）
    zako_arrow_body  B くさびの体（断面が三角の矢じり）
    zako_arrow_light B くさびの光（後ろの推進器と、鼻先の下のセンサーの目を 1 つのメッシュにまとめたもの）

形の決まり:
    - 体は凸包で作るので、へこみがなく閉じている（メッシュ破砕の条件）
    - 座標はエンジン（glTF）の向きで組み立てる: 前 +Z・上 +Y。Blender へは (x, -z, y) で入れる
    - 体を囲む箱の中心を原点にし、いちばん長い辺を 1 にそろえる（プレハブの scale 6 で 6m 角になる）
    - 光の部品は体と同じ座標で作り、体と同じ移動と拡大をかける（体と同じ行列で描けば重なる）
"""
import bpy
import bmesh
import math
import os
import sys

from mathutils import Vector

# ---- A 単眼 ----
# 殻の半径・経線の数・緯線の数（両極を含まない輪の数 + 1）
kEyeShellRadius = 0.5
kEyeShellSegments = 12
kEyeShellRings = 8
# 殻の上下のつぶし具合（Y に掛ける）
kEyeShellSquashY = 0.9
# 正面を平らに切る位置（Z）。これより前の点は切り口の輪に置きかえる
kEyeFrontCutZ = 0.34
# 目の板の半径・角の数・厚み・正面の面から前に出す量
kEyeLightRadius = 0.17
kEyeLightSides = 12
kEyeLightDepth = 0.035
kEyeLightProtrude = 0.02

# ---- B くさび ----
# 後ろの断面（X, Y）。底の左右の角と、上のとがりを少し面取りした 6 角形（上がとがった三角）
kArrowBackSection = [
    (-0.50, -0.06),
    (-0.46, -0.10),
    (0.46, -0.10),
    (0.50, -0.06),
    (0.06, 0.20),
    (-0.06, 0.20),
]
# 後ろの面の Z と、鼻先の Z
kArrowBackZ = -0.5
kArrowNoseZ = 0.5
# 鼻先へ向かって断面を細くする段の数（鼻先の 1 点は含まない）
kArrowSectionCount = 9
# 断面の幅と高さを細くしていく曲がり具合（1 で直線。1 より小さいほど外へふくらむ）
kArrowWidthExponent = 0.85
kArrowHeightExponent = 0.7
# 鼻先の高さ（断面の中心の Y が、後ろの 0 からここまで下がる）
kArrowNoseY = -0.04
# 推進器の板: 後ろの断面に対する大きさ・後ろへ出す量・体にめりこませる量
kArrowThrusterScale = 0.55
kArrowThrusterDepth = 0.03
kArrowThrusterInset = 0.005
# センサーの目: 置く場所（後ろ 0〜鼻先 1 の割合）・半径・底から下へ出す量
kArrowSensorAt = 0.7
kArrowSensorRadius = 0.06
kArrowSensorDrop = 0.01

# 書き出したモデルのいちばん長い辺
kTargetExtent = 1.0


def to_blender(p):
    """エンジン（glTF）の座標を Blender の座標にする（glTF 書き出しの +Y 上の変換の逆）"""
    return Vector((p[0], -p[2], p[1]))


def build_hull_mesh(name, points):
    """点の集まりの凸包で、閉じた三角形のメッシュを作る"""
    bm = bmesh.new()
    for p in points:
        bm.verts.new(to_blender(p))
    result = bmesh.ops.convex_hull(bm, input=bm.verts, use_existing_faces=False)
    # 凸包の内側に残った点（面に使われなかった点）を消す
    interior = [v for v in result["geom_interior"] if isinstance(v, bmesh.types.BMVert)]
    bmesh.ops.delete(bm, geom=interior, context='VERTS')
    unused = [v for v in bm.verts if not v.link_faces]
    bmesh.ops.delete(bm, geom=unused, context='VERTS')
    bmesh.ops.triangulate(bm, faces=bm.faces)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    return mesh


def join_meshes(name, meshes):
    """いくつかのメッシュを 1 つのメッシュにまとめる（エンジンは 1 ファイル 1 メッシュだけ読むため）"""
    bm = bmesh.new()
    for mesh in meshes:
        bm.from_mesh(mesh)
    joined = bpy.data.meshes.new(name)
    bm.to_mesh(joined)
    bm.free()
    for mesh in meshes:
        bpy.data.meshes.remove(mesh)
    return joined


def prism_points(outline, z_front, z_back):
    """断面の形（X, Y の列）を Z の 2 か所に置いた点の列（角柱の頂点）"""
    return [(x, y, z) for z in (z_front, z_back) for (x, y) in outline]


def regular_polygon(radius, sides, center=(0.0, 0.0), squash_y=1.0):
    """正多角形の頂点（X, Y の列）"""
    return [
        (center[0] + radius * math.cos(2.0 * math.pi * i / sides),
         center[1] + radius * squash_y * math.sin(2.0 * math.pi * i / sides))
        for i in range(sides)
    ]


def octahedron_points(center, radius):
    """正八面体の頂点"""
    cx, cy, cz = center
    return [
        (cx + radius, cy, cz), (cx - radius, cy, cz),
        (cx, cy + radius, cz), (cx, cy - radius, cz),
        (cx, cy, cz + radius), (cx, cy, cz - radius),
    ]


def build_eye():
    """A 単眼の体と目の点を作る"""
    r = kEyeShellRadius
    body = []
    # 上下の極と、緯線の輪（極を Y に向けた経緯度の球）
    body.append((0.0, r * kEyeShellSquashY, 0.0))
    body.append((0.0, -r * kEyeShellSquashY, 0.0))
    for ring in range(1, kEyeShellRings):
        latitude = math.pi * ring / kEyeShellRings - math.pi * 0.5
        for seg in range(kEyeShellSegments):
            longitude = 2.0 * math.pi * seg / kEyeShellSegments
            x = r * math.cos(latitude) * math.sin(longitude)
            y = r * math.sin(latitude) * kEyeShellSquashY
            z = r * math.cos(latitude) * math.cos(longitude)
            if z <= kEyeFrontCutZ:
                body.append((x, y, z))
    # 正面の切り口の輪（球と平面 Z = kEyeFrontCutZ の交わり）
    cut_radius = math.sqrt(r * r - kEyeFrontCutZ * kEyeFrontCutZ)
    for (x, y) in regular_polygon(cut_radius, kEyeShellSegments * 2, squash_y=kEyeShellSquashY):
        body.append((x, y, kEyeFrontCutZ))

    # 目: 正面の平らな面に、少しめりこませて前へ出した板
    eye_outline = regular_polygon(kEyeLightRadius, kEyeLightSides)
    light = [prism_points(eye_outline,
                          kEyeFrontCutZ + kEyeLightProtrude,
                          kEyeFrontCutZ + kEyeLightProtrude - kEyeLightDepth)]
    return body, light


def arrow_section(s):
    """B くさびの、後ろ 0〜鼻先 1 の割合 s の位置の断面（X, Y, Z の列）"""
    width = (1.0 - s) ** kArrowWidthExponent
    height = (1.0 - s) ** kArrowHeightExponent
    center_y = kArrowNoseY * s
    z = kArrowBackZ + (kArrowNoseZ - kArrowBackZ) * s
    return [(x * width, center_y + y * height, z) for (x, y) in kArrowBackSection]


def build_arrow():
    """B くさびの体と光（推進器・センサーの目）の点を作る"""
    body = []
    for i in range(kArrowSectionCount):
        body.extend(arrow_section(i / kArrowSectionCount))
    body.append((0.0, kArrowNoseY, kArrowNoseZ))

    # 推進器: 後ろの断面を中心へ縮めた形の板を、後ろの面から後ろへ出す
    cx = sum(p[0] for p in kArrowBackSection) / len(kArrowBackSection)
    cy = sum(p[1] for p in kArrowBackSection) / len(kArrowBackSection)
    thruster_outline = [(cx + (x - cx) * kArrowThrusterScale, cy + (y - cy) * kArrowThrusterScale)
                        for (x, y) in kArrowBackSection]
    thruster = prism_points(thruster_outline,
                            kArrowBackZ + kArrowThrusterInset,
                            kArrowBackZ - kArrowThrusterDepth)

    # センサーの目: 鼻先寄りの底の中央に、下へ少し出した正八面体
    section = arrow_section(kArrowSensorAt)
    bottom_y = min(p[1] for p in section)
    sensor_center = (0.0, bottom_y - kArrowSensorDrop, section[0][2])
    sensor = octahedron_points(sensor_center, kArrowSensorRadius)
    return body, [thruster, sensor]


def normalize_transform(points):
    """囲む箱の中心を原点へ移し、いちばん長い辺を kTargetExtent にする変換（移動量・倍率）"""
    lo = [min(p[i] for p in points) for i in range(3)]
    hi = [max(p[i] for p in points) for i in range(3)]
    center = [(lo[i] + hi[i]) * 0.5 for i in range(3)]
    extent = max(hi[i] - lo[i] for i in range(3))
    return center, kTargetExtent / extent


def apply_transform(points, center, scale):
    return [tuple((p[i] - center[i]) * scale for i in range(3)) for p in points]


def mesh_volume(mesh):
    """閉じたメッシュの体積（原点と各三角形がつくる四面体の符号付き体積の合計）"""
    volume = 0.0
    for poly in mesh.polygons:
        a, b, c = (mesh.vertices[i].co for i in poly.vertices)
        volume += a.dot(b.cross(c)) / 6.0
    return volume


def is_closed(mesh):
    """どの辺もちょうど 2 つの面に使われているか"""
    edge_faces = {}
    for poly in mesh.polygons:
        for key in poly.edge_keys:
            edge_faces[key] = edge_faces.get(key, 0) + 1
    return all(count == 2 for count in edge_faces.values())


def is_convex(mesh, tolerance=1e-5):
    """すべての頂点が、どの面の平面よりも外側に出ていないか"""
    for poly in mesh.polygons:
        n = poly.normal
        d = n.dot(mesh.vertices[poly.vertices[0]].co)
        for v in mesh.vertices:
            if n.dot(v.co) - d > tolerance:
                return False
    return True


def report(name, mesh, check_convex):
    xs = [v.co.x for v in mesh.vertices]
    ys = [v.co.y for v in mesh.vertices]
    zs = [v.co.z for v in mesh.vertices]
    # 表示はエンジンの軸（幅 X・高さ Y・長さ Z）に直す
    convex = str(is_convex(mesh)) if check_convex else "-"
    print("[zako] %-17s 三角形 %3d  幅 %.3f 高さ %.3f 長さ %.3f  体積 %.4f  閉じている %s  凸 %s" % (
        name, len(mesh.polygons),
        max(xs) - min(xs), max(zs) - min(zs), max(ys) - min(ys),
        mesh_volume(mesh), is_closed(mesh), convex))


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


def build_and_export(output_dir, name, body_points, light_parts):
    center, scale = normalize_transform(body_points)
    body = build_hull_mesh(name + "_body", apply_transform(body_points, center, scale))
    lights = [build_hull_mesh(name + "_light_part", apply_transform(part, center, scale))
              for part in light_parts]
    light = join_meshes(name + "_light", lights)
    # 光の部品はいくつかの形をまとめたもので割らないので、凸かどうかは調べない
    report(name + "_body", body, True)
    report(name + "_light", light, False)
    # 光の部品ごとの中心（プレハブの burstPoints に書く、撃破の光を出す場所）
    for part in light_parts:
        moved = apply_transform(part, center, scale)
        c = [sum(p[i] for p in moved) / len(moved) for i in range(3)]
        print("[zako] %-17s 光の部品の中心 [%.3f, %.3f, %.3f]" % (name + "_light", c[0], c[1], c[2]))
    export_mesh(body, output_dir, name + "_body")
    export_mesh(light, output_dir, name + "_light")


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if not argv:
        print("出力フォルダを指定してください: blender -b --factory-startup --python build_zako_models.py -- <出力フォルダ>")
        return
    output_dir = os.path.abspath(argv[0])
    os.makedirs(output_dir, exist_ok=True)

    # 起動時のシーンにある物を消す
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj)

    eye_body, eye_light = build_eye()
    build_and_export(output_dir, "zako_eye", eye_body, eye_light)

    arrow_body, arrow_light = build_arrow()
    build_and_export(output_dir, "zako_arrow", arrow_body, arrow_light)


main()
