#include "Math/MathUtil.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include "Math/Geometry.h"

Quaternion Slerp(const Quaternion &q0, const Quaternion &q1, float t) {
  float dot = q0.x * q1.x + q0.y * q1.y + q0.z * q1.z + q0.w * q1.w;
  Quaternion tq1 = q1;
  if (dot < 0.0f) {
    dot = -dot;
    tq1 = {-q1.x, -q1.y, -q1.z, -q1.w};
  }
  if (dot >= 1.0f - 0.0005f) {
    return {q0.x + (tq1.x - q0.x) * t, q0.y + (tq1.y - q0.y) * t,
            q0.z + (tq1.z - q0.z) * t, q0.w + (tq1.w - q0.w) * t};
  }
  float theta = std::acos(dot);
  float sinTheta = std::sin(theta);
  float s0 = std::sin((1.0f - t) * theta) / sinTheta;
  float s1 = std::sin(t * theta) / sinTheta;
  return {q0.x * s0 + tq1.x * s1, q0.y * s0 + tq1.y * s1,
          q0.z * s0 + tq1.z * s1, q0.w * s0 + tq1.w * s1};
}

Quaternion Multiply(const Quaternion &lhs, const Quaternion &rhs) {
  return {lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
          lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
          lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
          lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z};
}

Quaternion MakeRotateAxisAngleQuaternion(const Vector3 &axis, float angle) {
  float s = std::sin(angle * 0.5f);
  return {axis.x * s, axis.y * s, axis.z * s, std::cos(angle * 0.5f)};
}

Quaternion Conjugate(const Quaternion &q) { return {-q.x, -q.y, -q.z, q.w}; }

Vector3 RotateVector(const Quaternion &q, const Vector3 &v) {
  const Vector3 axis = {q.x, q.y, q.z};
  const Vector3 t = Cross(axis, v) * 2.0f;
  return v + t * q.w + Cross(axis, t);
}

Quaternion MakeRotationFromTo(const Vector3 &from, const Vector3 &to) {
  // 軸は2つの向きに垂直、角度は2つの間の角
  const float dot = std::clamp(Dot(from, to), -1.0f, 1.0f);
  const Vector3 axis = Cross(from, to);
  const float axisLength = Length(axis);
  // 同じ向き、または逆向き（軸が決まらない）
  const float kParallelEpsilon = 1.0e-6f;
  if (axisLength < kParallelEpsilon) {
    if (dot > 0.0f) {
      return {0.0f, 0.0f, 0.0f, 1.0f};
    }
    // 逆向きは、from に垂直な好きな軸まわりに半回転
    Vector3 side = Cross(from, {1.0f, 0.0f, 0.0f});
    if (Length(side) < kParallelEpsilon) {
      side = Cross(from, {0.0f, 1.0f, 0.0f});
    }
    return MakeRotateAxisAngleQuaternion(Normalize(side),
                                         std::numbers::pi_v<float>);
  }
  return MakeRotateAxisAngleQuaternion(axis * (1.0f / axisLength),
                                       std::acos(dot));
}

Quaternion ClampRotationAngle(Quaternion q, float maxAngle) {
  // w を正にそろえると、角度が 0〜π の短い向きの回転になる
  if (q.w < 0.0f) {
    q = {-q.x, -q.y, -q.z, -q.w};
  }
  const float angle = 2.0f * std::acos((std::min)(q.w, 1.0f));
  if (angle <= maxAngle) {
    return q;
  }
  const Vector3 axis = SafeNormalize({q.x, q.y, q.z});
  return MakeRotateAxisAngleQuaternion(axis, maxAngle);
}

Quaternion MakeRotationFromMatrix(const Matrix4x4 &matrix) {
  // 行列の各行は、X・Y・Z の軸が回った先の向き
  float m[3][3];
  for (int row = 0; row < 3; ++row) {
    const Vector3 axis = SafeNormalize(
        {matrix.m[row][0], matrix.m[row][1], matrix.m[row][2]});
    m[row][0] = axis.x;
    m[row][1] = axis.y;
    m[row][2] = axis.z;
  }
  // 対角の和の大きい成分から求めて、割り算の誤差を抑える
  Quaternion q;
  const float trace = m[0][0] + m[1][1] + m[2][2];
  if (trace > 0.0f) {
    const float s = std::sqrt(trace + 1.0f) * 2.0f; // s = 4w
    q.w = 0.25f * s;
    q.x = (m[1][2] - m[2][1]) / s;
    q.y = (m[2][0] - m[0][2]) / s;
    q.z = (m[0][1] - m[1][0]) / s;
  } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
    const float s = std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2.0f; // s = 4x
    q.w = (m[1][2] - m[2][1]) / s;
    q.x = 0.25f * s;
    q.y = (m[0][1] + m[1][0]) / s;
    q.z = (m[2][0] + m[0][2]) / s;
  } else if (m[1][1] > m[2][2]) {
    const float s = std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2.0f; // s = 4y
    q.w = (m[2][0] - m[0][2]) / s;
    q.x = (m[0][1] + m[1][0]) / s;
    q.y = 0.25f * s;
    q.z = (m[1][2] + m[2][1]) / s;
  } else {
    const float s = std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2.0f; // s = 4z
    q.w = (m[0][1] - m[1][0]) / s;
    q.x = (m[2][0] + m[0][2]) / s;
    q.y = (m[1][2] + m[2][1]) / s;
    q.z = 0.25f * s;
  }
  return q;
}

