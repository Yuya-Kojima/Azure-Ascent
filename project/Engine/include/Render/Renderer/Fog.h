#pragma once

#include "Math/MathUtil.h"

struct FogData {
    Vector4 color;    // 霧の色（RGBA）
    float nearDist;   // 霧がかかり始める距離
    float farDist;    // 完全に霧の色になる距離
    float enabled;    // 1.0f = 有効, 0.0f = 無効
    float padding;    // 16バイトアライメント用
    // 晴れの円（空と同じ円）。円があるときは、見る向きが円の外側なら color、内側なら clearColor の霧にする
    Vector4 clearColor = {0.0f, 0.0f, 0.0f, 1.0f};  // rgb: 円の内側の霧の色
    Vector4 clearCircle = {0.0f, 1.0f, 0.0f, 0.0f}; // xyz: 円の中心の向き, w: 半径（ラジアン。0 以下で晴れなし）
    Vector4 clearEdge = {0.0f, 0.0f, 0.0f, 0.0f};   // x: 境目のぼかしの幅, y: 外側で闇が濃くなる帯の幅, z: 濃くなる強さ, w: 境目をノイズで揺らす幅（ラジアン）
    float skyTime = 0.0f;                           // 空の時刻（秒。境目を空と同じノイズで揺らす）
    float clearPadding[3] = {};                     // 16バイトアライメント用
};
