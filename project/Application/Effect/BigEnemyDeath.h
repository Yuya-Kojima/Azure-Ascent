#pragma once
#include "Math/Matrix4x4.h"
#include "Math/Vector3.h"
#include <random>
#include <vector>

class FractureDebris;

/// <summary>
/// 大物（浮遊要塞・大蛇）の死亡演出。割れ目が走る → 大爆発 → 余韻の順に進める
/// 粒は EffectManager の層ごとの入れ物（閃光・炎・火花・黒煙・燃え続ける炎）に出す
/// 値は resources/config/BigEnemyDeathConfig.json に保存し、ImGui から調整する
/// </summary>
class BigEnemyDeath {
public:
  // 粒の層（EffectManager の入れ物の順）
  enum class Layer {
    Flash, // 閃光（足し算。大きく一瞬）
    Fire,  // 炎（足し算。飛び出して急に遅くなり、暗くなって消える）
    Spark, // 火花（足し算。四方へ飛んで落ちる）
    Smoke, // 黒煙（半透明。膨らみながら昇り、風に流れる）
    Flame, // 燃え続ける炎（足し算。炎の描き方。持ち主についていきながら昇る）
    Count
  };
  static constexpr int kLayerCount = static_cast<int>(Layer::Count);

  // 外へ知らせる出来事
  enum class Event {
    Stop,  // 倒された瞬間（ヒットストップ）
    Blast  // 大爆発（カメラを揺らす）
  };

  // 層ごとの見た目と、層の全部の粒にかかる力
  struct LayerConfig {
    float weightMin = 1.0f;    // 重さの最小
    float weightMax = 1.0f;    // 重さの最大
    float scaleMin = 1.0f;     // 大きさの最小（m）
    float scaleMax = 1.0f;     // 大きさの最大（m）
    float lifeMin = 1.0f;      // 寿命の最小（秒）
    float lifeMax = 1.0f;      // 寿命の最大（秒）
    float scaleEndRate = 1.0f; // 寿命の終わりの大きさの倍率
    float fadeStart = 0.0f;    // 薄くなり始める寿命の割合
    float spin = 0.0f;         // 回る速さの最大（ラジアン/秒）
    Vector3 colorStart = {1.0f, 1.0f, 1.0f}; // 生まれたときの色
    Vector3 colorEnd = {1.0f, 1.0f, 1.0f};   // 寿命の終わりの色
    Vector3 gravity = {0.0f, 0.0f, 0.0f};    // 重力の加速度（m/秒²。上向きなら昇る）
    Vector3 wind = {0.0f, 0.0f, 0.0f};       // 風の速さ（m/秒）
    float drag = 0.0f;                       // 空気の抵抗の強さ（1/秒）
  };