Matrix4x4 MakeIdentity4x4() {
  Matrix4x4 matrix;

  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      if (i == j) {
        matrix.m[i][j] = 1.0f;
      } else {
        matrix.m[i][j] = 0.0f;
      }
    }
  }

  return matrix;
}

Matrix4x4 Multiply(Matrix4x4 matrix1, Matrix4x4 matrix2) {

  Matrix4x4 result;

  result.m[0][0] =
      matrix1.m[0][0] * matrix2.m[0][0] + matrix1.m[0][1] * matrix2.m[1][0] +
      matrix1.m[0][2] * matrix2.m[2][0] + matrix1.m[0][3] * matrix2.m[3][0];

  result.m[0][1] =
      matrix1.m[0][0] * matrix2.m[0][1] + matrix1.m[0][1] * matrix2.m[1][1] +
      matrix1.m[0][2] * matrix2.m[2][1] + matrix1.m[0][3] * matrix2.m[3][1];

  result.m[0][2] =
      matrix1.m[0][0] * matrix2.m[0][2] + matrix1.m[0][1] * matrix2.m[1][2] +
      matrix1.m[0][2] * matrix2.m[2][2] + matrix1.m[0][3] * matrix2.m[3][2];

  result.m[0][3] =
      matrix1.m[0][0] * matrix2.m[0][3] + matrix1.m[0][1] * matrix2.m[1][3] +
      matrix1.m[0][2] * matrix2.m[2][3] + matrix1.m[0][3] * matrix2.m[3][3];

  result.m[1][0] =
      matrix1.m[1][0] * matrix2.m[0][0] + matrix1.m[1][1] * matrix2.m[1][0] +
      matrix1.m[1][2] * matrix2.m[2][0] + matrix1.m[1][3] * matrix2.m[3][0];

  result.m[1][1] =
      matrix1.m[1][0] * matrix2.m[0][1] + matrix1.m[1][1] * matrix2.m[1][1] +
      matrix1.m[1][2] * matrix2.m[2][1] + matrix1.m[1][3] * matrix2.m[3][1];

  result.m[1][2] =
      matrix1.m[1][0] * matrix2.m[0][2] + matrix1.m[1][1] * matrix2.m[1][2] +
      matrix1.m[1][2] * matrix2.m[2][2] + matrix1.m[1][3] * matrix2.m[3][2];

  result.m[1][3] =
      matrix1.m[1][0] * matrix2.m[0][3] + matrix1.m[1][1] * matrix2.m[1][3] +
      matrix1.m[1][2] * matrix2.m[2][3] + matrix1.m[1][3] * matrix2.m[3][3];

  result.m[2][0] =
      matrix1.m[2][0] * matrix2.m[0][0] + matrix1.m[2][1] * matrix2.m[1][0] +
      matrix1.m[2][2] * matrix2.m[2][0] + matrix1.m[2][3] * matrix2.m[3][0];

  result.m[2][1] =
      matrix1.m[2][0] * matrix2.m[0][1] + matrix1.m[2][1] * matrix2.m[1][1] +
      matrix1.m[2][2] * matrix2.m[2][1] + matrix1.m[2][3] * matrix2.m[3][1];

  result.m[2][2] =
      matrix1.m[2][0] * matrix2.m[0][2] + matrix1.m[2][1] * matrix2.m[1][2] +
      matrix1.m[2][2] * matrix2.m[2][2] + matrix1.m[2][3] * matrix2.m[3][2];

  result.m[2][3] =
      matrix1.m[2][0] * matrix2.m[0][3] + matrix1.m[2][1] * matrix2.m[1][3] +
      matrix1.m[2][2] * matrix2.m[2][3] + matrix1.m[2][3] * matrix2.m[3][3];

  result.m[3][0] =
      matrix1.m[3][0] * matrix2.m[0][0] + matrix1.m[3][1] * matrix2.m[1][0] +
      matrix1.m[3][2] * matrix2.m[2][0] + matrix1.m[3][3] * matrix2.m[3][0];

  result.m[3][1] =
      matrix1.m[3][0] * matrix2.m[0][1] + matrix1.m[3][1] * matrix2.m[1][1] +
      matrix1.m[3][2] * matrix2.m[2][1] + matrix1.m[3][3] * matrix2.m[3][1];

  result.m[3][2] =
      matrix1.m[3][0] * matrix2.m[0][2] + matrix1.m[3][1] * matrix2.m[1][2] +
      matrix1.m[3][2] * matrix2.m[2][2] + matrix1.m[3][3] * matrix2.m[3][2];

  result.m[3][3] =
      matrix1.m[3][0] * matrix2.m[0][3] + matrix1.m[3][1] * matrix2.m[1][3] +
      matrix1.m[3][2] * matrix2.m[2][3] + matrix1.m[3][3] * matrix2.m[3][3];

  return result;
}

