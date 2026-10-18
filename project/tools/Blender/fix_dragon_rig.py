"""自機ドラゴン（Tripo のボーン付き GLB）を読み込み、エンジン用の .gltf に書き出し直す。

使い方（Blender のバックグラウンド実行）:
    blender -b --factory-startup --python fix_dragon_rig.py -- <入力.glb> [--export <出力フォルダ>] [--preview <出力フォルダ>]

    --export   .gltf + .bin + テクスチャと、飛行姿勢（滑空・羽ばたき。骨だけ）の .gltf + .bin を書き出す
    --preview  翼・尾・首の骨を回した姿勢をレンダリングし、確認用の .blend を保存する

処理の流れ:
    1. GLB を読み込み、読み込み時に入る姿勢を消す
    2. 別々の向きで入っているメッシュと骨を合わせる
    3. 頭が -Y・上が +Z（glTF では頭が +Z・上が +Y）になるよう回し、底面を 0・前後左右の中心を 0 にそろえる
    4. 翼・首・尾の骨に名前を付ける
    5. 反対側の手足の骨や、前脚と後脚を取り違えた骨に付いている重みを取り除く
"""
import bpy
import os
import sys
from math import radians

import numpy as np
from mathutils import Matrix, Vector

# 1頂点に影響できる骨の最大本数（エンジンの kNumMaxInfluence と同じ）
kMaxInfluence = 4

# 回転を軸の入れ替えに丸めたとき、元の回転との差の許容値（行列成分の差）
kAxisSnapTolerance = 0.05

# 関節まわりとみなす頂点の条件：親子2本の骨の重みの合計と、片方の重みの範囲
kJointWeightSumMin = 0.9
kJointWeightMin = 0.3
kJointWeightMax = 0.7
# 関節の位置を求めるのに使う、関節まわりの頂点の最小数
kJointVertexCountMin = 10

# 骨に合わせた直後（頭が -X・上が +Z）から、頭を -Y へ向ける Z 軸まわりの回転（度）
kYawToEngineDegrees = 90.0

# 書き出すファイルの名前（拡張子なし）と、テクスチャの名前
kOutputName = "player_dragon"
kTextureName = "player_dragon_rig_basecolor"
# 書き出すアーマチュアの名前
kArmatureName = "Armature"
# 飛行姿勢のキーを打つフレーム（同じ姿勢を2フレームに打ち、長さのあるアニメーションにする）
kFlightPoseFrames = (0, 1)

# 翼の骨の Tripo での名前（付け根・腕）。腕の番号の次が手首、その次から指3本×3節が続く
kWingSourceBones = {
    'L': ("tripo::Spine_4", 34),
    'R': ("bone_45", 46),
}
# 尾の骨の Tripo での名前（根元から順に）
kTailSourceBones = ["bone_58", "bone_59", "bone_60", "bone_61"] + [f"tripo::Tail_{i}" for i in range(7)]
# 個別に名前を付ける骨
kOtherRenames = {
    "bone_5": "Neck",
    "bone_11": "Head_5",
    "bone_57": "Hip",
    "bone_69": "TailFin_0",
    "bone_70": "TailFin_1",
}
# 脚の骨の名前の付け替え（Tripo の頭の部分 → 新しい頭の部分）
kLimbPrefixes = {
    "0_Left_Limb_": "FrontLeg_L_",
    "0_Right_Limb_": "FrontLeg_R_",
    "1_Left_Limb_": "HindLeg_L_",
    "1_Right_Limb_": "HindLeg_R_",
}
kTripoPrefix = "tripo::"

# 名前を付けたあとの骨（確認用の姿勢で使う）
kNeckBones = ["Neck", "Head_0", "Head_1", "Head_2", "Head_3"]
kTailBones = [f"Tail_{i:02d}" for i in range(10)]

