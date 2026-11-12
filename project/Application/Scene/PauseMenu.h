#pragma once
#include "Scene/UiPlate.h"
#include <array>
#include <memory>

class Input;
class Sprite;
class SpriteRenderer;

/// <summary>
/// プレイ中のポーズ画面（右の暗い板）
/// 「再開」「リトライ」「拠点へ戻る」を縦に並べ、リトライ・拠点へ戻るは
/// 「今回の記録は残りません」と「はい」「いいえ」の確認を挟んでから選んだことにする
/// </summary>
class PauseMenu {
public:
  /// <summary>
  /// 選んだもの
  /// </summary>
  enum class Choice {
    None,   // まだ選んでいない
    Resume, // 再開（板を閉じる）
    Again,  // リトライ（確認ではいを選んだ）
    Base,   // 拠点へ戻る（確認ではいを選んだ）
  };

  PauseMenu();
  ~PauseMenu();

  void Initialize(SpriteRenderer *spriteRenderer);

  /// <summary>
  /// 板を開く（カーソルは「再開」）
  /// </summary>
  void Open();

  /// <summary>
  /// 選ばずに板を閉じる
  /// </summary>
  void Close();

  /// <summary>
  /// 更新（開いている間だけ操作を受ける）
  /// </summary>
  void Update(Input *input);

  /// <summary>
  /// 開いているか（リトライ・拠点へ戻るを選んだあとも、シーンが切り替わるまで開いたまま）
  /// </summary>
  bool IsOpen() const { return isOpen_; }

  /// <summary>
  /// 選んだもの（一度だけ返し、そのあとは None）
  /// </summary>
  Choice ConsumeChoice();

  void Draw();

private:
  // 板の中身
  enum class Page {
    Menu,    // 3 つの選択肢
    Confirm, // リトライ・拠点へ戻るの確認
  };
  // 3 つの選択肢の並び（上から）
  static constexpr int kItemCount = 3;
  static constexpr int kResumeIndex = 0;
  static constexpr int kAgainIndex = 1;
  static constexpr int kBaseIndex = 2;

  void UpdateMenuPage(Input *input);
  void UpdateConfirmPage(Input *input);
  void ShowPage(Page page);
  void UpdateSprites();

  bool isOpen_ = false;
  float timer_ = 0.0f;     // 開いてからの時間（秒）
  float pageTimer_ = 0.0f; // 今の中身に変わってからの時間（秒）
  Page page_ = Page::Menu;
  Choice pendingChoice_ = Choice::None;
  bool hasChosen_ = false; // リトライ・拠点へ戻るを選び終わったか（そのあとは操作を受けない）
  int focused_ = kResumeIndex;
  float focusPos_ = 0.0f; // 下線の位置（選択肢の番号。0: 再開 〜 2: 拠点へ戻る）
  bool isConfirmYes_ = false;
  float confirmPos_ = 1.0f; // 確認の下線の位置（0: はい 〜 1: いいえ）

  std::unique_ptr<Sprite> dim_; // 画面を暗くする黒
  UiPlate plate_;
  std::unique_ptr<Sprite> title_;
  std::unique_ptr<Sprite> titleLine_;
  std::array<std::unique_ptr<Sprite>, kItemCount> items_;
  std::unique_ptr<Sprite> warn_;
  std::unique_ptr<Sprite> askAgain_;
  std::unique_ptr<Sprite> askBase_;
  std::unique_ptr<Sprite> yes_;
  std::unique_ptr<Sprite> no_;
  std::unique_ptr<Sprite> underline_;
};
