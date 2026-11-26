#pragma once

// ボスの形態変化と撃破で、画面から消される物
class IClearedByBoss {
public:
  virtual ~IClearedByBoss() = default;

  // ボスの形態変化が始まった。消える
  virtual void OnBossTransition() = 0;

  // ボスが撃破された。誘爆して消える
  virtual void OnBossDefeated() = 0;
};
