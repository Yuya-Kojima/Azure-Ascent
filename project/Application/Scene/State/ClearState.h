#pragma once
#include "Scene/State/GamePlayState.h"
#include <optional>

/// <summary>
/// クリアの状態。ボスへの最後の一撃からならクリア演出を、ボスなしならすぐにリザルトを出す。自機の操作は受けない
/// </summary>
class ClearState : public GamePlayState {
public:
  /// <summary>
  /// ボスなしでレールが終わったときのクリア
  /// </summary>
  explicit ClearState(GamePlayScene &scene) : GamePlayState(scene) {}

  /// <summary>
  /// ボスへの最後の一撃からのクリア
  /// </summary>
  ClearState(GamePlayScene &scene, const BossLastHit &hit)
      : GamePlayState(scene), bossLastHit_(hit) {}

  void Enter() override;
  std::unique_ptr<GamePlayState> Update() override;

  float GetWorldTimeScale() const override;
  const ICamera *UpdateSequence(bool canMove) override;

  bool AcceptsPlayerControl() const override { return false; }

  float GetHudAlpha(float playAlpha) const override;
  bool ShowsHud() const override;
  void DrawBehindCards() override;

private:
  std::optional<BossLastHit> bossLastHit_; // ボスへの最後の一撃（ボスなしのクリアでは空）
};
