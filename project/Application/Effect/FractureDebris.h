#pragma once
#include "Framework/BaseActor.h"
#include "Render/Model/Model.h" // 割れ口の形 Model::ModelData（入れ子の型）を指すため
#include "Math/Matrix4x4.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include <memory>
#include <optional>
#include <random>
#include <vector>

class Object3d;
class Object3dRenderer;
class FracturePieceSet;

/// <summary>
/// メッシュ破砕の撃破演出（1回の爆発）
/// 事前に割った破片の形（FracturePieceSet）を借りて置き、物理で飛ばす
/// 寿命が来たら Destroy() し、ActorManager に消してもらう（EnemyDebris と同じ形）
/// つかんだ状態で作ると、Release までは持ち主の箱に沿って置かれ、割れ目だけが開いていく
/// 燃え方を決めると、Release のあと割れ口が冷めていき、割れ口から炎と黒煙を出しながら飛ぶ
/// </summary>
class FractureDebris : public BaseActor {

public: // 型
  // 調整値
  struct Params {
    float explosionSpeed = 3.0f;             // 外向きの速さ（1秒あたりの距離）
    Vector3 gravity = {0.0f, -9.8f, 0.0f};   // 重力加速度
    float maxAngularSpeed = 6.0f;            // 角速度の最大値（ラジアン/秒）
    float lifetime = 1.5f;                   // 寿命（秒）
    // 空気抵抗（1秒あたりに速度が減る割合）
    // 2.4 で旧演出 EnemyDebris の「毎フレーム速度を 96% にする」とほぼ同じ減り方になる（(1 - 0.96) x 60）
    float drag = 2.4f;
  };

  // 破片の見た目（敵の見た目を引き継ぐ）
  struct Look {
    Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f}; // 破片の色
    Vector3 rimColor = {0.0f, 0.0f, 0.0f};    // ふちの光の色
    float rimIntensity = 0.0f;                // ふちの光の強さ（0 でなし）
    float rimPower = 3.0f;                    // ふちの光の鋭さ
    // 割れ口の面を光らせる色（明るさ込み。1 を超えた分が光の漏れになる）
    // なければ割れ口も外側と同じ見た目で描く
    std::optional<Vector4> capGlowColor;
  };

  // 割れ目を開く決まり（つかんでいる間）
  struct Opening {
    Vector3 frontCenter = {0.0f, 0.0f, 0.0f}; // 割れ目の前線の中心（ワールド座標）
    float frontRadius = 0.0f;                 // 前線の、中心からの距離（m）
    float openWidth = 1.0f;                   // 前線が破片の重心を過ぎてから開ききるまでの距離（m）
    float gap = 0.0f;                         // 開ききったときに外へずれる幅（m）
  };

  // 放したあとの燃え方
  struct Burning {
    float coolSeconds = 1.0f;                       // 割れ口が冷めきるまでの秒数
    Vector4 cooledColor = {0.0f, 0.0f, 0.0f, 1.0f}; // 冷めきった割れ口の色
    float flameRate = 0.0f;    // 破片 1 つの割れ口から出る炎の数（1 秒あたり。燃え始め。寿命の終わりへ 0 に減る）
    float smokeRate = 0.0f;    // 破片 1 つから出る黒煙の数（1 秒あたり。燃え始め。寿命の終わりへ 0 に減る）
    float trailInherit = 0.0f; // 炎と黒煙が持って生まれる、破片の速さの割合
  };

public: // メンバ関数
  /// <summary>
  /// 破片を置いて、爆発させる
  /// </summary>
  /// <param name="renderer">破片の Object3d を描くレンダラー</param>
  /// <param name="pieceSet">破片の形（借りるだけ。この爆発より長生きすること）</param>
  /// <param name="position">爆発の中心（敵の位置）</param>
  /// <param name="rotation">敵の回転（破片の置き場所と、回り始めの向きに使う）</param>
  /// <param name="scale">敵の大きさ（形セットは元のモデルの大きさで作ってある）</param>
  /// <param name="look">破片の見た目（敵の色とふちの光を引き継ぐ）</param>
  /// <param name="inheritedVelocity">引き継ぐ速度（撃破された瞬間の敵の速度。1秒あたり）</param>
  /// <param name="params">調整値</param>
  FractureDebris(Object3dRenderer *renderer, const FracturePieceSet &pieceSet,
                 const Vector3 &position, const Vector3 &rotation,
                 const Vector3 &scale, const Look &look,
                 const Vector3 &inheritedVelocity, const Params &params);

  /// <summary>
  /// 破片を箱の形に並べて、つかんでおく（Release まで飛ばさない）
  /// </summary>
  /// <param name="renderer">破片の Object3d を描くレンダラー</param>
  /// <param name="pieceSet">破片の形（借りるだけ。この爆発より長生きすること）</param>
  /// <param name="boxWorld">箱の中 → ワールドの行列（回転と位置だけ）</param>
  /// <param name="scale">箱の大きさ（形セットは元のモデルの大きさで作ってある）</param>
  /// <param name="look">破片の見た目</param>
  /// <param name="params">Release したあとの調整値</param>
  FractureDebris(Object3dRenderer *renderer, const FracturePieceSet &pieceSet,
                 const Matrix4x4 &boxWorld, const Vector3 &scale, const Look &look,
                 const Params &params);

  // unique_ptr<Object3d> を前方宣言のまま持つため、デストラクタは .cpp で定義する
  ~FractureDebris() override;

  /// <summary>
  /// 更新（寿命と物理）。つかんでいる間は何もしない
  /// </summary>
  void Update() override;

  /// <summary>
  /// 描画
  /// </summary>
  void Draw3D() override;

  /// <summary>
  /// つかんでいる破片を箱の今の位置・向きに置き直し、前線が過ぎた破片の割れ目を開く
  /// 開いた割合に合わせて、割れ口を光らせる
  /// </summary>
  void Hold(const Matrix4x4 &boxWorld, const Opening &opening);

  /// <summary>
  /// 放して、今の置き場所から外へ飛ばす
  /// </summary>
  /// <param name="inheritedVelocity">引き継ぐ速度（持ち主の今の速度。1秒あたり）</param>
  void Release(const Vector3 &inheritedVelocity);

  bool IsHeld() const { return isHeld_; }

  /// <summary>
  /// 放したあとの燃え方を決める（決めなければ燃えず、割れ口も冷めない）
  /// </summary>
  void SetBurning(const Burning &burning) { burning_ = burning; }

  /// <summary>
  /// 開いている割れ目の線の上から、点を 1 つ選ぶ
  /// </summary>
  /// <param name="random">乱数</param>
  /// <param name="point">選んだ点（ワールド座標）</param>
  /// <param name="normal">点が乗っている外側の面の外向き（ワールド、長さ1）</param>
  /// <returns>開いている割れ目がなければ false</returns>
  bool PickOpenCrackPoint(std::mt19937 &random, Vector3 &point, Vector3 &normal) const;