# 飛行姿勢：(骨の名前, アーマチュア空間の軸, 角度[度]) を上から順に、骨の根元を中心に回す
# {S} は L と R の両方に置き換える。右は Y・Z 軸まわりの向きを反転する（X=0 の面で左と鏡写し）
# 軸の向き（頭が -Y・上が +Z・体の左が +X）：X 軸まわりの正の回転で、下向きの骨は後ろへ、上向きの骨は前へ倒れる
# 滑空と羽ばたきで共通の部分（首と尾）
kFlightBasePose = [
    # 首を前へ倒し、頭を水平に戻す
    ("Neck", 'X', 15.0),
    ("Head_0", 'X', 20.0),
    ("Head_1", 'X', 15.0),
    ("Head_2", 'X', 5.0),
    ("Head_3", 'X', -35.0),
    # 尾：根元を持ち上げ、右（-X）への曲がりを戻して後ろへまっすぐ伸ばす
    ("Tail_00", 'X', 20.0),
    ("Tail_00", 'Z', -8.0),
    ("Tail_01", 'Z', -8.0),
    ("Tail_02", 'Z', -6.0),
    ("Tail_03", 'Z', -6.0),
    ("Tail_04", 'Z', -4.0),
    ("Tail_05", 'X', -5.0),
    ("Tail_06", 'X', -5.0),
    ("Tail_07", 'X', -5.0),
    ("Tail_08", 'X', -5.0),
]
# 滑空：体は水平のまま、前脚を胸の下に畳み、後脚を後ろへ流す
kGlidePose = kFlightBasePose + [
    ("FrontLeg_{S}_1", 'X', 60.0),
    ("FrontLeg_{S}_2", 'X', -100.0),
    ("FrontLeg_{S}_3", 'X', 50.0),
    ("HindLeg_{S}_0", 'X', 70.0),
    ("HindLeg_{S}_1", 'X', -55.0),
    ("HindLeg_{S}_2", 'X', 70.0),
]
# 羽ばたき：体を起こし、前脚を胸の下で軽く曲げ、後脚を下へ垂らす
kHoverPose = [("Root", 'X', -30.0)] + kFlightBasePose + [
    ("FrontLeg_{S}_1", 'X', 10.0),
    ("FrontLeg_{S}_2", 'X', -60.0),
    ("FrontLeg_{S}_3", 'X', 50.0),
    ("HindLeg_{S}_0", 'X', 10.0),
    ("HindLeg_{S}_1", 'X', 20.0),
    ("HindLeg_{S}_2", 'X', 50.0),
]
# 飛行姿勢を書き出すファイルの名前（拡張子なし）、アニメーションの名前、姿勢
kFlightPoseOutputs = [
    ("player_dragon_glide_pose", "GlidePose", kGlidePose),
    ("player_dragon_hover_pose", "HoverPose", kHoverPose),
]


def WingBones(side):
    """名前を付けたあとの翼の骨（部位 → 骨の名前、指は3節のリスト）"""
    prefix = f"Wing_{side}_"
    bones = {"root": prefix + "Root", "arm": prefix + "Arm", "hand": prefix + "Hand"}
    for finger in range(1, 4):
        bones[f"finger{finger}"] = [f"{prefix}Finger{finger}_{joint}" for joint in range(3)]
    return bones


def ParseArgs():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if not argv:
        raise SystemExit("入力の .glb を指定してください")
    args = {"input": argv[0], "export": None, "preview": None}
    for key in ("export", "preview"):
        if "--" + key in argv:
            args[key] = argv[argv.index("--" + key) + 1]
    return args


