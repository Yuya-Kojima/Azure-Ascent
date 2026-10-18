#pragma once
#include "Matrix4x4.h"
#include "Transform.h"
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Quaternion.h"
#include <cmath>
#include <numbers>

//=========================
// Vector3
//=========================

inline float LengthSq(const Vector3 &v) {
  return v.x * v.x + v.y * v.y + v.z * v.z;
}

inline float Length(const Vector3 &v) { return std::sqrt(LengthSq(v)); }

inline float Dot(const Vector3 &a, const Vector3 &b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vector3 Cross(const Vector3 &a, const Vector3 &b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// ゼロ除算に安全な正規化（長さが極小なら {0,0,0} を返す）
inline Vector3 SafeNormalize(const Vector3 &v, float eps = 1e-6f) {
  float lsq = LengthSq(v);
  if (lsq <= eps * eps)
    return {0.0f, 0.0f, 0.0f};
  float invLen = 1.0f / std::sqrt(lsq);
  return {v.x * invLen, v.y * invLen, v.z * invLen};
}

// そのまま正規化（ゼロに厳密でない場合）
inline Vector3 Normalize(const Vector3 &v) {
  float len = Length(v);
  return (len > 0.0f) ? Vector3{v.x / len, v.y / len, v.z / len}
                      : Vector3{0.0f, 0.0f, 0.0f};
}

inline float Lerp(float a, float b, float t) {
  return a + (b - a) * t;
}

inline Vector3 Lerp(const Vector3 &a, const Vector3 &b, float t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

// 角度の補間（差を -π〜π に丸めて、近い向きに回る）
inline float LerpAngle(float a, float b, float t) {
  float diff = std::remainder(b - a, 2.0f * std::numbers::pi_v<float>);
  return a + diff * t;
}

// from から to を向く回転角（x: ピッチ, y: ヨー, z: 0）
// この回転で正面 (0,0,1) は (cos x * sin y, -sin x, cos x * cos y) を向く
inline Vector3 LookRotation(const Vector3 &from, const Vector3 &to) {
  Vector3 d = {to.x - from.x, to.y - from.y, to.z - from.z};
  float horizontal = std::sqrt(d.x * d.x + d.z * d.z);
  return {std::atan2(-d.y, horizontal), std::atan2(d.x, d.z), 0.0f};
}

// 4つの制御点と進行度t(0~1)からCatmull-Rom曲線上の座標を計算する
inline Vector3 CatmullRom(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t) {
  float t2 = t * t;
  float t3 = t2 * t;

  Vector3 result;
  result.x = 0.5f * (2.0f * p1.x + (-p0.x + p2.x) * t + (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 + (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3);
  result.y = 0.5f * (2.0f * p1.y + (-p0.y + p2.y) * t + (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 + (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3);
  result.z = 0.5f * (2.0f * p1.z + (-p0.z + p2.z) * t + (2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z) * t2 + (-p0.z + 3.0f * p1.z - 3.0f * p2.z + p3.z) * t3);
  return result;
}

Quaternion Slerp(const Quaternion& q0, const Quaternion& q1, float t);

/// <summary>
/// 回転の合成（ハミルトン積）。結果は rhs で回してから lhs で回す回転
/// </summary>
Quaternion Multiply(const Quaternion& lhs, const Quaternion& rhs);

/// <summary>
/// 軸と角度から回転を作る
/// </summary>
/// <param name="axis">回転軸（長さ1）</param>
/// <param name="angle">角度（ラジアン）</param>
Quaternion MakeRotateAxisAngleQuaternion(const Vector3& axis, float angle);

/// <summary>
/// 共役。長さ 1 の回転なら逆向きの回転になる
/// </summary>
Quaternion Conjugate(const Quaternion& q);

/// <summary>
/// ベクトルを回転で回す（v' = q v q*）
/// </summary>
Vector3 RotateVector(const Quaternion& q, const Vector3& v);

/// <summary>
/// 向き from を向き to へ回す最短の回転（どちらも長さ 1）
/// </summary>
Quaternion MakeRotationFromTo(const Vector3& from, const Vector3& to);

/// <summary>
/// 回転の角度を maxAngle（ラジアン）までに抑える。軸はそのまま
/// </summary>
Quaternion ClampRotationAngle(Quaternion q, float maxAngle);

//=========================
// Collision (Intersection)
//=========================
struct Ray;
struct Sphere;
// レイと球の交差判定。ヒットした場合は true を返し、outDistance にレイの始点から交点までの距離を格納する（任意）
bool IsCollision(const Ray& ray, const Sphere& sphere, float* outDistance = nullptr);

//=========================
// Matrix4x4
//=========================

/// <summary>
/// 単位行列作成
/// </summary>
/// <returns>単位行列</returns>
Matrix4x4 MakeIdentity4x4();

/// <summary>
/// 行列の積を求める
/// </summary>
/// <param name="matrix1">行列</param>
/// <param name="matrix2">行列</param>
/// <returns>積</returns>
Matrix4x4 Multiply(Matrix4x4 matrix1, Matrix4x4 matrix2);

// 平行移動行列
Matrix4x4 MakeTranslateMatrix(const Vector3 &translate);

// 拡縮行列
Matrix4x4 MakeScaleMatrix(const Vector3 &scale);

/// <summary>
/// X軸回転行列作成
/// </summary>
/// <param name="rotateX"></param>
/// <returns></returns>
Matrix4x4 MakeRotateXMatrix(const float &rotateX);

/// <summary>
/// Y軸回転行列作成
/// </summary>
/// <param name="rotateY"></param>
/// <returns></returns>
Matrix4x4 MakeRotateYMatrix(const float &rotateY);

/// <summary>
/// Z軸回転行列作成
/// </summary>
/// <param name="rotateZ"></param>
/// <returns></returns>
Matrix4x4 MakeRotateZMatrix(const float &rotateZ);

/// <summary>
/// 回転行列生成
/// </summary>
/// <param name="rotate"></param>
/// <returns></returns>
Matrix4x4 MakeRotateMatrix(const Vector3 &rotate);

/// <summary>
/// アフィン行列作成
/// </summary>
/// <param name="scale">拡縮</param>
/// <param name="rotate">回転</param>
/// <param name="translate">平行移動</param>
/// <returns>アフィン行列</returns>
Matrix4x4 MakeAffineMatrix(Vector3 scale, Vector3 rotate, Vector3 translate);

/// <summary>
/// アフィン行列作成(Quaternion版)
/// </summary>
/// <param name="scale">拡縮</param>
/// <param name="rotate">回転(Quaternion)</param>
/// <param name="translate">平行移動</param>
/// <returns>アフィン行列</returns>
Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Quaternion& rotate, const Vector3& translate);

/// <summary>
/// ビュー行列（LookAt）作成
/// </summary>
Matrix4x4 MakeLookAtMatrix(const Vector3& pos, const Vector3& target, const Vector3& up);

/// <summary>
/// 透視投影行列作成
/// </summary>
/// <param name="fovY">画角</param>
/// <param name="aspectRatio">アスペクト比</param>
/// <param name="nearClip">近平面</param>
/// <param name="farClip">遠平面</param>
/// <returns>透視投影行列</returns>
Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio,
                                   float nearClip, float farClip);

/// <summary>
/// 正射影行列(平行投影行列)作成
/// </summary>
/// <param name="left">左端</param>
/// <param name="top">上端</param>
/// <param name="right">右端</param>
/// <param name="bottom">下端</param>
/// <param name="nearClip">近平面</param>
/// <param name="farClip">遠平面</param>
/// <returns>正射影行列</returns>
Matrix4x4 MakeOrthographicMatrix(float left, float top, float right,
                                 float bottom, float nearClip, float farClip);

/// <summary>
/// 4x4逆行列生成
/// </summary>
/// <param name="matrix">4x4正則行列</param>
/// <returns>逆行列</returns>
Matrix4x4 Inverse(Matrix4x4 matrix);

/// <summary>
/// 行列の回転の部分を回転（クォータニオン）に直す。行ごとに長さ 1 にして拡大を取り除く
/// </summary>
Quaternion MakeRotationFromMatrix(const Matrix4x4& matrix);

// 座標の変換（回転・スケール・平行移動を適用）
inline Vector3 TransformPoint(const Vector3 &v, const Matrix4x4 &m) {
  Vector3 result;
  result.x = v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0] + m.m[3][0];
  result.y = v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1] + m.m[3][1];
  result.z = v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] + m.m[3][2];
  float w = v.x * m.m[0][3] + v.y * m.m[1][3] + v.z * m.m[2][3] + m.m[3][3];
  if (w != 0.0f && w != 1.0f) {
    result.x /= w;
    result.y /= w;
    result.z /= w;
  }
  return result;
}

// 法線の変換（回転・スケールのみ適用／平行移動は無視）
Vector3 TransformNormal(const Vector3 &v, const Matrix4x4 &m);

// ワールド座標からスクリーン座標への変換（ロックオンなどに使用）
Vector2 WorldToScreen(const Vector3& worldPos, const Matrix4x4& viewProjMatrix, float screenWidth, float screenHeight);

/// <summary>
/// 2Dスクリーン上の2点間を結ぶスプライト設定データ
/// </summary>
struct Line2DTransform {
  Vector2 position; // スプライトの中心位置
  Vector2 size;     // スプライトのサイズ (X: 直線の長さ, Y: 線の太さ)
  float rotation;   // 回転角度（ラジアン）
};

/// <summary>
/// 2点 P1, P2 から直線のトランスフォーム（位置・サイズ・回転）を算出する
/// </summary>
inline Line2DTransform CalculateLine2DTransform(const Vector2& p1, const Vector2& p2, float thickness) {
  Vector2 diff = { p2.x - p1.x, p2.y - p1.y };
  float length = std::sqrt(diff.x * diff.x + diff.y * diff.y);
  float angle = std::atan2(diff.y, diff.x);
  Vector2 center = { (p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f };

  return Line2DTransform{ center, { length, thickness }, angle };
}

Matrix4x4 Transpose(Matrix4x4 matrix);

static float DegToRad(float deg) { return deg * 3.14159265f / 180.0f; }

//=========================
// Easing
//=========================

// 線形
inline float EaseLinear(float t) { return t; }

// 加速
inline float EaseInQuad(float t) { return t * t; }

// 減速
inline float EaseOutQuad(float t) { return 1.0f - (1.0f - t) * (1.0f - t); }

// 加速 → 減速
inline float EaseInOutQuad(float t) {
  if (t < 0.5f) {
    return 2.0f * t * t;
  }
  return 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f;
}

// -------- cubic --------
inline float EaseInCubic(float t) { return t * t * t; }

inline float EaseOutCubic(float t) {
  float u = 1.0f - t;
  return 1.0f - u * u * u;
}

inline float EaseInOutCubic(float t) {
  if (t < 0.5f) {
    return 4.0f * t * t * t;
  }
  float u = -2.0f * t + 2.0f;
  return 1.0f - (u * u * u) * 0.5f;
}

// -------- quint --------
inline float EaseOutQuint(float t) {
  float u = 1.0f - t;
  return 1.0f - u * u * u * u * u;
}

/// <summary>
/// RGBをHSVに変換する
/// </summary>
/// <param name="rgb">RGB (0.0f～1.0f)</param>
/// <returns>HSV (H: 0.0f～360.0f, S: 0.0f～1.0f, V: 0.0f～1.0f)</returns>
Vector3 RGBToHSV(const Vector3& rgb);

/// <summary>
/// HSVをRGBに変換する
/// </summary>
/// <param name="hsv">HSV (H: 0.0f～360.0f, S: 0.0f～1.0f, V: 0.0f～1.0f)</param>
/// <returns>RGB (各0.0f～1.0f)</returns>
Vector3 HSVToRGB(const Vector3& hsv);
