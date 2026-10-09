#include "FractureCache.h"
#include "Effect/FracturePieceSet.h"
#include "Render/Model/Model.h"
#include <cstdint>

namespace {
// 種の数（≒ 1体あたりの破片の数）
// 計算量は O(種の数^2 x 三角形数)。ロード時に1回だけなので、見た目優先で決める
const uint32_t kSeedCount = 8;

// 種を置く乱数のシード（同じモデルは毎回同じ割れ方になる）
const uint32_t kRandomSeed = 1;
} // namespace

FractureCache *FractureCache::GetInstance() {
  static FractureCache instance;
  return &instance;
}

FractureCache::~FractureCache() = default;

void FractureCache::Prebuild(const Model *model) {
  if (!model || sets_.contains(model)) {
    return;
  }

  auto pieceSet = std::make_unique<FracturePieceSet>();
  pieceSet->Create(model->GetModelData(), kSeedCount, kRandomSeed);
  sets_.emplace(model, std::move(pieceSet));
}

const FracturePieceSet *FractureCache::Find(const Model *model) const {
  auto it = sets_.find(model);
  if (it == sets_.end()) {
    return nullptr;
  }
  return it->second.get();
}

void FractureCache::Clear() { sets_.clear(); }
