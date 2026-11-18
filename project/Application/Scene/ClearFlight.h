#pragma once
#include "Actor/DragonWingAnimator.h"
#include "Math/MathUtil.h"
#include <functional>
#include <random>
#include <string>
#include <vector>

class LineRenderer;

/// <summary>
/// クリア演出の最後の場面で、ドラゴンが晴れた空を自由に飛ぶ動きと、それを遅れて追うカメラの向き
/// 飛び方は「目標の点へ舵を切る」だけで決める
/// ・横は、体を傾けた分だけ曲がる（釣り合った旋回）。舵を切るとまず体が傾き、傾きに応じて向きが変わる
/// ・上りでは遅くなり、下りでは速くなる。遅いと羽ばたいて速さを取り戻す
/// 最初は決めた点（カメラの脇 → 太陽の前 → 左下 → カメラの前 → 頭の上）を順に通り、そのあとは乱数で選んだ点へ飛び続ける
/// 乱数の点は、カメラからの距離の帯（中・近・遠）を割合で選び、遠くの次は戻ってくる・近くは続けない・左右は交互、の順番で選ぶ
/// リザルトが出たら、カメラは最初の向き（太陽を映した向きから決めた角度だけずらした向き）へゆっくり戻って止まり、
/// ドラゴンは止まった画面の中・外のどこからでも行き先を選んで飛び回る。行き先は候補を何個か作り、
/// 最近通った場所から遠いほど・画面の左ほど選ばれやすく、板の裏は選ばれにくい重みで 1 つ選ぶ
/// 乱数の種を決めてあるので、Reset から同じ間隔で進めれば同じ動きになる
/// </summary>
class ClearFlight {
public:
  /// <summary>
  /// 最初からやり直す
  /// </summary>
  /// <param name="cameraPos">カメラの位置（点の基準）</param>
  /// <param name="right">水平な右</param>
  /// <param name="forward">水平な前</param>
  /// <param name="baseLook">カメラの元の向き（ドラゴンを追わないときの向き）</param>
  /// <param name="sunDir">カメラから見た太陽の向き</param>
  /// <param name="fovY">カメラの縦の画角（ラジアン。リザルトの間に止まる画面の広さに使う）</param>
  void Reset(const Vector3 &cameraPos, const Vector3 &right,
             const Vector3 &forward, const Vector3 &baseLook,
             const Vector3 &sunDir, float fovY);

  /// <summary>
  /// リザルトが出る時刻（Reset からの秒）。この時刻から、カメラは追うのをやめて止まる
  /// </summary>
  void SetResultTime(float time) { resultTime_ = time; }

  /// <summary>
  /// dt 秒進める（舵・速さ・位置・遊びの動き・翼・カメラの向き）
  /// </summary>
  void Step(float dt);

  /// <summary>
  /// Reset からの時間（秒）
  /// </summary>
  float GetTime() const { return time_; }

  const Vector3 &GetPosition() const { return position_; }
  /// <summary>
  /// モデルの原点（足元）を置く位置（体の中心 bodyPivot_ のまわりに回るよう、回転でずれた分を戻した位置）
  /// </summary>
  Vector3 GetModelPosition() const;

  /// <summary>
  /// モデルの回転（x: 上下の傾き、y: 向き、z: 体の傾きと横の回転）
  /// </summary>
  Vector3 GetRotation() const;
  /// <summary>
  /// モデルの向き（体の前の軸まわりに傾け、次に上下へ傾け、最後に向きを回す）
  /// </summary>
  Quaternion GetOrientation() const;

  const DragonWingAnimator::State &GetWingState() const { return wing_; }

  /// <summary>
  /// カメラが向く向き（ドラゴンへ遅れて向く）
  /// </summary>
  const Vector3 &GetLookDirection() const { return lookDir_; }

  /// <summary>
  /// 太陽の前を通り過ぎた時刻（Reset からの秒。まだなら負）
  /// </summary>
  float GetSunPassTime() const { return sunPassTime_; }

  /// <summary>
  /// 調整値の float を、JSON のキーと一緒に1つずつ visit に渡す
  /// </summary>
  void VisitConfigValues(
      const std::function<void(const std::string &key, float &value)> &visit);

  /// <summary>
  /// 調整用UI
  /// </summary>
  void DrawImGui();

  /// <summary>
  /// 確認用の線：直近 60 秒の通った跡（黄）・今の行き先（赤）・リザルトの間に止まる画面の枠（白）と板の左端（橙）
  /// </summary>
  void DrawDebugLines(LineRenderer &lines) const;