Matrix4x4 MakeTranslateMatrix(const Vector3 &translate) {

  Matrix4x4 result;

  result.m[0][0] = 1.0f;
  result.m[0][1] = 0.0f;
  result.m[0][2] = 0.0f;
  result.m[0][3] = 0.0f;
  result.m[1][0] = 0.0f;
  result.m[1][1] = 1.0f;
  result.m[1][2] = 0.0f;
  result.m[1][3] = 0.0f;
  result.m[2][0] = 0.0f;
  result.m[2][1] = 0.0f;
  result.m[2][2] = 1.0f;
  result.m[2][3] = 0.0f;
  result.m[3][0] = translate.x;
  result.m[3][1] = translate.y;
  result.m[3][2] = translate.z;
  result.m[3][3] = 1.0f;

  return result;
}

Matrix4x4 MakeScaleMatrix(const Vector3 &scale) {

  Matrix4x4 result;

  result.m[0][0] = scale.x;
  result.m[0][1] = 0.0f;
  result.m[0][2] = 0.0f;
  result.m[0][3] = 0.0f;
  result.m[1][0] = 0.0f;
  result.m[1][1] = scale.y;
  result.m[1][2] = 0.0f;
  result.m[1][3] = 0.0f;
  result.m[2][0] = 0.0f;
  result.m[2][1] = 0.0f;
  result.m[2][2] = scale.z;
  result.m[2][3] = 0.0f;
  result.m[3][0] = 0.0f;
  result.m[3][1] = 0.0f;
  result.m[3][2] = 0.0f;
  result.m[3][3] = 1.0f;

  return result;
}

Matrix4x4 MakeRotateXMatrix(const float &rotateX) {

  Matrix4x4 result;
  result.m[0][0] = 1.0f;
  result.m[0][1] = 0.0f;
  result.m[0][2] = 0.0f;
  result.m[0][3] = 0.0f;
  result.m[1][0] = 0.0f;
  result.m[1][1] = cosf(rotateX);
  result.m[1][2] = sinf(rotateX);
  result.m[1][3] = 0.0f;
  result.m[2][0] = 0.0f;
  result.m[2][1] = -sinf(rotateX);
  result.m[2][2] = cosf(rotateX);
  result.m[2][3] = 0.0f;
  result.m[3][0] = 0.0f;
  result.m[3][1] = 0.0f;
  result.m[3][2] = 0.0f;
  result.m[3][3] = 1.0f;

  return result;
}

Matrix4x4 MakeRotateYMatrix(const float &rotateY) {

  Matrix4x4 result;
  result.m[0][0] = cosf(rotateY);
  result.m[0][1] = 0.0f;
  result.m[0][2] = -sinf(rotateY);
  result.m[0][3] = 0.0f;
  result.m[1][0] = 0.0f;
  result.m[1][1] = 1.0f;
  result.m[1][2] = 0.0f;
  result.m[1][3] = 0.0f;
  result.m[2][0] = sinf(rotateY);
  result.m[2][1] = 0.0f;
  result.m[2][2] = cosf(rotateY);
  result.m[2][3] = 0.0f;
  result.m[3][0] = 0.0f;
  result.m[3][1] = 0.0f;
  result.m[3][2] = 0.0f;
  result.m[3][3] = 1.0f;

  return result;
}

Matrix4x4 MakeRotateZMatrix(const float &rotateZ) {

  Matrix4x4 result;
  result.m[0][0] = cosf(rotateZ);
  result.m[0][1] = sinf(rotateZ);
  result.m[0][2] = 0.0f;
  result.m[0][3] = 0.0f;
  result.m[1][0] = -sinf(rotateZ);
  result.m[1][1] = cosf(rotateZ);
  result.m[1][2] = 0.0f;
  result.m[1][3] = 0.0f;
  result.m[2][0] = 0.0f;
  result.m[2][1] = 0.0f;
  result.m[2][2] = 1.0f;
  result.m[2][3] = 0.0f;
  result.m[3][0] = 0.0f;
  result.m[3][1] = 0.0f;
  result.m[3][2] = 0.0f;
  result.m[3][3] = 1.0f;

  return result;
}

Matrix4x4 MakeRotateMatrix(const Vector3 &rotate) {

  Matrix4x4 rotateX = MakeRotateXMatrix(rotate.x);

  Matrix4x4 rotateY = MakeRotateYMatrix(rotate.y);

  Matrix4x4 rotateZ = MakeRotateZMatrix(rotate.z);

  Matrix4x4 result = Multiply(rotateX, Multiply(rotateY, rotateZ));

  return result;
}