def ImportGlb(path):
    """GLB を空のシーンへ読み込み、アーマチュアとスキン付きメッシュを返す"""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=path)

    armature = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    skinned = [o for o in bpy.data.objects
               if o.type == 'MESH' and any(m.type == 'ARMATURE' for m in o.modifiers)]
    if len(skinned) != 1:
        raise SystemExit(f"スキン付きメッシュが {len(skinned)} 個あります（1 個を想定）")
    mesh = skinned[0]

    # アーマチュアを一番上の階層にし、スキンを持たないメッシュ（Icosphere など）と空のオブジェクトを消す
    armature.parent = None
    for obj in [o for o in bpy.data.objects if o not in (armature, mesh)]:
        bpy.data.objects.remove(obj, do_unlink=True)
    armature.name = kArmatureName
    mesh.name = kOutputName
    mesh.data.name = kOutputName
    return armature, mesh


def ClearPose(armature):
    """骨の姿勢を消し、元の位置（レスト）に戻す"""
    for pb in armature.pose.bones:
        pb.rotation_mode = 'QUATERNION'
        pb.location = (0.0, 0.0, 0.0)
        pb.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
        pb.scale = (1.0, 1.0, 1.0)
    bpy.context.view_layer.update()


def SnapToAxes(rotation):
    """回転行列を、各行・各列に ±1 が1つだけある軸の入れ替えに丸める"""
    snapped = np.zeros((3, 3))
    used = set()
    for row in range(3):
        col = max((c for c in range(3) if c not in used), key=lambda c: abs(rotation[row, c]))
        used.add(col)
        snapped[row, col] = np.sign(rotation[row, col])
    return snapped


def FitMeshToBones(armature, mesh):
    """メッシュの座標を骨の座標へ移す剛体変換（回転 + 平行移動）を求める

    回転: 各頂点を「重みで平均した骨の中点」と対応させ、最小二乗で求めて軸の入れ替えに丸める
    平行移動: 親子2本の骨に半々で重みが乗った頂点の中心を関節の位置と対応させ、中央値をとる
    """
    groupName = {g.index: g.name for g in mesh.vertex_groups}
    bones = armature.data.bones

    meshPoints = []
    bonePoints = []
    for v in mesh.data.vertices:
        total = 0.0
        acc = Vector()
        for g in v.groups:
            bone = bones[groupName[g.group]]
            acc += g.weight * (bone.head_local + bone.tail_local) * 0.5
            total += g.weight
        if total > 0.0:
            meshPoints.append(tuple(v.co))
            bonePoints.append(tuple(acc / total))
    p = np.array(meshPoints)
    q = np.array(bonePoints)

    # 2つの点群の共分散を特異値分解して、p を q に重ねる回転を求める
    pc = p - p.mean(axis=0)
    qc = q - q.mean(axis=0)
    u, _, vt = np.linalg.svd(qc.T @ pc)
    d = np.eye(3)
    d[2, 2] = np.sign(np.linalg.det(u @ vt))
    rotation = u @ d @ vt
    snapped = SnapToAxes(rotation)
    snapError = np.abs(rotation - snapped).max()
    if snapError > kAxisSnapTolerance:
        raise SystemExit(f"メッシュと骨のずれが軸の入れ替えになっていません（差 {snapError:.3f}）")

    # 関節まわりの頂点を回転後の座標で集め、関節（子の骨の根元）ごとの中心を求める
    jointVertices = {}
    for v in mesh.data.vertices:
        weights = sorted(((g.weight, groupName[g.group]) for g in v.groups), reverse=True)
        if len(weights) < 2:
            continue
        (w0, name0), (w1, name1) = weights[0], weights[1]
        if w0 + w1 < kJointWeightSumMin or not (kJointWeightMin < w0 < kJointWeightMax):
            continue
        b0 = bones[name0]
        b1 = bones[name1]
        child = b1 if b1.parent == b0 else (b0 if b0.parent == b1 else None)
        if child is None:
            continue
        jointVertices.setdefault(child.name, []).append(snapped @ np.array(v.co))

    offsets = [np.array(bones[name].head_local) - np.mean(points, axis=0)
               for name, points in jointVertices.items() if len(points) >= kJointVertexCountMin]
    translation = np.median(offsets, axis=0)

    result = Matrix.Identity(4)
    for r in range(3):
        for c in range(3):
            result[r][c] = snapped[r, c]
        result[r][3] = translation[r]
    print(f"[fix_dragon_rig] 回転の丸め誤差 {snapError:.4f} / 関節 {len(offsets)} 個から平行移動を算出")
    print(f"[fix_dragon_rig] メッシュ → 骨の変換\n{result}")
    return result