  /// <summary>
  /// 確認用に真上から見下ろすカメラの位置と角度（画面の上が、リザルトの間に止まる向き）
  /// </summary>
  void GetTopView(Vector3 &translate, Vector3 &rotate) const;

private:
  // GetOrientation と同じ向きの回転行列
  Matrix4x4 MakeOrientationMatrix() const;
  // リザルトの間の映り方を resultStats_ に足す
  void StepResultStats(float dt);
  // 今の巡航の速さ（リザルトの間は resultCruiseSpeed_）
  float CruiseSpeed() const;

  // 最初に通る点の順番（Sun は太陽の前、Wander からは乱数で選ぶ）
  enum class Waypoint { NearPass, Sun, DiveLow, NearPassAgain, OverHead, Wander };
  // 乱数で選ぶ点の、カメラからの距離の帯
  enum class Band { Mid, Near, Far };
  // リザルトの間の行き先の種類（止まった画面の中・外・カメラの脇を抜ける点）
  enum class ResultLeg { In, Out, Pass, Count };

  // カメラから、水平な右・上・前の値だけ動かした位置
  Vector3 FromCamera(const Vector3 &offset) const;
  // 今の向き（上下の傾き pitch_ と向き yaw_ から）
  Vector3 Direction() const;
  // 次の点へ進める（太陽を過ぎた時刻・翼を広げて浮く・横の回転もここで決める）
  void NextWaypoint();
  // 乱数で、気ままに飛ぶ次の点を選ぶ
  Vector3 PickWanderTarget();
  // 前に選んだ帯から、次の帯を選ぶ（遠くの次は遠くにしない・近くは続けない）
  Band PickBand();
  // 帯 band の中で、左右 side（1: 右、-1: 左）の側の点を 1 つ選ぶ
  Vector3 PickPointInBand(Band band, float side);
  // リザルトの間の行き先を、候補から重みで 1 つ選ぶ
  Vector3 PickResultTarget();
  // リザルトの間の行き先の候補を 1 つ作る（leg に種類を入れる）
  Vector3 MakeResultCandidate(ResultLeg &leg);
  // 候補 candidate の選ばれやすさ（最近通った場所から遠いほど・画面の左ほど大きく、板の裏は小さい）
  float ResultCandidateWeight(const Vector3 &candidate) const;
  // 止まった画面の向きから水平に yawDeg 度（右が正）・水平から elevationDeg 度見上げた向きの、水平な距離 distance の位置
  Vector3 FromResultView(float yawDeg, float elevationDeg, float distance) const;
  // position の、止まった画面での位置（-1〜1 が画面の中）。カメラより後ろなら false
  bool ToResultScreen(const Vector3 &position, Vector2 &screen) const;
  // 位置 position が、止まった画面に映っているか
  bool IsInResultFrame(const Vector3 &position) const;
  // position が、向き look のカメラの画面の中（端の角度の内側）か
  bool IsInFrame(const Vector3 &position, const Vector3 &look) const;
  // リザルトが出ているか
  bool IsResultShown() const { return time_ >= resultTime_; }
  // 今の速さで、体を最も傾けて曲がったときの半径
  float TurnRadius() const;
  void StepSteering(float dt);
  void StepSpeed(float dt);
  void StepPlay(float dt);
  void StepWing();
  void StepCamera(float dt);

  // 基準
  Vector3 cameraPos_{};
  Vector3 right_ = {1.0f, 0.0f, 0.0f};
  Vector3 forward_ = {0.0f, 0.0f, 1.0f};
  Vector3 baseLook_ = {0.0f, 0.0f, 1.0f};
  Vector3 sunDir_ = {0.0f, 1.0f, 0.0f};

