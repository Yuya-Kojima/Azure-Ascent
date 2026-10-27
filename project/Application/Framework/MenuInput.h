#pragma once

class Input;

/// <summary>
/// メニュー・演出で使う決定・選択の入力（全シーン共通のキー割り当て）
/// </summary>
namespace MenuInput {

/// <summary>
/// 決定（スペース・パッドの A）。シーンの切り替え中は false
/// </summary>
bool IsDecideTriggered(Input *input);

/// <summary>
/// 演出を飛ばす長押しの入力を押している間（スペース・パッドの A・Start）
/// </summary>
bool IsSkipHeld(Input *input);

/// <summary>
/// 上（W・↑・十字キー上）
/// </summary>
bool IsUpTriggered(Input *input);

/// <summary>
/// 下（S・↓・十字キー下）
/// </summary>
bool IsDownTriggered(Input *input);

/// <summary>
/// 左（A・←・十字キー左）
/// </summary>
bool IsLeftTriggered(Input *input);

/// <summary>
/// 右（D・→・十字キー右）
/// </summary>
bool IsRightTriggered(Input *input);

} // namespace MenuInput
