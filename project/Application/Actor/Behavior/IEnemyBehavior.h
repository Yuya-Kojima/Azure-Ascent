#pragma once

class Enemy;

// 敵の振る舞い（AI）を定義する基底インターフェース
// Strategy Pattern を用いて、Enemyクラス本体から移動・思考ロジックを分離する
class IEnemyBehavior {
public:
  virtual ~IEnemyBehavior() = default;

  /// <summary>
  /// 毎フレーム呼ばれる更新処理
  /// </summary>
  /// <param name="enemy"> 操作対象となる敵本体のポインタ</param>
  virtual void Update(Enemy *enemy) = 0;

  /// <summary>
  /// ロックオン可能かどうか（デフォルトは常に可能）
  /// </summary>
  virtual bool CanBeLockedOn() const { return true; }
};