  // 今の状態
  float time_ = 0.0f;
  Vector3 position_{};
  float yaw_ = 0.0f;   // 向き（ラジアン。+z が 0、+x へ増える）
  float pitch_ = 0.0f; // 上下の傾き（ラジアン。上が正）
  float speed_ = 0.0f;
  float bank_ = 0.0f;  // 体の傾き（ラジアン。右へ曲がるとき正）
  float flap_ = 0.0f;  // 羽ばたきの強さの割合（0: 滑る 〜 1: 強く羽ばたく）
  float brake_ = 0.0f; // 翼を広げて体を起こし、空気で減速している割合（0〜1）
  Waypoint waypoint_ = Waypoint::NearPass;
  Vector3 target_{};
  float targetTimer_ = 0.0f;  // 今の点へ向かい始めてからの秒
  float sunPassTime_ = -1.0f;
  float spreadTimer_ = 0.0f;  // 翼を広げて浮いている残りの秒
  float rollTimer_ = -1.0f;   // 横の回転を始めてからの秒（負なら回っていない）
  float rollSide_ = 1.0f;     // 横の回転の向き
  bool isRollPending_ = false; // 横の回転を、条件がそろったら始めるか（次の点へ切り替えるまで）
  Band lastBand_ = Band::Near; // 前に選んだ距離の帯
  float wanderSide_ = 1.0f;   // 次に選ぶ点の左右（1: 右、-1: 左。選ぶたびに入れ替える）
  DragonWingAnimator::State wing_{};
  Vector3 lookDir_ = {0.0f, 0.0f, 1.0f};
  float resultTime_ = 1.0e9f;
  Vector3 resultLook_ = {0.0f, 0.0f, 1.0f};      // リザルトの間に止まる向き（止まった画面の基準）
  Vector3 resultStartLook_ = {0.0f, 0.0f, 1.0f}; // リザルトが出た瞬間のカメラの向き
  bool hasResultLook_ = false;
  std::mt19937 random_;

  // リザルトの間の映り方の記録（ImGui で見る）
  // 行き先の種類の数（ResultLeg の順）
  static constexpr int kLegCount = static_cast<int>(ResultLeg::Count);
  struct ResultStats {
    float legTime[kLegCount] = {};  // その種類の行き先へ向かっていた秒
    float legBoard[kLegCount] = {}; // そのうち板の裏にいた秒
    float legOut[kLegCount] = {};   // そのうち画面の外にいた秒
    float outWhere[5] = {};         // 画面の外にいた秒を場所で分けたもの（カメラの後ろ・左・右・上・下）
    float total = 0.0f;      // リザルトが出てからの秒
    float largeSec = 0.0f;     // 見える所で、画面の高さの largeRatio_ 以上に映った秒
    float mediumSec = 0.0f;    // 見える所で、smallRatio_〜largeRatio_ に映った秒
    float smallSec = 0.0f;     // 見える所で、smallRatio_ より小さく映った秒
    float boardSec = 0.0f;     // 画面の中だが、リザルトの板の裏にいた秒
    float absent = 0.0f;     // 今、続けて見えない（画面の外か板の裏）秒
    float maxAbsent = 0.0f;  // 続けて見えなかった秒の最長
  };
  ResultStats resultStats_;
  int resultLeg_ = 0; // 今の行き先の種類（ResultStats の leg の添え字）
  // 確認用の通った跡（kTrailInterval ごとに記録し、古いものから捨てる）
  static constexpr float kTrailInterval = 0.1f; // 記録の間隔（秒）
  static constexpr size_t kTrailCount = 600;    // 残す数（60 秒分）
  std::vector<Vector3> trail_;
  float trailTimer_ = 0.0f;
  float measureSpan_ = 5.0f;   // 映る大きさを測るときのドラゴンの大きさ（翼の端から端、m。モデルの幅 0.98 × 自機の大きさ 5）
  float largeRatio_ = 0.15f;   // 大きく映ったとみなす、画面の高さに対する割合
  float smallRatio_ = 0.10f;   // 小さく映ったとみなす、画面の高さに対する割合
  float boardLeftNdc_ = 0.1f;  // リザルトの板の左端の、画面の横の位置（-1: 左端 〜 1: 右端）

  //---- 最初に通る点（カメラから、水平な右・上・前。長さはワールドの長さ）----
  Vector3 startOffset_ = {5.0f, -6.0f, -8.0f};     // 飛び出す位置（カメラの右下の後ろ）
  float startSpeed_ = 24.0f;                       // 飛び出す速さ
  Vector3 nearPassOffset_ = {3.5f, 1.5f, 6.0f};    // 最初にかすめる点（カメラの右前）
  float sunDistance_ = 45.0f;                      // 太陽の前の点の、カメラからの距離
  Vector3 diveLowOffset_ = {-35.0f, -2.0f, 40.0f}; // 急降下で降りる点（左下の奥）
  Vector3 nearPassAgainOffset_ = {-4.0f, 2.0f, 5.0f}; // もう一度かすめる点（カメラの左前）
  Vector3 overHeadOffset_ = {-6.0f, 10.0f, -30.0f};   // 頭の上を越えて抜ける点（カメラの後ろ上）

