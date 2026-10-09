#pragma once

// ゲーム全体で共通の時間の定数
// 値を変えるときはここ1か所だけを直せば、使っている所すべてに反映される

// 1フレームの時間（秒）。このゲームは 60fps 固定として計算する
// 「1秒あたりの量（速度・重力など）× kFixedDeltaTime」で1フレーム分の量になる
inline constexpr float kFixedDeltaTime = 1.0f / 60.0f;
