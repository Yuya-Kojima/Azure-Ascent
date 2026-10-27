#pragma once
#include "Math/Matrix4x4.h"
#include "Math/Quaternion.h"
#include "Math/Vector3.h"
#include "SpringBoneChain.h"
#include <array>
#include <cstdint>
#include <functional>
#include <random>
#include <vector>

class SkinnedObject;
struct Animation;

/// <summary>
/// 自機ドラゴンの羽ばたき。翼・尾の関節を位相をずらした波で回し、頭は体の揺れを打ち消して保ち、
/// 脚は必要なときに前後へ振って空を掻かせ、尾の先と脚は揺れもので遅れてしならせて、SkinnedObject に書き込む
/// </summary>
class DragonWingAnimator {
public:
  /// <summary>
  /// 羽ばたきの状態。使う側は目標として毎フレーム渡し、今の状態はそこへ滑らかに近づく
  /// </summary>
  struct State {
    float strength = 0.6f;  // 打つ角度の大きさ（0: 広げたまま 〜 1: 最大）
    float frequency = 1.5f; // 1秒あたりの羽ばたき回数
    float fold = 0.0f;      // 翼の畳み具合（0: 広げる 〜 1: 畳む）
    float asymmetry = 0.0f; // 左右差（-1 〜 1）。正で左の翼を強く打つ（右へ曲がるとき）
    float pose = 1.0f;      // 姿勢（0: 地上の立ち姿勢 〜 1: 飛行姿勢）
    float hover = 0.0f;     // 飛行姿勢の形（0: 滑空の姿勢 〜 1: 体を起こした羽ばたきの姿勢）
    float sideLag = 0.0f;   // 右の翼が左より遅れて打つ割合（1周期に対して、0〜0.5）
    float buckle = 0.0f;    // 翼の張りが保てない度合い（0〜1）。打ち下ろしの途中で腕・手首が折れ、先ほど遅れてついてくる
    float flail = 0.0f;     // 体が羽ばたきに振り回される度合い（0〜1）。尾と体の前後の揺れを大きくする
    float response = 0.0f;  // 今の状態が目標へ近づく速さ（1/秒）。0 なら Config::stateResponse を使う
  };

  /// <summary>
  /// 羽ばたきに重ねる姿勢の重み（0〜1）。使う側が毎フレーム渡し、そのまま使う（State のようには遅らせない）
  /// 重みの合計の分だけ羽ばたきを弱め、各姿勢の角度を重みで混ぜて足す
  /// </summary>
  struct Gesture {
    float crouch = 0.0f;  // 溜め（頭を下げ、翼をすぼめる）
    float roar = 0.0f;    // 吠える（首を空へ立て、翼を張る）
    float launch = 0.0f;  // 飛び出しの一打ち（翼を打ち下ろす）
    float tremble = 0.0f; // 首と翼の先の震えの大きさ
    float collapse = 0.0f; // 倒れる（首を地面へ下ろし、翼を畳む）
    float lookUp = 0.0f;   // 見上げる（伏せた姿勢から首を立てて空を見上げる）
    // 顔の向き（GetFaceAngle と同じ測り方）を、体の傾きによらず faceUpAngle に合わせる割合（0〜1）。首は曲げず頭蓋だけ回す。重みの合計には数えない
    float faceUp = 0.0f;
    float faceUpAngle = 0.0f; // faceUp で合わせる顔の向き（度。GetFaceAngle と同じ測り方）
    // 地上の土台の姿勢を、立ち姿勢から伏せの姿勢へ寄せる割合（0〜1）。重みの合計には数えない
    float lie = 0.0f;
    // 伏せの姿勢を、胸を起こした伏せへ寄せる割合（0〜1）。重みの合計には数えない
    float lieChest = 0.0f;
    // 首の震えに掛ける割合（tremble のうち首に効く分。1 で翼の先と同じ）。重みの合計には数えない
    float neckTrembleRatio = 1.0f;
    // 力が抜けて垂れる割合（0〜1）。脚と尾の先を重さで垂らし、尾は自分で振らずに根元から下げる。重みの合計には数えない
    float hang = 0.0f;
    // 4 本の脚で空を掻く強さ（0〜1）。重みの合計には数えない
    float paddle = 0.0f;
    // 掻く周期の中の位置（周期の数。左前脚の位置で、ほかの脚は Config::legPaddleLag ずつ遅れる）
    float paddlePhase = 0.0f;
    // 揺れもので垂れた形のまま、脚を付け根から前後へ振る割合（-1〜1。正で後ろ、掻く振れ幅を掛ける）。重みの合計には数えない
    // 脚の順は左前・右前・左後・右後
    std::array<float, 4> limpLegSwing{};
  };