private: // 型
  // 破片1つ分（形は借り物、ここは置き場所と動きだけを持つ）
  struct Piece {
    std::unique_ptr<Object3d> object = nullptr;    // 外側の面
    std::unique_ptr<Object3d> capObject = nullptr; // 割れ口の面（なければ nullptr）
    Vector3 position = {0.0f, 0.0f, 0.0f};        // 今の位置
    Vector3 velocity = {0.0f, 0.0f, 0.0f};        // 1秒あたりに進む量
    Vector3 rotation = {0.0f, 0.0f, 0.0f};        // 今の回転角（ラジアン）
    Vector3 angularVelocity = {0.0f, 0.0f, 0.0f}; // 1秒あたりに回る量（ラジアン）
    Vector3 localCenter = {0.0f, 0.0f, 0.0f};     // 箱の中での重心（箱の大きさをかけた後）
    Vector3 localOutward = {0.0f, 0.0f, 0.0f};    // 箱の中での外向き（開く・飛ぶ向き。長さ1）
    float openRate = 0.0f;                         // 割れ目の開いた割合（0〜1）
    const Model::ModelData *capShape = nullptr;    // 割れ口の形（炎を出す所を選ぶ。なければ nullptr）
    float flameCarry = 0.0f;                       // 出し切れなかった炎の数の端数
    float smokeCarry = 0.0f;                       // 出し切れなかった黒煙の数の端数
  };

private: // メンバ関数
  /// <summary>
  /// 破片を形セットの順に作り、見た目を決める（置き場所はまだ決めない）
  /// </summary>
  void CreatePieces(Object3dRenderer *renderer, const Vector3 &scale, const Look &look);

  /// <summary>
  /// 破片の位置と回転を Object3d（外側の面・割れ口の面）に反映する
  /// </summary>
  void ApplyTransform(Piece &piece);

  /// <summary>
  /// 放したあとの 1 フレーム分、割れ口を冷まし、割れ口から炎と黒煙を出す
  /// </summary>
  void UpdateBurning(Piece &piece);

  /// <summary>
  /// 割れ口の面の上の点を 1 つ選ぶ（ワールド座標）
  /// </summary>
  Vector3 PickCapPoint(const Piece &piece) const;

private: // メンバ変数
  // 破片
  std::vector<Piece> pieces_;

  // 破片の形（借り物。割れ目の線を読む）
  const FracturePieceSet *pieceSet_ = nullptr;

  // 箱の大きさ（形セットの座標を箱の中の座標に直す）
  Vector3 scale_ = {1.0f, 1.0f, 1.0f};

  // 最後に置いた箱の中 → ワールドの行列（つかんでいる間）
  Matrix4x4 boxWorld_{};

  // 最後に開いた割れ目の、開ききったときの幅（m）
  float openGap_ = 0.0f;

  // 割れ口を光らせる色（なければ光らせない）
  std::optional<Vector4> capGlowColor_;

  // 外向きの速さ・角速度の最大値（Release のときに使う）
  float explosionSpeed_ = 0.0f;
  float maxAngularSpeed_ = 0.0f;

  // つかんでいるか
  bool isHeld_ = false;

  // 放したあとの燃え方（なければ燃えない）
  std::optional<Burning> burning_;

  // 放してからの時間（秒）と、放したときの寿命（秒）
  float releasedTime_ = 0.0f;
  float lifetime_ = 0.0f;

  // 重力加速度
  Vector3 gravity_ = {0.0f, 0.0f, 0.0f};

  // 空気抵抗（1秒あたりに速度が減る割合）
  float drag_ = 0.0f;

  // 残り寿命（秒）
  float lifeTimer_ = 0.0f;
};
