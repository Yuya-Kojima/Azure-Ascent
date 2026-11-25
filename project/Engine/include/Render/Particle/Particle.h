#pragma once
#include "Math/Transform.h"
#include "Math/Vector3.h"
#include <random>

struct Particle {
  Transform transform;
  Vector3 velocity;
  Vector4 color;
  float lifeTime;
  float currentTime;
};

// Compute Shader用（GPUパーティクル用）の構造体。Particle.CS.hlsl と同じ並び
struct ParticleCS {
  Vector3 translate;
  float weight; // 重さ
  Vector3 scale;
  float padding2;
  Vector3 rotate;
  float lifeTime;
  Vector3 velocity;
  float currentTime;
  Vector4 color; // 今の色
  Vector3 scaleVelocity;
  float scaleEndRate; // 寿命の終わりの大きさの倍率
  Vector3 startScale; // 生まれたときの大きさ
  float fadeStart; // 薄くなり始める寿命の割合（0〜1）
  Vector3 colorStart; // 生まれたときの色
  float padding4;
  Vector3 colorEnd; // 寿命の終わりの色
  float padding5;
  Vector3 rotateVelocity; // 回る速さ（ラジアン/秒）
  float padding6;
};

struct Emitter {
  Transform transform; // エミッターのtransform
  uint32_t count;      // 発生する数
  float frequency;     // 発生頻度
  float frequencyTime;
};

// GPU Particle用のエミッター構造体
struct EmitterSphere {
  Vector3 translate;   // 位置
  float radius;        // 射出半径
  uint32_t count;      // 射出数
  float frequency;     // 射出間隔
  float frequencyTime; // 射出間隔調整用時間
  uint32_t emit;       // 射出許可
};