  //---- 気ままに飛ぶ点を選ぶ帯 ----
  float midWeight_ = 0.6f;    // 中の帯を選ぶ重み（形と羽ばたきがはっきり見える距離）
  float nearWeight_ = 0.25f;  // 近くの帯を選ぶ重み（カメラの脇を抜ける）
  float farWeight_ = 0.15f;   // 遠くの帯を選ぶ重み（空の広さを見せる）
  float midMinDistance_ = 25.0f;  // 中の帯の、カメラからの距離
  float midMaxDistance_ = 55.0f;
  float farMinDistance_ = 70.0f;  // 遠くの帯の、カメラからの距離
  float farMaxDistance_ = 100.0f;
  float spreadYawDeg_ = 40.0f;    // 中・遠くの点を選ぶ、カメラの元の向きからの左右の角度（度）
  float minElevationDeg_ = 8.0f;  // 中・遠くの点の、水平からの見上げる角度（度）
  float maxElevationDeg_ = 50.0f;
  Vector2 nearSide_ = {5.0f, 12.0f};    // 近くの点の、カメラからの横の距離（最小・最大）
  Vector2 nearUp_ = {2.0f, 8.0f};       // 近くの点の、カメラからの高さ（最小・最大）
  Vector2 nearForward_ = {-5.0f, 8.0f}; // 近くの点の、カメラからの前の距離（最小・最大）
  float minHopDistance_ = 20.0f; // 次の点を、今の位置からこれ以上離す
  float reachRadius_ = 6.0f;  // 点に着いたとみなす距離
  float targetTimeout_ = 7.0f; // 点に着けなくても次の点へ移るまで（秒）
  float floorOffset_ = -6.0f;  // 床の高さ（カメラの高さから。雲海へ潜らない）

  //---- 舵 ----
  float turnGain_ = 1.5f;     // 向きのずれに掛けて、曲がりたい速さにする割合
  float maxYawRate_ = 1.4f;   // 曲がりたい速さの上限（ラジアン/秒）
  float maxBank_ = 1.1f;      // 体の傾きの上限（ラジアン）
  float bankRate_ = 2.5f;     // 体の傾きが変わる速さ（ラジアン/秒）
  float pitchGain_ = 2.0f;    // 上下のずれに掛けて、上下の向きを変える速さにする割合
  float maxPitchRate_ = 1.2f; // 上下の向きを変える速さの上限（ラジアン/秒）
  float maxPitch_ = 1.2f;     // 上下の傾きの上限（ラジアン）
  float resultMaxPitch_ = 0.6f; // リザルトの間の maxPitch_（上り下りの行き過ぎで画面の上へ抜けない）

  //---- 速さ ----
  float cruiseSpeed_ = 18.0f;   // この速さより遅いと羽ばたく
  float resultCruiseSpeed_ = 15.0f; // リザルトの間の cruiseSpeed_
  float glideSpeed_ = 10.0f;    // 羽ばたかずに水平に飛ぶと落ち着く速さ
  float minSpeed_ = 7.0f;
  float maxSpeed_ = 40.0f;
  float flapAccel_ = 12.0f;     // 強く羽ばたいたときの加速（単位/秒²）
  float drag_ = 0.5f;           // 滑る速さとの差に掛ける減速の割合（1/秒）
  float slopeAccel_ = 6.0f;     // 真上を向いたときの減速・真下を向いたときの加速（単位/秒²）
  float flapSpeedRange_ = 8.0f; // 速さが cruiseSpeed_ からこれだけ遅いと、最も強く羽ばたく
  float climbFlap_ = 1.2f;      // 上り（向きの上の成分）に掛けて羽ばたきを強める割合
  //---- 曲がる前の減速（翼を広げて体を起こし、空気をブレーキにする）----
  float brakeStartDeg_ = 30.0f; // 行き先との向きのずれがこれより大きいと減速し始める（度）
  float brakeFullDeg_ = 90.0f;  // 行き先との向きのずれがこれ以上で、最も強く減速する（度）
  float brakeSpeed_ = 8.0f;     // 減速して近づける速さ（これより遅いと減速しない）
  float brakeDecel_ = 10.0f;    // 最も強く減速したときの減速（単位/秒²）
  float brakeLag_ = 0.25f;      // 減速の割合が狙いへ近づく時間（秒）
  float brakePitch_ = 0.35f;    // 最も強く減速したときに体を起こす角度（ラジアン。見た目だけ）
  float brakeHover_ = 0.7f;     // 最も強く減速したときの翼の姿勢（0: 滑空 〜 1: 体を起こした羽ばたきの姿勢）
  float flapLag_ = 0.3f;        // 羽ばたきの強さが変わる遅れ（秒）