  /// <summary>
  /// 重ねる姿勢の角度（度）。骨1本あたりの角度で、上げる・後ろへ回す向きが正
  /// </summary>
  struct GesturePose {
    float spine = 0.0f;     // 背骨（左右の軸まわり。正で頭側が下がる）
    float neck = 0.0f;      // 首（左右の軸まわり。正で頭が下がる）
    float head = 0.0f;      // 頭蓋（首の角度に足す。正で鼻先が下がる）
    float tail = 0.0f;      // 尾の根元側（左右の軸まわり。正で上がる）
    float wingUp = 0.0f;    // 翼の付け根を上げる
    float wingBack = 0.0f;  // 翼の付け根を後ろへ回す
    float armUp = 0.0f;     // 腕を上げる
    float armBack = 0.0f;   // 腕を後ろへ回す
    float handUp = 0.0f;    // 手首を上げる
    float handBack = 0.0f;  // 手首を後ろへ回す
    float fingerFan = 0.0f; // 指を開く（翼端側の指ほど後ろ、根元側の指ほど前へ回す）
  };

  /// <summary>
  /// 調整値。角度は度、遅れは1周期に対する割合
  /// </summary>
  struct Config {
    float downstrokeRatio = 0.4f; // 1周期のうち打ち下ろしにかける割合

    // 振れ幅（度）。付け根は片側の振れ幅、それより先は付け根の動きに足す量
    float rootAmplitude = 35.0f;
    float rootBias = 5.0f; // 付け根の振りの中心（上向きが正）
    float armAmplitude = 10.0f;
    float handAmplitude = 10.0f;
    float fingerAmplitude = 6.0f;

    // 付け根に対する遅れ
    float armLag = 0.05f;
    float handLag = 0.10f;
    float fingerLag = 0.15f;
    float fingerJointLag = 0.04f; // 指の節ごとに足す遅れ

    // 打ち上げの途中で畳む量（度）
    float upstrokeArmSweep = 10.0f;
    float upstrokeHandSweep = 30.0f;
    float upstrokeFingerCurl = 15.0f;

    // 畳み具合 1 のときの量（度）
    float foldArmSweep = 25.0f;
    float foldHandSweep = 60.0f;
    float foldFingerFan = 25.0f; // 翼端側の指ほど後ろへ回して、指の間を閉じる
    float foldFingerCurl = 20.0f;

    float asymmetryGain = 0.4f; // 左右差 1 のとき、片側の振れ幅を何割増減するか

    // 翼のねじれ（度）。打ち下ろしの途中で前縁を下げ、打ち上げで戻す。腕はこの半分、手首はそのまま（指は手首について回る）
    float wingTwist = 10.0f;

    // 尾（骨1本あたりの角度）
    float tailAmplitude = 1.5f;
    float tailLag = 0.25f;
    float tailJointLag = 0.06f;
    float tailTurn = 3.0f; // 左右差 1 のときの尾の左右の曲がり

    // 体の上下（モデルの単位）
    float bodyBob = 0.01f;
    float bodyLag = 0.05f;
    float bodyPitchAmplitude = 3.0f; // 羽ばたきでの体の前後の傾き（度。正で頭が下がる）
    float bodyPitchLag = 0.1f;

    // 頭の安定
    float headLevelPitch = 0.5f; // 体の上下の向きのうち、頭で水平に戻す割合
    float headLevelRoll = 0.7f;  // 体の傾き（ロール）のうち、頭で水平に戻す割合
    float headTurnLead = 10.0f;  // 左右差 1 のとき、頭を曲がる方向へ向ける角度（度）
    float headMaxAngle = 40.0f;  // 頭を体から逸らす最大の角度（度）
    float neckShare = 0.5f;      // 逸らす角度のうち首の4本が受け持つ割合
    float headKeepHeight = 0.8f; // 羽ばたき由来の頭の上下を打ち消す割合
    float neckMaxPitch = 15.0f;  // 高さ合わせで首1本を曲げる最大の角度（度）

    float stateResponse = 4.0f; // 今の状態が目標へ近づく速さ（1/秒）