Matrix4x4 MakeAffineMatrix(Vector3 scale, Vector3 rotate, Vector3 translate) {
  Matrix4x4 scaleMatrix = MakeScaleMatrix(scale);

  Matrix4x4 rotateMatrix = MakeRotateMatrix(rotate);

  Matrix4x4 translateMatrix = MakeTranslateMatrix(translate);

  Matrix4x4 worldMatrix;
  worldMatrix = Multiply(Multiply(scaleMatrix, rotateMatrix), translateMatrix);

  return worldMatrix;
}

Matrix4x4 MakeRotateMatrix(const Quaternion &quaternion) {
  Matrix4x4 result = MakeIdentity4x4();
  float xx = quaternion.x * quaternion.x;
  float yy = quaternion.y * quaternion.y;
  float zz = quaternion.z * quaternion.z;
  float ww = quaternion.w * quaternion.w;
  float xy = quaternion.x * quaternion.y;
  float xz = quaternion.x * quaternion.z;
  float xw = quaternion.x * quaternion.w;
  float yz = quaternion.y * quaternion.z;
  float yw = quaternion.y * quaternion.w;
  float zw = quaternion.z * quaternion.w;

  result.m[0][0] = ww + xx - yy - zz;
  result.m[0][1] = 2.0f * (xy + zw);
  result.m[0][2] = 2.0f * (xz - yw);
  result.m[0][3] = 0.0f;

  result.m[1][0] = 2.0f * (xy - zw);
  result.m[1][1] = ww - xx + yy - zz;
  result.m[1][2] = 2.0f * (yz + xw);
  result.m[1][3] = 0.0f;

  result.m[2][0] = 2.0f * (xz + yw);
  result.m[2][1] = 2.0f * (yz - xw);
  result.m[2][2] = ww - xx - yy + zz;
  result.m[2][3] = 0.0f;

  result.m[3][0] = 0.0f;
  result.m[3][1] = 0.0f;
  result.m[3][2] = 0.0f;
  result.m[3][3] = 1.0f;

  return result;
}

Matrix4x4 MakeAffineMatrix(const Vector3 &scale, const Quaternion &rotate,
                           const Vector3 &translate) {
  Matrix4x4 scaleMatrix = MakeScaleMatrix(scale);
  Matrix4x4 rotateMatrix = MakeRotateMatrix(rotate);
  Matrix4x4 translateMatrix = MakeTranslateMatrix(translate);

  Matrix4x4 worldMatrix =
      Multiply(Multiply(scaleMatrix, rotateMatrix), translateMatrix);
  return worldMatrix;
}

Matrix4x4 MakeLookAtMatrix(const Vector3& pos, const Vector3& target, const Vector3& up) {
  Vector3 f = SafeNormalize({ target.x - pos.x, target.y - pos.y, target.z - pos.z }); // forward
  Vector3 r = SafeNormalize(Cross(up, f)); // right
  Vector3 u = Cross(f, r);                 // up（再計算）

  Matrix4x4 m = MakeIdentity4x4();

  m.m[0][0] = r.x; m.m[0][1] = u.x; m.m[0][2] = f.x; m.m[0][3] = 0.0f;
  m.m[1][0] = r.y; m.m[1][1] = u.y; m.m[1][2] = f.y; m.m[1][3] = 0.0f;
  m.m[2][0] = r.z; m.m[2][1] = u.z; m.m[2][2] = f.z; m.m[2][3] = 0.0f;
  m.m[3][0] = -Dot(r, pos); m.m[3][1] = -Dot(u, pos); m.m[3][2] = -Dot(f, pos); m.m[3][3] = 1.0f;
  return m;
}

Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio,
                                   float nearClip, float farClip) {
  Matrix4x4 matrix;
  float f = 1.0f / tanf(fovY / 2.0f);

  matrix.m[0][0] = f / aspectRatio;
  matrix.m[0][1] = 0.0f;
  matrix.m[0][2] = 0.0f;
  matrix.m[0][3] = 0.0f;
  matrix.m[1][0] = 0.0f;
  matrix.m[1][1] = f;
  matrix.m[1][2] = 0.0f;
  matrix.m[1][3] = 0.0f;
  matrix.m[2][0] = 0.0f;
  matrix.m[2][1] = 0.0f;
  matrix.m[2][2] = farClip / (farClip - nearClip);
  matrix.m[2][3] = 1.0f;
  matrix.m[3][0] = 0.0f;
  matrix.m[3][1] = 0.0f;
  matrix.m[3][2] = -nearClip * farClip / (farClip - nearClip);
  matrix.m[3][3] = 0.0f;

  return matrix;
}