def ReportInfluence(mesh):
    """1頂点あたりの骨の最大本数と、上限を超える頂点の数を表示する"""
    counts = [sum(1 for g in v.groups if g.weight > 0.0) for v in mesh.data.vertices]
    over = sum(1 for c in counts if c > kMaxInfluence)
    print(f"[fix_dragon_rig] 頂点 {len(counts)} / 1頂点あたりの骨の最大本数 {max(counts)}"
          f" / {kMaxInfluence} 本を超える頂点 {over}")
    if over > 0:
        raise SystemExit(f"骨が {kMaxInfluence} 本を超える頂点があります")


def LoadDragon(path):
    """GLB を読み込み、メッシュを骨の位置へ合わせた状態にする"""
    armature, mesh = ImportGlb(path)
    ClearPose(armature)
    mesh.data.transform(FitMeshToBones(armature, mesh))
    mesh.data.update()
    ReportInfluence(mesh)
    return armature, mesh


def OrientForEngine(armature, mesh):
    """頭を -Y へ向け、メッシュの底面を z=0・前後左右の中心を 0 にそろえる（骨も同じだけ動かす）"""
    rotation = Matrix.Rotation(radians(kYawToEngineDegrees), 4, 'Z')
    armature.data.transform(rotation)
    mesh.data.transform(rotation)

    points = np.array([tuple(v.co) for v in mesh.data.vertices])
    lower = points.min(axis=0)
    upper = points.max(axis=0)
    offset = Vector((-(lower[0] + upper[0]) * 0.5, -(lower[1] + upper[1]) * 0.5, -lower[2]))
    translation = Matrix.Translation(offset)
    armature.data.transform(translation)
    mesh.data.transform(translation)
    mesh.data.update()

    size = upper - lower
    print(f"[fix_dragon_rig] 大きさ 左右 {size[0]:.3f} / 前後 {size[1]:.3f} / 高さ {size[2]:.3f}")


def BuildRenameMap():
    """Tripo の骨の名前 → 新しい名前"""
    renames = dict(kOtherRenames)
    for side, (root, arm) in kWingSourceBones.items():
        renames[root] = f"Wing_{side}_Root"
        renames[f"bone_{arm}"] = f"Wing_{side}_Arm"
        renames[f"bone_{arm + 1}"] = f"Wing_{side}_Hand"
        for finger in range(3):
            for joint in range(3):
                source = f"bone_{arm + 2 + finger * 3 + joint}"
                renames[source] = f"Wing_{side}_Finger{finger + 1}_{joint}"
    for index, name in enumerate(kTailSourceBones):
        renames[name] = f"Tail_{index:02d}"
    return renames


def RenameBones(armature, mesh):
    """骨に名前を付ける（頂点グループの名前も同じに変わる）。名前を決めていない骨は頭の tripo:: だけ外す"""
    renames = BuildRenameMap()
    missing = [name for name in renames if name not in armature.data.bones]
    if missing:
        raise SystemExit(f"名前を付ける骨が見つかりません: {missing}")

    for bone in armature.data.bones:
        name = renames.get(bone.name)
        if name is None:
            name = bone.name.removeprefix(kTripoPrefix)
            for prefix, replaced in kLimbPrefixes.items():
                if name.startswith(prefix):
                    name = replaced + name[len(prefix):]
        bone.name = name

    boneNames = {b.name for b in armature.data.bones}
    stray = [g.name for g in mesh.vertex_groups if g.name not in boneNames]
    if stray:
        raise SystemExit(f"骨と名前の合わない頂点グループがあります: {stray}")


