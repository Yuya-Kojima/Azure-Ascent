#pragma once
#include "Score/VolleyId.h"
#include <functional>
#include <utility>

/// <summary>
/// ロックオンの一斉射撃の、弾 1 発ぶんの券
/// 持ち主を移すことはできるが、コピーはできない。Finish を呼ぶか券が消えるかの早い方で、射撃番号を 1 回だけ返す
/// </summary>
class VolleyTicket {
public:
  // どの射撃にも属さない空の券（返しても何も起きない）
  VolleyTicket() = default;

  /// <param name="volleyId">この券の射撃番号</param>
  /// <param name="onFinish">券を返したときに射撃番号を渡して呼ぶ関数</param>
  VolleyTicket(int volleyId, std::function<void(int)> onFinish)
      : volleyId_(volleyId), onFinish_(std::move(onFinish)) {}

  ~VolleyTicket() { Finish(); }

  VolleyTicket(const VolleyTicket &) = delete;
  VolleyTicket &operator=(const VolleyTicket &) = delete;

  // 移した元の券は空になる
  VolleyTicket(VolleyTicket &&other) noexcept
      : volleyId_(std::exchange(other.volleyId_, kNoVolleyId)),
        onFinish_(std::exchange(other.onFinish_, nullptr)) {}

  // 持っていた券を返してから、新しい券を受け取る
  VolleyTicket &operator=(VolleyTicket &&other) noexcept {
    if (this != &other) {
      Finish();
      volleyId_ = std::exchange(other.volleyId_, kNoVolleyId);
      onFinish_ = std::exchange(other.onFinish_, nullptr);
    }
    return *this;
  }

  /// <summary>
  /// 券を返す（2 回目からは何もしない）
  /// </summary>
  void Finish() {
    if (!onFinish_) {
      return;
    }
    // 関数を券から外してから呼ぶので、呼んだ先から Finish が呼ばれても 2 回は返らない
    std::function<void(int)> onFinish = std::exchange(onFinish_, nullptr);
    onFinish(volleyId_);
  }

  /// <summary>
  /// この券の射撃番号（空の券なら kNoVolleyId。返したあとも変わらない）
  /// </summary>
  int GetVolleyId() const { return volleyId_; }

private:
  int volleyId_ = kNoVolleyId;
  std::function<void(int)> onFinish_;
};
