#pragma once
#include <cstdint>

class SrvManager;

/// <summary>
/// SRV ヒープの番号 1 つの持ち主。壊れると番号を SrvManager に返す。
/// コピーはできず、ムーブすると持ち主が移る（移した側は空になる）。
/// 番号を返す先の SrvManager は、すべてのハンドルより後に壊す
/// </summary>
class SrvHandle {
public:
  SrvHandle() = default;
  SrvHandle(SrvManager *srvManager, uint32_t index);
  ~SrvHandle();

  SrvHandle(const SrvHandle &) = delete;
  SrvHandle &operator=(const SrvHandle &) = delete;

  SrvHandle(SrvHandle &&other) noexcept;
  SrvHandle &operator=(SrvHandle &&other) noexcept;

  /// <summary>
  /// 持っている番号（空のハンドルでは呼ばない）
  /// </summary>
  uint32_t Index() const;

  bool IsValid() const { return srvManager_ != nullptr; }

  /// <summary>
  /// 番号を SrvManager に返して空になる
  /// </summary>
  void Reset();

private:
  SrvManager *srvManager_ = nullptr;
  uint32_t index_ = 0;
};