def BoneSide(bone):
    """骨が体の左右どちらの手足に属するか（自身か祖先の名前の _L_ / _R_ で決める）。どちらでもなければ None"""
    while bone:
        if "_L_" in bone.name:
            return 'L'
        if "_R_" in bone.name:
            return 'R'
        bone = bone.parent
    return None


def BoneLeg(bone):
    """骨が前脚・後脚のどちらに属するか（自身か祖先の名前で決める）。脚でなければ None"""
    while bone:
        for leg in ("FrontLeg", "HindLeg"):
            if bone.name.startswith(leg):
                return leg
        bone = bone.parent
    return None


def RemoveStrayLimbWeights(armature, mesh):
    """各頂点から、ほかの手足の骨への重みを取り除き、残りの重みの合計を 1 に戻す。
    ・反対側の手足の骨（頂点の左右は X の符号で決める。体の左が +X、左右の中心は 0）
    ・脚の頂点では、もう一方の脚の骨（前脚の頂点なら後脚の骨。どちらの脚かは重みの合計の大きい方）
    """
    groupSides = {}
    groupLegs = {}
    for group in mesh.vertex_groups:
        bone = armature.data.bones.get(group.name)
        groupSides[group.index] = BoneSide(bone) if bone else None
        groupLegs[group.index] = BoneLeg(bone) if bone else None

    sideCount = 0
    legCount = 0
    for vertex in mesh.data.vertices:
        vertexSide = 'L' if vertex.co.x > 0.0 else 'R'
        crossSide = [g.group for g in vertex.groups
                     if groupSides[g.group] is not None and groupSides[g.group] != vertexSide]

        legWeights = {}
        for g in vertex.groups:
            leg = groupLegs[g.group]
            if leg is not None and g.group not in crossSide:
                legWeights[leg] = legWeights.get(leg, 0.0) + g.weight
        crossLeg = []
        if len(legWeights) > 1:
            vertexLeg = max(legWeights, key=legWeights.get)
            crossLeg = [g.group for g in vertex.groups
                        if groupLegs[g.group] not in (None, vertexLeg) and g.group not in crossSide]

        if not crossSide and not crossLeg:
            continue
        for index in crossSide + crossLeg:
            mesh.vertex_groups[index].remove([vertex.index])
        sideCount += len(crossSide)
        legCount += len(crossLeg)
        total = sum(g.weight for g in vertex.groups)
        if total > 0.0:
            for g in vertex.groups:
                g.weight /= total
    mesh.data.update()
    print(f"[fix_dragon_rig] 反対側の手足の骨への重みを {sideCount} 個、"
          f"もう一方の脚の骨への重みを {legCount} 個取り除きました")


def ExportGltf(armature, mesh, outDir):
    """アーマチュアとメッシュを .gltf + .bin + テクスチャに書き出す（Y 上、アニメなし、1頂点4本まで）"""
    os.makedirs(outDir, exist_ok=True)
    images = {node.image for mat in mesh.data.materials if mat and mat.use_nodes
              for node in mat.node_tree.nodes if node.type == 'TEX_IMAGE' and node.image}
    if len(images) != 1:
        raise SystemExit(f"テクスチャが {len(images)} 枚あります（1 枚を想定）")
    images.pop().name = kTextureName

    bpy.ops.object.select_all(action='DESELECT')
    armature.select_set(True)
    mesh.select_set(True)
    bpy.context.view_layer.objects.active = armature
    bpy.ops.export_scene.gltf(
        filepath=os.path.join(outDir, kOutputName + ".gltf"),
        export_format='GLTF_SEPARATE',
        use_selection=True,
        export_yup=True,
        export_animations=False,
        export_skins=True,
        export_influence_nb=kMaxInfluence,
        export_all_influences=False,
        export_image_format='AUTO',
        export_cameras=False,
        export_lights=False,
        export_extras=False,
    )
    print(f"[fix_dragon_rig] {outDir} に {kOutputName}.gltf を書き出しました")