Matrix4x4 MakeOrthographicMatrix(float left, float top, float right,
                                 float bottom, float nearClip, float farClip) {
  Matrix4x4 matrix;

  matrix.m[0][0] = 2.0f / (right - left);
  matrix.m[0][1] = 0.0f;
  matrix.m[0][2] = 0.0f;
  matrix.m[0][3] = 0.0f;
  matrix.m[1][0] = 0.0f;
  matrix.m[1][1] = 2.0f / (top - bottom);
  matrix.m[1][2] = 0.0f;
  matrix.m[1][3] = 0.0f;
  matrix.m[2][0] = 0.0f;
  matrix.m[2][1] = 0.0f;
  matrix.m[2][2] = 1.0f / (farClip - nearClip);
  matrix.m[2][3] = 0.0f;
  matrix.m[3][0] = (left + right) / (left - right);
  matrix.m[3][1] = (top + bottom) / (bottom - top);
  matrix.m[3][2] = nearClip / (nearClip - farClip);
  matrix.m[3][3] = 1.0f;

  return matrix;
}

Matrix4x4 Inverse(Matrix4x4 matrix) {
  Matrix4x4 inverseMatrix;

  float det =
      matrix.m[0][0] * matrix.m[1][1] * matrix.m[2][2] * matrix.m[3][3] +
      matrix.m[0][0] * matrix.m[1][2] * matrix.m[2][3] * matrix.m[3][1] +
      matrix.m[0][0] * matrix.m[1][3] * matrix.m[2][1] * matrix.m[3][2] -
      matrix.m[0][0] * matrix.m[1][3] * matrix.m[2][2] * matrix.m[3][1] -
      matrix.m[0][0] * matrix.m[1][2] * matrix.m[2][1] * matrix.m[3][3] -
      matrix.m[0][0] * matrix.m[1][1] * matrix.m[2][3] * matrix.m[3][2] -
      matrix.m[0][1] * matrix.m[1][0] * matrix.m[2][2] * matrix.m[3][3] -
      matrix.m[0][2] * matrix.m[1][0] * matrix.m[2][3] * matrix.m[3][1] -
      matrix.m[0][3] * matrix.m[1][0] * matrix.m[2][1] * matrix.m[3][2] +
      matrix.m[0][3] * matrix.m[1][0] * matrix.m[2][2] * matrix.m[3][1] +
      matrix.m[0][2] * matrix.m[1][0] * matrix.m[2][1] * matrix.m[3][3] +
      matrix.m[0][1] * matrix.m[1][0] * matrix.m[2][3] * matrix.m[3][2] +
      matrix.m[0][1] * matrix.m[1][2] * matrix.m[2][0] * matrix.m[3][3] +
      matrix.m[0][2] * matrix.m[1][3] * matrix.m[2][0] * matrix.m[3][1] +
      matrix.m[0][3] * matrix.m[1][1] * matrix.m[2][0] * matrix.m[3][2] -
      matrix.m[0][3] * matrix.m[1][2] * matrix.m[2][0] * matrix.m[3][1] -
      matrix.m[0][2] * matrix.m[1][1] * matrix.m[2][0] * matrix.m[3][3] -
      matrix.m[0][1] * matrix.m[1][3] * matrix.m[2][0] * matrix.m[3][2] -
      matrix.m[0][1] * matrix.m[1][2] * matrix.m[2][3] * matrix.m[3][0] -
      matrix.m[0][2] * matrix.m[1][3] * matrix.m[2][1] * matrix.m[3][0] -
      matrix.m[0][3] * matrix.m[1][1] * matrix.m[2][2] * matrix.m[3][0] +
      matrix.m[0][3] * matrix.m[1][2] * matrix.m[2][1] * matrix.m[3][0] +
      matrix.m[0][2] * matrix.m[1][1] * matrix.m[2][3] * matrix.m[3][0] +
      matrix.m[0][1] * matrix.m[1][3] * matrix.m[2][2] * matrix.m[3][0];

  float invDet = 1 / det;

  // 逆行列を求めるための補助行列を計算
  inverseMatrix.m[0][0] = (matrix.m[1][1] * matrix.m[2][2] * matrix.m[3][3] -
                           matrix.m[1][1] * matrix.m[2][3] * matrix.m[3][2] -
                           matrix.m[1][2] * matrix.m[2][1] * matrix.m[3][3] +
                           matrix.m[1][2] * matrix.m[2][3] * matrix.m[3][1] +
                           matrix.m[1][3] * matrix.m[2][1] * matrix.m[3][2] -
                           matrix.m[1][3] * matrix.m[2][2] * matrix.m[3][1]) *
                          invDet;

  inverseMatrix.m[0][1] = (-(matrix.m[0][1] * matrix.m[2][2] * matrix.m[3][3]) +
                           matrix.m[0][1] * matrix.m[2][3] * matrix.m[3][2] +
                           matrix.m[0][2] * matrix.m[2][1] * matrix.m[3][3] -
                           matrix.m[0][2] * matrix.m[2][3] * matrix.m[3][1] -
                           matrix.m[0][3] * matrix.m[2][1] * matrix.m[3][2] +
                           matrix.m[0][3] * matrix.m[2][2] * matrix.m[3][1]) *
                          invDet;

  inverseMatrix.m[0][2] = (matrix.m[0][1] * matrix.m[1][2] * matrix.m[3][3] -
                           matrix.m[0][1] * matrix.m[1][3] * matrix.m[3][2] -
                           matrix.m[0][2] * matrix.m[1][1] * matrix.m[3][3] +
                           matrix.m[0][2] * matrix.m[1][3] * matrix.m[3][1] +
                           matrix.m[0][3] * matrix.m[1][1] * matrix.m[3][2] -
                           matrix.m[0][3] * matrix.m[1][2] * matrix.m[3][1]) *
                          invDet;

  inverseMatrix.m[0][3] = (-(matrix.m[0][1] * matrix.m[1][2] * matrix.m[2][3]) +
                           matrix.m[0][1] * matrix.m[1][3] * matrix.m[2][2] +
                           matrix.m[0][2] * matrix.m[1][1] * matrix.m[2][3] -
                           matrix.m[0][2] * matrix.m[1][3] * matrix.m[2][1] -
                           matrix.m[0][3] * matrix.m[1][1] * matrix.m[2][2] +
                           matrix.m[0][3] * matrix.m[1][2] * matrix.m[2][1]) *
                          invDet;
  inverseMatrix.m[1][0] = (-(matrix.m[1][0] * matrix.m[2][2] * matrix.m[3][3]) +
                           matrix.m[1][0] * matrix.m[2][3] * matrix.m[3][2] +
                           matrix.m[1][2] * matrix.m[2][0] * matrix.m[3][3] -
                           matrix.m[1][2] * matrix.m[2][3] * matrix.m[3][0] -
                           matrix.m[1][3] * matrix.m[2][0] * matrix.m[3][2] +
                           matrix.m[1][3] * matrix.m[2][2] * matrix.m[3][0]) *
                          invDet;

  inverseMatrix.m[1][1] = (matrix.m[0][0] * matrix.m[2][2] * matrix.m[3][3] -
                           matrix.m[0][0] * matrix.m[2][3] * matrix.m[3][2] -
                           matrix.m[0][2] * matrix.m[2][0] * matrix.m[3][3] +
                           matrix.m[0][2] * matrix.m[2][3] * matrix.m[3][0] +
                           matrix.m[0][3] * matrix.m[2][0] * matrix.m[3][2] -
                           matrix.m[0][3] * matrix.m[2][2] * matrix.m[3][0]) *
                          invDet;

  inverseMatrix.m[1][2] = (-(matrix.m[0][0] * matrix.m[1][2] * matrix.m[3][3]) +
                           matrix.m[0][0] * matrix.m[1][3] * matrix.m[3][2] +
                           matrix.m[0][2] * matrix.m[1][0] * matrix.m[3][3] -
                           matrix.m[0][2] * matrix.m[1][3] * matrix.m[3][0] -
                           matrix.m[0][3] * matrix.m[1][0] * matrix.m[3][2] +
                           matrix.m[0][3] * matrix.m[1][2] * matrix.m[3][0]) *
                          invDet;

  inverseMatrix.m[1][3] = (matrix.m[0][0] * matrix.m[1][2] * matrix.m[2][3] -
                           matrix.m[0][0] * matrix.m[1][3] * matrix.m[2][2] -
                           matrix.m[0][2] * matrix.m[1][0] * matrix.m[2][3] +
                           matrix.m[0][2] * matrix.m[1][3] * matrix.m[2][0] +
                           matrix.m[0][3] * matrix.m[1][0] * matrix.m[2][2] -
                           matrix.m[0][3] * matrix.m[1][2] * matrix.m[2][0]) *
                          invDet;

  inverseMatrix.m[2][0] = (matrix.m[1][0] * matrix.m[2][1] * matrix.m[3][3] -
                           matrix.m[1][0] * matrix.m[2][3] * matrix.m[3][1] -
                           matrix.m[1][1] * matrix.m[2][0] * matrix.m[3][3] +
                           matrix.m[1][1] * matrix.m[2][3] * matrix.m[3][0] +
                           matrix.m[1][3] * matrix.m[2][0] * matrix.m[3][1] -
                           matrix.m[1][3] * matrix.m[2][1] * matrix.m[3][0]) *
                          invDet;

  inverseMatrix.m[2][1] = (-(matrix.m[0][0] * matrix.m[2][1] * matrix.m[3][3]) +
                           matrix.m[0][0] * matrix.m[2][3] * matrix.m[3][1] +
                           matrix.m[0][1] * matrix.m[2][0] * matrix.m[3][3] -
                           matrix.m[0][1] * matrix.m[2][3] * matrix.m[3][0] -
                           matrix.m[0][3] * matrix.m[2][0] * matrix.m[3][1] +
                           matrix.m[0][3] * matrix.m[2][1] * matrix.m[3][0]) *
                          invDet;

  inverseMatrix.m[2][2] = (matrix.m[0][0] * matrix.m[1][1] * matrix.m[3][3] -
                           matrix.m[0][0] * matrix.m[1][3] * matrix.m[3][1] -
                           matrix.m[0][1] * matrix.m[1][0] * matrix.m[3][3] +
                           matrix.m[0][1] * matrix.m[1][3] * matrix.m[3][0] +
                           matrix.m[0][3] * matrix.m[1][0] * matrix.m[3][1] -
                           matrix.m[0][3] * matrix.m[1][1] * matrix.m[3][0]) *
                          invDet;

  inverseMatrix.m[2][3] = (-(matrix.m[0][0] * matrix.m[1][1] * matrix.m[2][3]) +
                           matrix.m[0][0] * matrix.m[1][3] * matrix.m[2][1] +
                           matrix.m[0][1] * matrix.m[1][0] * matrix.m[2][3] -
                           matrix.m[0][1] * matrix.m[1][3] * matrix.m[2][0] -
                           matrix.m[0][3] * matrix.m[1][0] * matrix.m[2][1] +
                           matrix.m[0][3] * matrix.m[1][1] * matrix.m[2][0]) *
                          invDet;

  inverseMatrix.m[3][0] = (-(matrix.m[1][0] * matrix.m[2][1] * matrix.m[3][2]) +
                           matrix.m[1][0] * matrix.m[2][2] * matrix.m[3][1] +
                           matrix.m[1][1] * matrix.m[2][0] * matrix.m[3][2] -
                           matrix.m[1][1] * matrix.m[2][2] * matrix.m[3][0] -
                           matrix.m[1][2] * matrix.m[2][0] * matrix.m[3][1] +
                           matrix.m[1][2] * matrix.m[2][1] * matrix.m[3][0]) *
                          invDet;

  inverseMatrix.m[3][1] = (matrix.m[0][0] * matrix.m[2][1] * matrix.m[3][2] -
                           matrix.m[0][0] * matrix.m[2][2] * matrix.m[3][1] -
                           matrix.m[0][1] * matrix.m[2][0] * matrix.m[3][2] +
                           matrix.m[0][1] * matrix.m[2][2] * matrix.m[3][0] +
                           matrix.m[0][2] * matrix.m[2][0] * matrix.m[3][1] -
                           matrix.m[0][2] * matrix.m[2][1] * matrix.m[3][0]) *
                          invDet;

  inverseMatrix.m[3][2] = (-(matrix.m[0][0] * matrix.m[1][1] * matrix.m[3][2]) +
                           matrix.m[0][0] * matrix.m[1][2] * matrix.m[3][1] +
                           matrix.m[0][1] * matrix.m[1][0] * matrix.m[3][2] -
                           matrix.m[0][1] * matrix.m[1][2] * matrix.m[3][0] -
                           matrix.m[0][2] * matrix.m[1][0] * matrix.m[3][1] +
                           matrix.m[0][2] * matrix.m[1][1] * matrix.m[3][0]) *
                          invDet;

  inverseMatrix.m[3][3] = (matrix.m[0][0] * matrix.m[1][1] * matrix.m[2][2] -
                           matrix.m[0][0] * matrix.m[1][2] * matrix.m[2][1] -
                           matrix.m[0][1] * matrix.m[1][0] * matrix.m[2][2] +
                           matrix.m[0][1] * matrix.m[1][2] * matrix.m[2][0] +
                           matrix.m[0][2] * matrix.m[1][0] * matrix.m[2][1] -
                           matrix.m[0][2] * matrix.m[1][1] * matrix.m[2][0]) *
                          invDet;

  return inverseMatrix;
}

