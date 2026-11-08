#include "Actor/EnemyLights.h"
#include "Math/MathUtil.h"
#include "Render/Object3d/Object3d.h"

namespace {
// 描くときの色（光の色に強さを掛ける）
Vector4 ToDrawColor(const EnemyLights::Part &part) {
  return {part.color.x * part.intensity, part.color.y * part.intensity,
          part.color.z * part.intensity, 1.0f};
}
} // namespace

EnemyLights::EnemyLights() = default;

EnemyLights::~EnemyLights() = default;

void EnemyLights::Initialize(Object3dRenderer *renderer, const Object3d *body,
                             const std::vector<Part> &parts) {
  body_ = body;
  instances_.clear();
  if (!renderer || !body_) {
    return;
  }

  instances_.reserve(parts.size());
  for (const Part &part : parts) {
    Instance instance;
    instance.part = part;
    instance.object = std::make_unique<Object3d>();
    instance.object->Initialize(renderer);
    instance.object->SetModel(part.modelPath);
    instance.object->SetEnableLighting(false);
    instance.object->SetFlatUnlit(true);
    instance.object->SetColor(ToDrawColor(part));
    // 部品の位置と回転は 0、大きさは 1 のままにし、体の行列だけで置く
    // 部品の行列 = 部品のローカル行列 × 体のワールド行列
    instance.object->SetParent(body_);
    instances_.push_back(std::move(instance));
  }
}

void EnemyLights::SetColor(const Vector3 &color, float intensity) {
  for (Instance &instance : instances_) {
    instance.part.color = color;
    instance.part.intensity = intensity;
    instance.object->SetColor(ToDrawColor(instance.part));
  }
}

void EnemyLights::Update() {
  for (Instance &instance : instances_) {
    instance.object->Update();
  }
}

void EnemyLights::Draw() {
  for (Instance &instance : instances_) {
    instance.object->Draw();
  }
}

std::vector<Vector3> EnemyLights::GetBurstPositions() const {
  std::vector<Vector3> positions;
  if (!body_) {
    return positions;
  }
  const Matrix4x4 &bodyWorld = body_->GetWorldMatrix();
  for (const Instance &instance : instances_) {
    for (const Vector3 &point : instance.part.burstPoints) {
      positions.push_back(TransformPoint(point, bodyWorld));
    }
  }
  return positions;
}

Vector4 EnemyLights::GetBurstColor() const {
  if (instances_.empty()) {
    return {1.0f, 1.0f, 1.0f, 1.0f};
  }
  const Vector3 &color = instances_.front().part.color;
  return {color.x, color.y, color.z, 1.0f};
}