def ExportFlightPose(armature, outDir, outputName, actionName, pose):
    """飛行姿勢を、骨だけの .gltf + .bin にアニメーションとして書き出す"""
    ClearPose(armature)
    ApplyFlightPose(armature, pose)
    for frame in kFlightPoseFrames:
        for pb in armature.pose.bones:
            pb.keyframe_insert(data_path="rotation_quaternion", frame=frame)
            pb.keyframe_insert(data_path="location", frame=frame)
    armature.animation_data.action.name = actionName

    bpy.ops.object.select_all(action='DESELECT')
    armature.select_set(True)
    bpy.context.view_layer.objects.active = armature
    bpy.ops.export_scene.gltf(
        filepath=os.path.join(outDir, outputName + ".gltf"),
        export_format='GLTF_SEPARATE',
        use_selection=True,
        export_yup=True,
        export_animations=True,
        export_animation_mode='ACTIONS',
        export_force_sampling=True,
        export_optimize_animation_size=False,
        export_cameras=False,
        export_lights=False,
        export_extras=False,
    )

    # 書き出したあとは姿勢とアニメーションを消して元に戻す
    action = armature.animation_data.action
    armature.animation_data.action = None
    bpy.data.actions.remove(action)
    ClearPose(armature)
    print(f"[fix_dragon_rig] {outDir} に {outputName}.gltf を書き出しました")


# ---------------------------------------------------------------------------
# 確認用のレンダリング（--preview）
# ---------------------------------------------------------------------------

# 1枚の画像の大きさ（ピクセル）
kPreviewWidth = 520
kPreviewHeight = 400
# 正投影カメラの写す幅と、注視点
kPreviewOrthoScale = 1.5
kPreviewTarget = Vector((0.0, 0.0, 0.3))
# カメラの位置（注視点からの方向 × 距離）。正面は頭側（-Y）から、横は体の左側（+X）から
kPreviewViews = {
    "front": Vector((0.0, -3.0, 0.0)),
    "side": Vector((3.0, 0.0, 0.0)),
    "top": Vector((1.6, -1.4, 2.2)),
}


def RotateBoneWorld(armature, name, axis, degrees):
    """骨を、今の根元を中心にアーマチュア空間の軸まわりで回す（子の骨もついて回る）"""
    bpy.context.view_layer.update()
    pb = armature.pose.bones[name]
    head = pb.head.copy()
    rotation = Matrix.Rotation(radians(degrees), 4, axis)
    pb.matrix = Matrix.Translation(head) @ rotation @ Matrix.Translation(-head) @ pb.matrix
    bpy.context.view_layer.update()


def MirrorSign(side, axis):
    """右の翼を左と鏡写しに動かすための向き。左右の面（X=0）で映すと Y・Z 軸まわりの回転が逆になる"""
    return -1.0 if (side == 'R' and axis in ('Y', 'Z')) else 1.0


def RotateBothWings(armature, part, axis, degrees):
    for side in ('L', 'R'):
        bones = WingBones(side)
        names = bones[part] if isinstance(bones[part], list) else [bones[part]]
        for name in names:
            RotateBoneWorld(armature, name, axis, degrees * MirrorSign(side, axis))


# 左の翼（+X 側）で「上げる」は前後の軸（Y）まわりの負の回転、「後ろへ畳む」は上下の軸（Z）まわりの正の回転
def PoseUp(armature, degrees):
    RotateBothWings(armature, "root", 'Y', -degrees)


def PoseSweepBack(armature, part, degrees):
    RotateBothWings(armature, part, 'Z', degrees)


def PoseFingerCurl(armature, degrees):
    """指の第2・第3節を下へ曲げる"""
    for side in ('L', 'R'):
        bones = WingBones(side)
        for finger in ("finger1", "finger2", "finger3"):
            for name in bones[finger][1:]:
                RotateBoneWorld(armature, name, 'Y', degrees * MirrorSign(side, 'Y'))