Vector3 TransformNormal(const Vector3 &v, const Matrix4x4 &m) {
  Vector3 result;
  result.x = v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0];
  result.y = v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1];
  result.z = v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2];
  return result;
}

Vector2 WorldToScreen(const Vector3 &worldPos, const Matrix4x4 &viewProjMatrix,
                      float screenWidth, float screenHeight) {
  //  ViewProjection行列でワールド座標をクリップ空間座標（NDC）へ変換
  Vector3 ndc;
  ndc.x = worldPos.x * viewProjMatrix.m[0][0] +
          worldPos.y * viewProjMatrix.m[1][0] +
          worldPos.z * viewProjMatrix.m[2][0] + 1.0f * viewProjMatrix.m[3][0];
  ndc.y = worldPos.x * viewProjMatrix.m[0][1] +
          worldPos.y * viewProjMatrix.m[1][1] +
          worldPos.z * viewProjMatrix.m[2][1] + 1.0f * viewProjMatrix.m[3][1];
  ndc.z = worldPos.x * viewProjMatrix.m[0][2] +
          worldPos.y * viewProjMatrix.m[1][2] +
          worldPos.z * viewProjMatrix.m[2][2] + 1.0f * viewProjMatrix.m[3][2];
  float w = worldPos.x * viewProjMatrix.m[0][3] +
            worldPos.y * viewProjMatrix.m[1][3] +
            worldPos.z * viewProjMatrix.m[2][3] + 1.0f * viewProjMatrix.m[3][3];

  // 透視投影除算
  if (w <= 0.0f) {
    // カメラの後ろにいる場合は画面外の座標を返す
    return Vector2{-10000.0f, -10000.0f};
  }

  ndc.x /= w;
  ndc.y /= w;
  ndc.z /= w;

  // NDC (-1.0 ~ 1.0) をスクリーン座標に変換
  // Xは右がプラス、Yは下がプラスになるよう調整
  Vector2 screenPos;
  screenPos.x = (ndc.x + 1.0f) * 0.5f * screenWidth;
  screenPos.y = (1.0f - ndc.y) * 0.5f * screenHeight;

  return screenPos;
}

