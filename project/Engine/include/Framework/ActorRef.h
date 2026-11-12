#pragma once
#include <memory>

/// <summary>
/// Actor への参照。相手の Actor が破棄されると Get() が nullptr を返す
/// </summary>
template <class T>
class ActorRef {
public:
	ActorRef() = default;
	ActorRef(T* actor) : actor_(actor) {
		if (actor_) {
			token_ = actor_->GetLifeToken();
		}
	}

	// 相手がまだ破棄されていなければその Actor、破棄されていれば nullptr
	T* Get() const { return token_.expired() ? nullptr : actor_; }

	// 何も指していない状態に戻す
	void Reset() {
		actor_ = nullptr;
		token_.reset();
	}

private:
	T* actor_ = nullptr;
	// 相手の Actor が持つ目印。相手が破棄されると期限切れになる
	std::weak_ptr<const void> token_;
};
