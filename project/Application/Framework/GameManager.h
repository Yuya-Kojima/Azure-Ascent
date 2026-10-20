#pragma once
#include <string>

/// <summary>
/// ゲーム全体を通して保持すべきデータを管理するシングルトン
/// </summary>
class GameManager {
private:
    GameManager() = default;
    ~GameManager() = default;
    GameManager(const GameManager&) = delete;
    GameManager& operator=(const GameManager&) = delete;

    // 現在プレイ中のレベル（ステージ）のファイル名
    std::string currentLevelFileName_ = "level_editor.json"; // デフォルト
    
#ifdef USE_IMGUI
    bool isGlobalPlayMode_ = false;
#else
    bool isGlobalPlayMode_ = true;
#endif

public:
    static GameManager* GetInstance();

    // 現在のレベルファイル名を設定・取得
    void SetCurrentLevel(const std::string& fileName) { currentLevelFileName_ = fileName; }
    const std::string& GetCurrentLevel() const { return currentLevelFileName_; }

    void SetGlobalPlayMode(bool isPlay) { isGlobalPlayMode_ = isPlay; }
    bool IsGlobalPlayMode() const { return isGlobalPlayMode_; }

    // プレイ開始時の状態保存用
    std::string playStartSceneName_ = "TITLE";
    std::string playStartLevelName_ = "level_editor.json";

    void SetPlayStartSceneName(const std::string& name) { playStartSceneName_ = name; }
    const std::string& GetPlayStartSceneName() const { return playStartSceneName_; }

    void SetPlayStartLevelName(const std::string& name) { playStartLevelName_ = name; }
    const std::string& GetPlayStartLevelName() const { return playStartLevelName_; }

    // --- ステージ開始演出 ---
    // 一度最後まで見たか（見たあとは、ボタンで飛ばせる）
    bool hasSeenStageIntro_ = false;
    void SetSeenStageIntro() { hasSeenStageIntro_ = true; }
    bool HasSeenStageIntro() const { return hasSeenStageIntro_; }

    // --- スコア関連 ---
    int currentScore_ = 0;
    void AddScore(int score) { currentScore_ += score; }
    void ResetScore() { currentScore_ = 0; }
    int GetScore() const { return currentScore_; }
};