  //---- 遊び ----
  float spreadSec_ = 1.2f;      // 太陽の前で翼を広げて浮く時間
  float rollSec_ = 1.1f;        // 横に一回転する時間
  float rollChance_ = 0.35f;    // 点に着いたとき、速ければ横に一回転する割合
  float rollMinSpeed_ = 14.0f;  // 横に一回転するのに要る速さ
  float rollMaxBank_ = 0.25f;   // 横に一回転を始められる、体の傾きの上限（ラジアン。旋回中は始めない）
  float rollMaxPitch_ = 0.35f;  // 横に一回転を始められる、上下の傾きの上限（ラジアン。上り・下りの途中は始めない）
  Vector3 bodyPivot_ = {0.0f, 1.1f, 0.5f}; // 回す中心（モデルの原点＝足元から、モデルの右・上・前へ m）
  float bankSign_ = -1.0f;      // モデルの z 回転に掛ける符号（体が曲がる側へ傾くよう合わせる）

  //---- 翼 ----
  float flapStrength_ = 0.85f;  // 強く羽ばたくときの打つ角度の大きさ
  float glideStrength_ = 0.08f; // 滑るときの打つ角度の大きさ
  float flapFrequency_ = 1.7f;  // 強く羽ばたくときの 1 秒あたりの回数
  float glideFrequency_ = 1.0f; // 滑るときの 1 秒あたりの回数

  //---- カメラ ----
  float followWeight_ = 0.7f;   // 元の向きとドラゴンの向きの間で、ドラゴンへ寄せる割合
  float cameraLag_ = 0.45f;     // ドラゴンへ向く遅れ（秒）
  float maxCameraRate_ = 0.9f;  // 向きを変える速さの上限（ラジアン/秒）
  float behindDot_ = -0.1f;     // ドラゴンがこの内積より後ろへ行ったら追わない
  float minLookPitch_ = 0.05f;  // カメラが下を向きすぎないよう、上下の向きの下限（ラジアン）
  float settleSec_ = 1.5f;      // リザルトが出てから、止まる向きへ戻りきるまで（秒）
  float resultYawOffsetDeg_ = 0.0f;   // 止まる向きを、最初の向きから右へずらす角度（度）
  float resultPitchOffsetDeg_ = 0.0f; // 止まる向きを、最初の向きから上へずらす角度（度）

  //---- リザルトの間の飛び方（止まった画面の向きから）----
  float frameHalfYawDeg_ = 44.0f;   // 止まった画面の左右の半分の角度（度。Reset でカメラの画角から求める）
  float frameHalfPitchDeg_ = 28.6f; // 止まった画面の上下の半分の角度（度。Reset でカメラの画角から求める）
  Vector2 resultDistance_ = {12.0f, 45.0f}; // 行き先の、カメラからの水平な距離（最小・最大）
  float resultYawMarginDeg_ = 25.0f;   // 行き先を選ぶ左右の範囲を、画面の端より外へ広げる角度（度）
  float resultPitchMarginDeg_ = 15.0f; // 行き先を選ぶ上の範囲を、画面の上の端より外へ広げる角度（度）
  float resultMinElevationDeg_ = 3.0f; // 行き先の、水平からの見上げる角度の下限（度）
  float resultMaxHeight_ = 35.0f;      // 行き先の、カメラからの高さの上限（m）
  float resultPassChance_ = 0.15f;     // 候補を、カメラの脇を抜ける点にする割合
  float resultCandidates_ = 12.0f;     // 1 回に作る候補の数
  float recentSec_ = 15.0f;       // 最近通った場所として見る、通った跡の秒
  float recentRadius_ = 15.0f;    // 最近通った場所からこの距離より近い候補ほど選ばれにくい（m）
  float recentMinWeight_ = 0.05f; // 最近通った場所の上の候補の選ばれやすさ（遠い候補を 1 とする）
  float rightWeight_ = 0.35f;     // 画面の右端の候補の選ばれやすさ（左端を 1 とし、間はなめらかに変える）
  float boardWeight_ = 0.3f;      // 板の裏に映る候補の選ばれやすさに掛ける割合
  float resultMinFlap_ = 0.5f;    // リザルトの間の、翼の羽ばたきの見た目の強さの下限（速さは変えない）
  float resultRollChance_ = 0.7f; // リザルトの間に、点に着いたとき横に一回転する割合
  float passAngleDeg_ = 70.0f;      // 自由に飛ぶ間、近くの点が前からこの角度より横にあれば着いたとみなす（度）
};