Matrix4x4 Transpose(Matrix4x4 matrix) {
  Matrix4x4 result{};

  result.m[0][0] = matrix.m[0][0];
  result.m[0][1] = matrix.m[1][0];
  result.m[0][2] = matrix.m[2][0];
  result.m[0][3] = matrix.m[3][0];

  result.m[1][0] = matrix.m[0][1];
  result.m[1][1] = matrix.m[1][1];
  result.m[1][2] = matrix.m[2][1];
  result.m[1][3] = matrix.m[3][1];

  result.m[2][0] = matrix.m[0][2];
  result.m[2][1] = matrix.m[1][2];
  result.m[2][2] = matrix.m[2][2];
  result.m[2][3] = matrix.m[3][2];

  result.m[3][0] = matrix.m[0][3];
  result.m[3][1] = matrix.m[1][3];
  result.m[3][2] = matrix.m[2][3];
  result.m[3][3] = matrix.m[3][3];

  return result;
}

Vector3 RGBToHSV(const Vector3 &rgb) {
  float r = rgb.x;
  float g = rgb.y;
  float b = rgb.z;

  float maxColor = std::max({r, g, b});
  float minColor = std::min({r, g, b});
  float delta = maxColor - minColor;

  float h = 0.0f;
  float s = 0.0f;
  float v = maxColor;

  if (maxColor > 0.0f) {
    s = delta / maxColor;
  }

  if (delta > 0.0f) {
    if (maxColor == r) {
      h = 60.0f * (g - b) / delta;
    } else if (maxColor == g) {
      h = 60.0f * (2.0f + (b - r) / delta);
    } else if (maxColor == b) {
      h = 60.0f * (4.0f + (r - g) / delta);
    }

    if (h < 0.0f) {
      h += 360.0f;
    }
  }

  return {h, s, v};
}

