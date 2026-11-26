#pragma once
#include "Framework/BaseActor.h"
#include <memory>
#include <vector>

/// <summary>
/// 使い終わった Actor をしまう入れ物の基底。ActorManager は中身の型を知らずに、消えた Actor をここへ返す
/// </summary>
class IActorPool {
public:
	virtual ~IActorPool() = default;

	/// <summary>
	/// 使い終わった Actor をしまう
	/// </summary>
	virtual void Return(std::unique_ptr<BaseActor> actor) = 0;
};

/// <summary>
/// 型 T の Actor をしまっておき、次に使うときに取り出す入れ物
/// </summary>
template <class T>
class ActorPool : public IActorPool {
public:
	/// <summary>
	/// しまってある Actor を 1 つ取り出す。しまってある物がなければ新しく作る
	/// </summary>
	std::unique_ptr<T> Acquire() {
		std::unique_ptr<T> actor;
		if (free_.empty()) {
			actor = std::make_unique<T>();
		} else {
			actor = std::move(free_.back());
			free_.pop_back();
		}
		// 消えたときに、この入れ物へ返してもらう
		actor->SetPool(this);
		return actor;
	}

	void Return(std::unique_ptr<BaseActor> actor) override {
		// この入れ物から取り出した Actor だけが返ってくるので、型は T に決まっている
		free_.push_back(std::unique_ptr<T>(static_cast<T*>(actor.release())));
	}

private:
	std::vector<std::unique_ptr<T>> free_; // しまってある Actor
};
