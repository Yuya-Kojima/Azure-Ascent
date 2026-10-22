#include "Core/SrvHandle.h"
#include "Core/SrvManager.h"
#include <cassert>

SrvHandle::SrvHandle(SrvManager *srvManager, uint32_t index)
    : srvManager_(srvManager), index_(index) {}

SrvHandle::~SrvHandle() { Reset(); }

SrvHandle::SrvHandle(SrvHandle &&other) noexcept
    : srvManager_(other.srvManager_), index_(other.index_) {
  other.srvManager_ = nullptr;
  other.index_ = 0;
}

SrvHandle &SrvHandle::operator=(SrvHandle &&other) noexcept {
  if (this != &other) {
    // いま持っている番号を返してから、other の番号を受け取る
    Reset();
    srvManager_ = other.srvManager_;
    index_ = other.index_;
    other.srvManager_ = nullptr;
    other.index_ = 0;
  }
  return *this;
}

uint32_t SrvHandle::Index() const {
  assert(IsValid());
  return index_;
}

void SrvHandle::Reset() {
  if (srvManager_) {
    srvManager_->Free(index_);
    srvManager_ = nullptr;
    index_ = 0;
  }
}