Vector3 HSVToRGB(const Vector3 &hsv) {
  float h = hsv.x;
  float s = hsv.y;
  float v = hsv.z;

  // Hが360度以上の場合は0-359度に丸める
  h = std::fmod(h, 360.0f);
  if (h < 0.0f)
    h += 360.0f;

  float c = v * s;
  float hp = h / 60.0f;
  float x = c * (1.0f - std::abs(std::fmod(hp, 2.0f) - 1.0f));
  float m = v - c;

  float r = 0.0f, g = 0.0f, b = 0.0f;

  if (0.0f <= hp && hp < 1.0f) {
    r = c;
    g = x;
    b = 0.0f;
  } else if (1.0f <= hp && hp < 2.0f) {
    r = x;
    g = c;
    b = 0.0f;
  } else if (2.0f <= hp && hp < 3.0f) {
    r = 0.0f;
    g = c;
    b = x;
  } else if (3.0f <= hp && hp < 4.0f) {
    r = 0.0f;
    g = x;
    b = c;
  } else if (4.0f <= hp && hp < 5.0f) {
    r = x;
    g = 0.0f;
    b = c;
  } else if (5.0f <= hp && hp < 6.0f) {
    r = c;
    g = 0.0f;
    b = x;
  }

  return {r + m, g + m, b + m};
}

// レイと球の交差判定
bool IsCollision(const Ray& ray, const Sphere& sphere, float* outDistance) {
  Vector3 m = {ray.origin.x - sphere.center.x, ray.origin.y - sphere.center.y, ray.origin.z - sphere.center.z};
  float b = Dot(m, ray.diff);
  float c = Dot(m, m) - sphere.radius * sphere.radius;

  // レイの始点が球の外側にあり、かつレイが球と反対方向を向いている場合は交差しない
  if (c > 0.0f && b > 0.0f) {
    return false;
  }

  // 判別式
  float discr = b * b - c;
  
  // 負の場合は交差しない
  if (discr < 0.0f) {
    return false;
  }

  // 交差距離 t を求める（最も近い点）
  float t = -b - std::sqrt(discr);

  // t が負の場合は球の内部から発射されている。その場合は t = 0 (もしくは反対側の交点) とする
  if (t < 0.0f) {
    t = 0.0f;
  }

  if (outDistance) {
    *outDistance = t;
  }

  return true;
}