def PoseTailSwing(armature, degreesPerBone):
    for name in kTailBones:
        RotateBoneWorld(armature, name, 'Z', degreesPerBone)


def PoseNeckRaise(armature, degreesPerBone):
    for name in kNeckBones:
        RotateBoneWorld(armature, name, 'X', -degreesPerBone)


def ApplyFlightPose(armature, pose):
    """飛行姿勢にする（pose の回転を順に当てる）"""
    for name, axis, degrees in pose:
        if "{S}" in name:
            for side in ('L', 'R'):
                RotateBoneWorld(armature, name.replace("{S}", side), axis,
                                degrees * MirrorSign(side, axis))
        else:
            RotateBoneWorld(armature, name, axis, degrees)


# 確認する姿勢（名前, 姿勢を作る関数）
kPreviewPoses = [
    ("rest", lambda a: None),
    ("glide", lambda a: ApplyFlightPose(a, kGlidePose)),
    ("glide_up45", lambda a: (ApplyFlightPose(a, kGlidePose), PoseUp(a, 45.0))),
    ("hover", lambda a: ApplyFlightPose(a, kHoverPose)),
    ("hover_down30", lambda a: (ApplyFlightPose(a, kHoverPose), PoseUp(a, -30.0))),
    ("up45", lambda a: PoseUp(a, 45.0)),
    ("up70", lambda a: PoseUp(a, 70.0)),
    ("down30", lambda a: PoseUp(a, -30.0)),
    ("down50", lambda a: PoseUp(a, -50.0)),
    ("upstroke_fold", lambda a: (PoseUp(a, 30.0), PoseSweepBack(a, "arm", 25.0),
                                 PoseSweepBack(a, "hand", 35.0), PoseFingerCurl(a, 20.0))),
    ("glide_fold", lambda a: (PoseSweepBack(a, "arm", 20.0), PoseSweepBack(a, "hand", 45.0))),
    ("tail_neck", lambda a: (PoseTailSwing(a, 6.0), PoseNeckRaise(a, 6.0))),
]


def SetupPreviewScene():
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.render.resolution_x = kPreviewWidth
    scene.render.resolution_y = kPreviewHeight
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    shading = scene.display.shading
    shading.light = 'STUDIO'
    shading.color_type = 'TEXTURE'
    shading.show_cavity = True
    scene.world = bpy.data.worlds.new("PreviewWorld")
    scene.world.color = (0.55, 0.65, 0.8)

    target = bpy.data.objects.new("PreviewTarget", None)
    target.location = kPreviewTarget
    scene.collection.objects.link(target)

    cameras = {}
    for viewName, direction in kPreviewViews.items():
        data = bpy.data.cameras.new(viewName)
        data.type = 'ORTHO'
        data.ortho_scale = kPreviewOrthoScale
        cam = bpy.data.objects.new("Cam_" + viewName, data)
        cam.location = kPreviewTarget + direction
        track = cam.constraints.new('TRACK_TO')
        track.target = target
        track.track_axis = 'TRACK_NEGATIVE_Z'
        track.up_axis = 'UP_Y'
        scene.collection.objects.link(cam)
        cameras[viewName] = cam
    return cameras


def AddBoneSticks(armature):
    """骨の位置を見るため、骨ごとに細い棒のメッシュを置く（翼は赤、それ以外は青）"""
    wingNames = set()
    for side in ('L', 'R'):
        for value in WingBones(side).values():
            wingNames.update(value if isinstance(value, list) else [value])
    red = bpy.data.materials.new("StickWing")
    red.diffuse_color = (1.0, 0.1, 0.1, 1.0)
    blue = bpy.data.materials.new("StickBody")
    blue.diffuse_color = (0.1, 0.3, 1.0, 1.0)

    sticks = []
    for bone in armature.data.bones:
        head = armature.matrix_world @ bone.head_local
        tail = armature.matrix_world @ bone.tail_local
        length = (tail - head).length
        if length < 1e-4:
            continue
        bpy.ops.mesh.primitive_cylinder_add(vertices=6, radius=0.004, depth=length,
                                            location=(head + tail) * 0.5)
        stick = bpy.context.active_object
        stick.rotation_mode = 'QUATERNION'
        stick.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(tail - head)
        stick.data.materials.append(red if bone.name in wingNames else blue)
        sticks.append(stick)
    return sticks