  // 調整値
  struct Config {
    // 時間
    float crackSeconds = 1.0f;     // 倒されてから大爆発までの秒数（割れ目が走る長さ）
    float aftermathSeconds = 2.5f; // 大爆発から終わりまでの秒数
    // 耐える間（倒されてから割れ始めるまで、体のあちこちで小さな爆発が起き、だんだん激しくなる）
    float damageIntervalStart = 0.6f; // 倒した直後の、小さな爆発の間隔（秒）
    float damageIntervalEnd = 0.2f;   // 激しくなりきったときの、小さな爆発の間隔（秒）
    float damageScaleStart = 0.6f;    // 倒した直後の、小さな爆発の大きさの倍率
    float damageScaleEnd = 1.0f;      // 激しくなりきったときの、小さな爆発の大きさの倍率
    float damageRampSeconds = 1.5f;   // 倒してから、激しくなりきるまでの秒数
    float damageFinalScale = 2.0f;    // 割れ始めの瞬間に、割れ目の出発点で起きる爆発の大きさの倍率
    float damageFlashScale = 0.4f;    // 閃光の大きさ（閃光の層の大きさに対する倍率）
    int damageFireCount = 6;          // 火の玉の粒の数
    float damageFireSpeed = 4.0f;     // 火の玉が面の外へふくらむ速さ（m/秒）
    float damageFireSpread = 1.5f;    // 火の玉の粒を散らす範囲（m）
    float damageFireScale = 0.5f;     // 火の玉の大きさ（炎の層の大きさに対する倍率）
    int damageSparkCount = 0;         // 火花の数
    float damageSparkSpeed = 25.0f;   // 火花が面の外へ飛ぶ速さ（m/秒）
    int damageSmokeCount = 6;         // 黒煙の数
    float damageSmokeSpeed = 4.0f;    // 黒煙が面の外へ出る速さ（m/秒）
    float damageSmokeScale = 0.8f;    // 黒煙の大きさ（黒煙の層の大きさに対する倍率）
    float damageSmokeInherit = 0.2f;  // 黒煙が持って生まれる、持ち主の速さの割合（小さいほど後ろに残る）
    // 割れ目（前線は割れ目が走る長さで、撃ち抜いた点から体の一番遠い所まで走る）
    float crackOpenWidth = 6.0f;   // 前線が塊の重心を過ぎてから、割れ目が開ききるまでの距離（m）
    float crackGap = 0.6f;         // 開ききった塊が外へずれる幅（m）
    float crackSparkRate = 600.0f; // 開いた割れ目から噴く火花の数（1 秒あたり）
    float crackSparkSpeed = 30.0f; // 火花が面の外へ噴く速さ（m/秒）
    float crackSparkSpread = 8.0f; // 火花の速さのばらつき（m/秒）
    float crackSmokeRate = 40.0f;  // 開いた割れ目から噴く煙の数（1 秒あたり）
    float crackSmokeSpeed = 6.0f;  // 煙が面の外へ噴く速さ（m/秒）
    float crackSmokeSpread = 2.0f; // 煙の速さのばらつき（m/秒）
    float crackSmokeScale = 0.4f;  // 煙の大きさ（黒煙の層の大きさに対する倍率）
    // 火花の見た目（火花の層の全部の粒）
    float sparkStreakSeconds = 0.1f;  // 火花の線の長さを「画面に沿った速さ × この秒数」にする
    float sparkStreakWidthRate = 0.25f; // 火花の線の半分の太さ（火花の層の大きさに対する倍率）
    // 燃え続ける炎
    float flameSpeed = 2.0f;       // 燃え続ける炎の速さのばらつき（m/秒）
    // 大爆発
    int blastFlashCount = 1;          // 閃光の芯の数
    float blastFlashScale = 2.8f;     // 閃光の芯の大きさ（閃光の層の大きさに対する倍率）
    float blastFlashLifeRate = 1.5f;  // 閃光の芯の寿命（閃光の層の寿命に対する倍率）
    float blastFlashIntensity = 4.0f; // 閃光の芯の明るさ（閃光の層の色に対する倍率。1 を超えた分が光の漏れになる）
    int blastGlowCount = 2;           // 芯のまわりに広がる光の数（閃光の層に出す）
    float blastGlowScale = 1.8f;      // まわりの光の大きさ（閃光の層の大きさに対する倍率）
    float blastGlowLifeRate = 2.0f;   // まわりの光の寿命（閃光の層の寿命に対する倍率）
    float blastGlowIntensity = 2.0f;  // まわりの光の明るさ（閃光の層の色に対する倍率）
    float blastFlashTowardCamera = 25.0f; // 閃光と光を、体の中心からカメラの方へずらす距離（m。カメラまでの半分を超えない）
    bool blastShockwave = true;       // 大爆発の中心から空気のゆがみの輪を広げるか
    int blastFireCount = 80;        // 割れ目から噴く炎の数（1 粒ずつ出す頼みになるので、1 フレームの頼みの上限 256 より少なくする）
    float blastFireSpeed = 25.0f;   // 炎が面の外へ噴く速さ（m/秒）
    float blastFireSpread = 6.0f;   // 炎の速さのばらつき（m/秒）
    float blastFireScale = 1.2f;    // 炎の大きさ（炎の層の大きさに対する倍率）
    int blastSparkCount = 800;      // 火花の数
    float blastSparkSpeed = 70.0f;  // 火花の速さ（m/秒）
    int blastSmokeCount = 120;      // 黒煙の数
    float blastSmokeSpeed = 8.0f;   // 黒煙の速さ（m/秒）
    float blastSmokeScale = 1.5f;   // 黒煙の大きさ（黒煙の層の大きさに対する倍率）
    float blastSmokeDelay = 0.15f;  // 大爆発から黒煙を出すまでの秒数
    int blastEmberCount = 150;       // 火の粉の数（火花の層に出す）
    float blastEmberSpeed = 20.0f;   // 火の粉の速さ（m/秒）
    float blastEmberLifeRate = 2.5f; // 火の粉の寿命（火花の層の寿命に対する倍率）
    float blastSpreadRate = 0.6f;   // 炎・火花・黒煙を散らす範囲（体の一番大きい箱の半分の大きさに対する倍率）
    // 塊（大爆発で体の箱をボロノイで割った破片）
    int chunkSeedCount = 4;          // 箱 1 個を割る数（反映されるのは次に出た敵から）
    int chunkLargeSeedCount = 24;    // 一番大きい箱を割る数（反映されるのは次に出た敵から）
    float chunkSpeed = 60.0f;        // 塊が外向きに飛ぶ速さ（m/秒）
    float chunkGravity = 80.0f;      // 塊が落ちる加速度（m/秒²）
    float chunkSpin = 1.0f;          // 塊が回る速さの最大（ラジアン/秒）
    float chunkLifetime = 3.0f;      // 塊が消えるまでの秒数
    float chunkDrag = 2.0f;          // 塊の空気の抵抗（1 秒あたりに速さが減る割合）
    float chunkColorRate = 1.0f;     // 塊の外側の色（体の色に対する倍率）
    Vector3 chunkCapColor = {1.0f, 0.25f, 0.05f}; // 塊の割れ口が光る色
    float chunkCapIntensity = 3.0f;  // 塊の割れ口の明るさ（1 を超えた分が光の漏れになる）
    float chunkCoolSeconds = 1.0f;   // 大爆発から割れ口が冷めきるまでの秒数
    float chunkCooledRate = 0.08f;   // 冷めきった割れ口の明るさ（割れ口の明るさに対する倍率）
    float chunkFlameRate = 30.0f;    // 塊 1 つの割れ口から出る炎の数（1 秒あたり。燃え始め）
    float chunkSmokeRate = 8.0f;     // 塊 1 つから出る黒煙の数（1 秒あたり。燃え始め）
    float chunkTrailInherit = 0.2f;  // 炎と黒煙が持って生まれる、塊の速さの割合（小さいほど後ろへ尾を引く）
    float chunkSmokeScale = 0.6f;    // 尾の黒煙の大きさ（黒煙の層の大きさに対する倍率）
    float chunkSmokeSpread = 2.0f;   // 尾の黒煙の速さのばらつき（m/秒）
    // 層（Layer の順）
    LayerConfig layers[kLayerCount] = {
        // 閃光
        {1.0f, 1.0f, 20.0f, 30.0f, 0.12f, 0.2f, 1.5f, 0.0f, 0.0f,
         {1.0f, 0.95f, 0.85f}, {1.0f, 0.5f, 0.2f},
         {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0.0f},
        // 炎
        {0.5f, 1.5f, 3.0f, 6.0f, 0.6f, 1.1f, 3.0f, 0.6f, 0.0f,
         {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f},
         {0.0f, 4.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 3.0f},
        // 火花
        {1.0f, 3.0f, 1.0f, 2.0f, 0.6f, 1.2f, 0.3f, 0.5f, 0.0f,
         {1.0f, 0.7f, 0.3f}, {0.6f, 0.1f, 0.02f},
         {0.0f, -25.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 1.0f},
        // 黒煙
        {0.3f, 0.8f, 10.0f, 20.0f, 2.0f, 3.5f, 2.5f, 0.3f, 0.5f,
         {0.1f, 0.095f, 0.095f}, {0.16f, 0.155f, 0.155f},
         {0.0f, 6.0f, 0.0f}, {9.0f, 0.0f, -2.5f}, 1.5f},
        // 燃え続ける炎
        {0.5f, 1.0f, 3.0f, 6.0f, 0.5f, 0.9f, 0.7f, 0.6f, 0.0f,
         {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f},
         {0.0f, 15.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0.4f},
    };
  };

  // 体の箱 1 つ
  struct Box {
    Matrix4x4 world;  // 箱の中 → ワールド（回転と位置だけ）
    Vector3 halfSize; // 半分の大きさ（m）
    FractureDebris *chunks; // 箱を割った塊（大爆発までつかんでおく。なければ nullptr）
  };

  /// <summary>
  /// 始める（倒された瞬間に 1 回）
  /// </summary>
  /// <param name="localHitPoint">撃ち抜いた点（持ち主の中の座標）</param>
  /// <param name="localHitNormal">撃ち抜いた点の面の外向き（持ち主の中の向き）</param>
  void Start(const Vector3 &localHitPoint, const Vector3 &localHitNormal);

  /// <summary>
  /// 1 フレーム進める（世界の時間が進むフレームだけ呼ぶ）
  /// </summary>
  /// <param name="ownerFrame">持ち主の中 → ワールドの行列（回転と位置だけ）</param>
  /// <param name="boxes">体の箱</param>
  /// <param name="ownerVelocity">持ち主の動く速さ（m/秒）</param>
  /// <param name="cameraPosition">カメラの位置（大爆発の閃光をカメラの方へずらすのに使う）</param>
  void Update(const Matrix4x4 &ownerFrame, const std::vector<Box> &boxes,
              const Vector3 &ownerVelocity, const Vector3 &cameraPosition);

  bool IsStarted() const { return phase_ != Phase::None; }

  /// <summary>
  /// 体が形を保っているか（大爆発の前まで。割れ目が走る間も含む）
  /// </summary>
  bool IsBodyVisible() const { return phase_ == Phase::None || phase_ == Phase::Crack; }

  bool IsFinished() const { return phase_ == Phase::Finished; }

  /// <summary>
  /// 耐える間の 1 フレーム分。間隔ごとに、体の箱の表面のどこかで小さな爆発（閃光・火の玉・黒煙）を起こす
  /// 耐えた時間が長いほど、間隔が短く、爆発が大きくなる
  /// </summary>
  /// <param name="boxes">体の箱</param>
  /// <param name="ownerVelocity">持ち主の動く速さ（m/秒）</param>
  void UpdateDamage(const std::vector<Box> &boxes, const Vector3 &ownerVelocity);

  /// <summary>
  /// 燃え続ける炎を 1 か所から出す（燃える塊の尾）
  /// </summary>
  /// <param name="position">出す所（ワールド座標）</param>
  /// <param name="velocity">燃えている物の速さ（m/秒）。炎はこの速さを持って生まれ、ついていく</param>
  /// <param name="count">炎の数</param>
  static void EmitFlame(const Vector3 &position, const Vector3 &velocity, int count);

  /// <summary>
  /// 燃える塊の尾の黒煙を 1 か所から出す
  /// </summary>
  /// <param name="position">出す所（ワールド座標）</param>
  /// <param name="velocity">黒煙が持って生まれる速さ（m/秒）</param>
  /// <param name="count">黒煙の数</param>
  static void EmitTrailSmoke(const Vector3 &position, const Vector3 &velocity, int count);

  /// <summary>
  /// 前の呼び出しから起きた出来事を取り出す
  /// </summary>
  std::vector<Event> TakeEvents();

  /// <summary>
  /// 設定を読む（なければ初期値のまま）
  /// </summary>
  static void LoadConfig(Config &config);

  /// <summary>
  /// 設定を保存する
  /// </summary>
  static void SaveConfig(const Config &config);

  /// <summary>
  /// 設定の ImGui
  /// </summary>
  /// <returns>値が変わったか</returns>
  static bool DrawConfigUI(Config &config);

private:
  enum class Phase {
    None,      // 始まっていない
    Crack,     // 割れ目が走る（体は塊に置き換わり、まだつながっている）
    Aftermath, // 大爆発のあとの余韻（体はもうない）
    Finished   // 終わり
  };

  static constexpr float kFrameTime_ = 1.0f / 60.0f; // 1 フレームの秒数

  /// <summary>
  /// 割れ目を 1 フレーム進める。前線を広げ、前線が過ぎた塊の割れ目を開き、開いた割れ目から火花と煙を噴く
  /// </summary>
  void UpdateCrack(const Config &config, const std::vector<Box> &boxes,
                   const Vector3 &hitPoint, const Vector3 &hitNormal, const Vector3 &ownerVelocity);

  /// <summary>
  /// 面の上の 1 点で、小さな爆発（閃光・火の玉・火花・黒煙）を 1 回起こす
  /// </summary>
  /// <param name="point">爆発する点（ワールド座標）</param>
  /// <param name="normal">その点の面の外向き</param>
  /// <param name="ownerVelocity">持ち主の動く速さ（m/秒）</param>
  /// <param name="scaleRate">閃光・火の玉・黒煙の大きさに掛ける倍率</param>
  static void EmitSmallExplosion(const Config &config, const Vector3 &point, const Vector3 &normal,
                                 const Vector3 &ownerVelocity, float scaleRate);

  /// <summary>
  /// 開いた割れ目の線の上の点から、面の外向きに層の粒を 1 つ噴く
  /// </summary>
  /// <param name="speed">面の外へ噴く速さ（m/秒）</param>
  /// <param name="spread">速さのばらつき（m/秒）</param>
  /// <param name="scaleRate">層の大きさに対する倍率</param>
  /// <returns>開いた割れ目がなく、噴けなかったら false</returns>
  bool EmitCrackJet(Layer layer, const std::vector<Box> &boxes, const Vector3 &ownerVelocity,
                    float speed, float spread, float scaleRate);

  /// <summary>
  /// 大爆発（1 回だけ）。体の中心から閃光・空気のゆがみの輪・火花・火の粉・黒煙、開いた割れ目から外へ炎
  /// 割れ目が開いていなければ、炎も体の中心から出す
  /// </summary>
  void Blast(const Config &config, const std::vector<Box> &boxes, const Vector3 &ownerVelocity,
             const Vector3 &cameraPosition);

  /// <summary>
  /// 点のまわりに、層の粒を四方へ出す
  /// </summary>
  /// <param name="spread">出す場所を散らす範囲（半分の大きさ m）</param>
  /// <param name="speed">四方へ飛ぶ速さの最大（m/秒）</param>
  /// <param name="scaleRate">層の大きさに対する倍率</param>
  /// <param name="lifeRate">層の寿命に対する倍率</param>
  /// <param name="intensity">層の色（始まりと終わり）に掛ける明るさの倍率</param>
  static void EmitBurst(Layer layer, const Vector3 &position, const Vector3 &spread,
                        int count, const Vector3 &baseVelocity, float speed,
                        float scaleRate = 1.0f, float lifeRate = 1.0f, float intensity = 1.0f);

  Phase phase_ = Phase::None;
  Vector3 localHitPoint_ = {0.0f, 0.0f, 0.0f};
  Vector3 localHitNormal_ = {0.0f, 1.0f, 0.0f};
  float timer_ = 0.0f;         // 今の段階に入ってからの時間（秒）
  float frontRadius_ = 0.0f;   // 今の前線の、撃ち抜いた点からの距離（m）
  float sparkCarry_ = 0.0f;    // 出し切れなかった割れ目の火花の数の端数
  float smokeCarry_ = 0.0f;    // 出し切れなかった割れ目の煙の数の端数
  bool hasCrackStarted_ = false; // 割れ始めの爆発を出したか
  bool hasBlastSmoked_ = false; // 大爆発の黒煙を出したか
  float damageTimer_ = 0.0f;    // 前の小さな爆発からの時間（秒）
  float damageElapsed_ = 0.0f;  // 耐え始めてからの時間（秒）
  Vector3 blastCenter_ = {0.0f, 0.0f, 0.0f}; // 大爆発の中心（ワールド座標）
  Vector3 blastSpread_ = {0.0f, 0.0f, 0.0f}; // 大爆発の粒を散らす範囲（半分の大きさ m）
  std::mt19937 random_{std::random_device{}()};
  std::vector<Event> events_;  // まだ取り出されていない出来事
};