    // 1回ごとの揺らぎ（揺らぎ -1 〜 1 に掛ける量）
    float beatAmplitudeJitter = 0.08f; // 翼の振れ幅 ×（1 + 値 × 揺らぎ）
    float beatFrequencyJitter = 0.06f; // 1秒あたりの回数 ×（1 + 値 × 揺らぎ）
    float beatAsymmetryJitter = 0.05f; // 左右差に足す
    float beatBiasJitter = 3.0f;       // 付け根の振りの中心に足す（度）

    // 揺れもの（尾の先・脚）
    float tailSpringStiffness = 18.0f; // 尾のばねの強さ（1/秒）
    float tailSpringDrag = 0.4f;       // 尾の揺れの減り方（1/60 秒あたり）
    float legSpringStiffness = 18.0f;  // 脚のばねの強さ（1/秒）
    float legSpringDrag = 0.4f;        // 脚の揺れの減り方（1/60 秒あたり）
    float springGravity = 0.0f;        // 下向きの重さ（1/秒）
    float tailSpringMaxAngle = 12.0f;  // 尾の骨1本が姿勢から曲がる最大の角度（度）
    float legSpringMaxAngle = 8.0f;    // 脚の骨1本が姿勢から曲がる最大の角度（度）
    float springResetDistance = 0.5f;  // これ以上（モデルの単位）一度に動いたら置き直す

    // 力が抜けて垂れる（Gesture::hang が 1 のとき。揺れものの値はこの値へ混ぜる）
    float hangLegStiffness = 3.0f;   // 脚のばねの強さ（1/秒）
    float hangSpringGravity = 12.0f; // 脚と尾の先の下向きの重さ（1/秒）
    float hangLegMaxAngle = 45.0f;   // 脚の骨1本が姿勢から曲がる最大の角度（度）
    float hangTailDroop = 4.0f;      // 尾の根元側の骨1本を下げる角度（度）

    // 脚で空を掻く（Gesture::paddle が 1 のときの角度、度）
    float frontLegPaddle = 30.0f; // 前脚の付け根を前後へ振る片側の角度
    float frontElbowBend = 20.0f; // 前脚を手前へ引く途中で肘を曲げる角度
    float hindLegPaddle = 25.0f;  // 後脚の付け根を前後へ振る片側の角度
    float hindKneeExtend = 15.0f; // 後脚を後ろへ蹴る途中で膝を伸ばす角度
    float legPaddleLag = 0.25f;   // 掻く順（左前 → 右後 → 右前 → 左後）で、1 本ごとに足す遅れ（1周期に対する割合）

    // 重ねる姿勢（翼の付け根は rootBias に足す）
    GesturePose crouchPose = {4.0f, 6.0f, 8.0f, -2.0f, -15.0f, 20.0f,
                              0.0f, 15.0f, 0.0f, 25.0f, -8.0f};
    GesturePose roarPose = {-5.0f, -13.0f, 5.0f, 6.0f, 15.0f, -10.0f,
                            5.0f, 0.0f, 0.0f, -20.0f, 14.0f};
    GesturePose launchPose = {2.0f, 3.0f, 0.0f, 0.0f, -40.0f, 0.0f,
                              -10.0f, 0.0f, -10.0f, 0.0f, 6.0f};
    GesturePose collapsePose = {3.0f, 12.0f, 10.0f, -4.0f, -20.0f, 25.0f,
                                -5.0f, 20.0f, 0.0f, 30.0f, -10.0f};
    // 伏せの姿勢から首を起こし（真上より少し前に傾けたまま）、鼻先をおよそ 30 度上へ向ける（首 4 本で -104 度、頭蓋で -8 度）
    GesturePose lookUpPose = {0.0f, -26.0f, -8.0f, 3.0f, 0.0f, 0.0f,
                              0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    // 翼が折れる（State::buckle が 1 のとき）
    float buckleArmBend = 25.0f;  // 打ち下ろしの途中で腕を下へ折る角度（度）
    float buckleHandBend = 35.0f; // 打ち下ろしの途中で手首を下へ折る角度（度）
    float buckleLagGain = 1.5f;   // 腕・手首・指の遅れに、この割合を掛けて足す

    // 振り回される（State::flail が 1 のとき、元の振れ幅に掛けて足す割合）
    float flailTailGain = 6.0f;
    float flailBodyPitchGain = 3.0f;


    // 震え（Gesture::tremble が 1 のときの振れ幅、度）
    float trembleFrequency = 14.0f; // 1秒あたりの回数
    float neckTremble = 3.0f;       // 首の骨1本あたり
    float fingerTremble = 4.0f;     // 指の節1つあたり

    // 直接モード
    float directBeatSec = 0.35f;     // 遅れ（1周期に対する割合）を秒に直すときの、1周期の長さ（秒）
    float directPushSpeed = 700.0f;  // 付け根がこの速さ（度/秒）で動くとき、打つ速さを 1 とする
  };