def RenderTo(path, camera):
    scene = bpy.context.scene
    scene.camera = camera
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


def ComposeSheet(paths, columns, outPath):
    """同じ大きさの画像を、左上から横 columns 枚ずつ並べた1枚にまとめる"""
    rows = (len(paths) + columns - 1) // columns
    sheet = np.ones((rows * kPreviewHeight, columns * kPreviewWidth, 4), dtype=np.float32)
    for i, path in enumerate(paths):
        img = bpy.data.images.load(path)
        px = np.array(img.pixels[:], dtype=np.float32).reshape(kPreviewHeight, kPreviewWidth, 4)
        # Blender の画像は下の行から並んでいるので、行の位置を下から数える
        row = rows - 1 - i // columns
        col = i % columns
        sheet[row * kPreviewHeight:(row + 1) * kPreviewHeight,
              col * kPreviewWidth:(col + 1) * kPreviewWidth] = px
        bpy.data.images.remove(img)
    out = bpy.data.images.new("Sheet", columns * kPreviewWidth, rows * kPreviewHeight, alpha=True)
    out.pixels = sheet.ravel()
    out.filepath_raw = outPath
    out.file_format = 'PNG'
    out.save()


def Preview(armature, outDir):
    os.makedirs(outDir, exist_ok=True)

    # 確認用の .blend は、カメラや骨の棒を足す前の状態で保存する
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(outDir, "dragon_check.blend"), copy=True)

    cameras = SetupPreviewScene()
    shading = bpy.context.scene.display.shading

    # 骨の位置：メッシュを半透明にして骨の棒を重ねる
    sticks = AddBoneSticks(armature)
    shading.color_type = 'MATERIAL'
    shading.show_xray = True
    shading.xray_alpha = 0.35
    bonePaths = []
    for viewName, cam in cameras.items():
        path = os.path.join(outDir, f"bones_{viewName}.png")
        RenderTo(path, cam)
        bonePaths.append(path)
    ComposeSheet(bonePaths, 3, os.path.join(outDir, "sheet_bones.png"))
    for stick in sticks:
        bpy.data.objects.remove(stick, do_unlink=True)
    shading.show_xray = False
    shading.color_type = 'TEXTURE'

    # 姿勢ごとに、正面・横・斜め上から
    posePaths = []
    for poseName, makePose in kPreviewPoses:
        ClearPose(armature)
        makePose(armature)
        bpy.context.view_layer.update()
        for viewName, cam in cameras.items():
            path = os.path.join(outDir, f"pose_{poseName}_{viewName}.png")
            RenderTo(path, cam)
            posePaths.append(path)
    ClearPose(armature)
    ComposeSheet(posePaths, 3, os.path.join(outDir, "sheet_poses.png"))
    print(f"[fix_dragon_rig] 確認画像を {outDir} に出力しました")


def Main():
    args = ParseArgs()
    armature, mesh = LoadDragon(args["input"])
    OrientForEngine(armature, mesh)
    RenameBones(armature, mesh)
    RemoveStrayLimbWeights(armature, mesh)
    if args["export"]:
        ExportGltf(armature, mesh, args["export"])
        for outputName, actionName, pose in kFlightPoseOutputs:
            ExportFlightPose(armature, args["export"], outputName, actionName, pose)
    if args["preview"]:
        Preview(armature, args["preview"])


if __name__ == "__main__":
    Main()