  /// <summary>
  /// 直接モードで、付け根の角度（度。上が正）を返す関数
  /// 引数は、右の翼か・今から何秒前の角度か
  /// </summary>
  using DirectRootFunction = std::function<float(bool isRight, float delaySec)>;

  /// <summary>
  /// 初期化。羽ばたかせる SkinnedObject の関節を探し、調整値を読み込む
  /// </summary>
  /// <param name="glidePose">滑空の飛行姿勢（先頭の姿勢を使う）。nullptr なら立ち姿勢のまま</param>
  /// <param name="hoverPose">羽ばたきの飛行姿勢（先頭の姿勢を使う）。nullptr なら滑空の姿勢を使う</param>
  void Initialize(SkinnedObject *skinned, const Animation *glidePose = nullptr,
                  const Animation *hoverPose = nullptr);

  /// <summary>
  /// 伏せの姿勢を読み込む（Initialize のあとに呼ぶ）。Gesture::lie・lieChest で立ち姿勢から寄せる
  /// </summary>
  /// <param name="liePose">伏せの姿勢（先頭の姿勢を使う）</param>
  /// <param name="lieChestPose">胸を起こした伏せの姿勢（先頭の姿勢を使う）。nullptr なら伏せの姿勢を使う</param>
  void SetLiePoses(const Animation *liePose, const Animation *lieChestPose);

  /// <summary>
  /// 目標の状態を渡す（今の状態は Config::stateResponse の速さで近づく）
  /// </summary>
  void SetTargetState(const State &state) { targetState_ = state; }

  /// <summary>
  /// 補間せずに、今の状態を目標と同じにする
  /// </summary>
  void SetStateImmediate(const State &state);

  /// <summary>
  /// 羽ばたきに重ねる姿勢の重みを渡す（毎フレーム。何もしないときは既定値）
  /// </summary>
  void SetGesture(const Gesture &gesture) { gesture_ = gesture; }

  /// <summary>
  /// 直接モードにする。付け根の角度を、周期の波でなくこの関数から毎フレーム求め、腕・手首・指は付け根の少し前の角度をなぞる
  /// 畳み・折れ・振り回され・姿勢は State のまま使う。空の関数を渡すと周期で打つ羽ばたきに戻る
  /// </summary>
  void SetDirectRoot(DirectRootFunction rootAngleAt) {
    directRoot_ = std::move(rootAngleAt);
  }

  /// <summary>
  /// 直接モードか
  /// </summary>
  bool IsDirect() const { return static_cast<bool>(directRoot_); }

  /// <summary>
  /// 羽ばたきを進めて関節に書き込む。SkinnedObject::Update の前に呼ぶ
  /// </summary>
  /// <param name="worldMatrix">このフレームの、SkinnedObject のワールド行列（親を含む）</param>
  void Update(float deltaTime, const Matrix4x4 &worldMatrix);

  /// <summary>
  /// 調整値の ImGui
  /// </summary>
  void DrawImGui();

  void LoadConfig();
  void SaveConfig() const;

  const State &GetCurrentState() const { return currentState_; }

  /// <summary>
  /// 片方の翼が今打ち下ろしている強さ（0〜1。打ち上げの間は 0）
  /// </summary>
  /// <param name="isRight">右の翼なら true</param>
  float GetDownstrokePush(bool isRight) const;

  /// <summary>
  /// 片方の翼の付け根を今上げている角度（度。正で上、負で下）
  /// </summary>
  float GetRootAngle(bool isRight) const {
    return isRight ? rightRootAngle_ : leftRootAngle_;
  }
  const Config &GetConfig() const { return config_; }

  /// <summary>
  /// 顔の向き（度）。頭蓋の先の骨の根元から先への線が、世界の水平から上へ向く角度（下向きは負）
  /// この線は鼻先より約 40 度下を向くので、0 のとき鼻先は斜め上を向く
  /// </summary>
  float GetFaceAngle() const { return faceAngle_; }

private:
  // 書き込む関節。モデル空間の軸を、元の姿勢での親の向きで表したものを持つ
  struct DrivenJoint {
    int32_t index = -1; // 関節の番号。-1 は見つからなかった
    Vector3 restTranslation{};
    Vector3 lateralAxis{};  // モデルの左右の軸（X）
    Vector3 verticalAxis{}; // モデルの上下の軸（Y）
    Vector3 forwardAxis{};  // モデルの前後の軸（Z）
  };

  // 1回ごとに揺らす量の種類
  enum BeatJitterKind { kBeatAmplitude, kBeatFrequency, kBeatAsymmetry, kBeatBias, kBeatJitterCount };

  static constexpr int kFingerCount_ = 3;
  static constexpr int kFingerJointCount_ = 3;

  struct Wing {
    float side = 0.0f; // 左 -1・右 +1。「上げる」「後ろへ畳む」の回転の向きをそろえる
    DrivenJoint root;
    DrivenJoint arm;
    DrivenJoint hand;
    std::array<std::array<DrivenJoint, kFingerJointCount_>, kFingerCount_>
        fingers;
  };

  DrivenJoint MakeJoint(const char *name) const;
  void SetupFlightPoses(const Animation *glidePose, const Animation *hoverPose);

  // 立ち姿勢（伏せの割合で伏せの姿勢へ寄せたもの）と飛行姿勢を、今の姿勢の割合で混ぜた回転（羽ばたきはこの上に重ねる）
  Quaternion BaseRotation(int32_t jointIndex) const;
  Wing MakeWing(const char *sidePrefix, float side) const;

  // 関節を混ぜた姿勢から、骨の向き（ローカル +Y）まわりにねじり、
  // モデル空間の X → Z → Y の軸まわりの順に回して書き込む（ラジアン）
  void ApplyRotation(const DrivenJoint &joint, float aroundLateral,
                     float aroundVertical, float aroundForward,
                     float aroundBone = 0.0f) const;

  // 打つ位置（+1: 上げきり 〜 -1: 下ろしきり）。lag だけ遅れた位相で求める
  float Stroke(float lag) const;
  // 打ち上げの途中ほど 1 に近い重み（打ち下ろしの間は 0）
  float UpstrokeWeight(float lag) const;
  // 打つ速さ（正: 下へ動いている 〜 負: 上へ動いている、-1 〜 1）。lag だけ遅れた位相で求める
  float StrokeVelocity(float lag) const;

  // 直接モードで、遅れ lag（1周期に対する割合）だけ前の付け根の角度（度）
  float DirectRootAngle(bool isRight, float lag) const;
  // 打つ位置・打つ速さ・打ち上げの重み。周期で打つときは Stroke などと同じで isRight は使わない
  // 直接モードでは isRight の翼の付け根の角度から求める（打つ位置は rootBias からの角度を rootAmplitude で割った値）
  float WingStroke(bool isRight, float lag) const;
  float WingStrokeVelocity(bool isRight, float lag) const;
  float WingUpstrokeWeight(bool isRight, float lag) const;
  // 左右の翼の打つ位置の平均（体と尾に使う）
  float MeanStroke(float lag) const;
  // 打つ位置に掛ける強さ（直接モードでは打つ大きさを付け根の角度が持つので 1）
  float StrokeStrength() const {
    return IsDirect() ? 1.0f : currentState_.strength;
  }

  // 周期が1周するたびに、次の1回ぶんの揺らぎを引く
  void DrawNextBeat();
  // 今の周期の中での揺らぎ（前の値から次の値へ、周期の中でなめらかに移る）
  float BeatJitter(BeatJitterKind kind) const;

  void UpdatePose() const;
  // gain は振れ幅に掛ける割合、wingLag はこの翼だけの遅れ（1周期に対する割合）
  // 戻り値は付け根を上げた角度（度）
  float UpdateWing(const Wing &wing, float gain, float wingLag) const;
  void UpdateTail() const;
  void UpdateBody() const;
  // 戻り値は書き込んだあとの顔の向き（度。GetFaceAngle と同じ測り方）
  float UpdateHead(const Matrix4x4 &worldMatrix) const;
  void UpdateSpine() const;
  // 脚で空を掻く角度を、脚の揺れものの骨の姿勢に足す
  void UpdateLegs() const;
  // 揺れもので決まった脚の形のまま、付け根を Gesture::limpLegSwing の分だけ前後へ回す
  void SwingLimpLegs() const;

  // 重ねる姿勢の角度を、重みで混ぜて求める（度）
  float GestureAngle(float GesturePose::*part) const;
  // 震えの今の角度（-1 〜 1 に Gesture::tremble を掛けたもの）
  float TrembleWave(float phaseOffset) const;
  void UpdateSprings(float deltaTime, const Matrix4x4 &worldMatrix);

  // 関節のモデル空間の回転・位置（今の上書きを反映したもの）
  Quaternion JointModelRotation(int32_t jointIndex) const;
  Vector3 JointModelPosition(int32_t jointIndex) const;

  SkinnedObject *skinned_ = nullptr;
  Config config_;
  State targetState_;
  State currentState_;
  float phase_ = 0.0f; // 周期の中の位置（0〜1）。0 で上げきり、そこから打ち下ろす
  DirectRootFunction directRoot_; // 直接モードの付け根の角度（空なら周期で打つ）

  // 1回ごとの揺らぎ（-1 〜 1）。周期の始めに beatFrom_、終わりに beatTo_ になる
  std::mt19937 random_;
  std::array<float, kBeatJitterCount> beatFrom_{};
  std::array<float, kBeatJitterCount> beatTo_{};

  // 関節ごとの立ち姿勢・滑空・羽ばたきの回転と、立ち姿勢から動く関節の番号
  std::vector<Quaternion> restRotations_;
  std::vector<Quaternion> glideRotations_;
  std::vector<Quaternion> hoverRotations_;
  // 関節ごとの伏せ・胸を起こした伏せの回転（SetLiePoses を呼ぶまでは空）
  std::vector<Quaternion> lieRotations_;
  std::vector<Quaternion> lieChestRotations_;
  std::vector<int32_t> posedJoints_;
  float hoverWeight_ = 0.0f; // 飛行姿勢のうち羽ばたきの割合（0: 滑空 〜 1: 羽ばたき）
  float bodyPitch_ = 0.0f;   // 羽ばたきでの体の前後の傾き（ラジアン、モデルの左右の軸まわり）
  Gesture gesture_;          // 重ねる姿勢の重み
  float gestureWeight_ = 0.0f; // 重ねる姿勢の重みの合計（0〜1）
  float flapScale_ = 1.0f;     // 羽ばたきの強さに掛ける割合（1 - gestureWeight_）
  float trembleTime_ = 0.0f;   // 震えの経過時間（秒）
  float leftRootAngle_ = 0.0f;  // 左の翼の付け根を上げた角度（度）
  float rightRootAngle_ = 0.0f; // 右の翼の付け根を上げた角度（度）

  Wing leftWing_;
  Wing rightWing_;
  std::vector<DrivenJoint> neck_; // 頭蓋より根元側の首の骨
  DrivenJoint head_;              // 頭蓋（前を向く骨）
  DrivenJoint faceBase_;          // 顔の向きを測る骨の根元（頭蓋の先）
  DrivenJoint faceTip_;           // 顔の向きを測る骨の先
  float faceAngle_ = 0.0f;        // 顔の向き（度。GetFaceAngle と同じ測り方）
  std::vector<DrivenJoint> tail_;
  std::vector<DrivenJoint> spine_;
  static constexpr size_t kGestureTailJointCount_ = 6; // 重ねる姿勢で曲げる尾の骨の数（根元から）
  DrivenJoint body_;
  static constexpr int kLegCount_ = 4; // 前脚・後脚の左右

  // 脚で掻くときに回す骨（付け根と、前脚は肘・後脚は膝）
  struct LegJoints {
    DrivenJoint root;
    DrivenJoint bend;
  };
  std::array<LegJoints, kLegCount_> legJoints_;

  SpringBoneChain tailSpring_;                        // 尾の先
  std::array<SpringBoneChain, kLegCount_> legSprings_; // 前脚・後脚
  std::vector<int32_t> legSpringJoints_;              // 脚の揺れものの骨（毎フレーム姿勢に戻す）
  Quaternion restBodyRotation_{0.0f, 0.0f, 0.0f, 1.0f}; // 元の姿勢での体のモデル空間の回転
  Vector3 restBodyPosition_{};                          // 元の姿勢での体のモデル空間の位置
};
