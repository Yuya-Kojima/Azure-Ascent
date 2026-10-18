#pragma once
#include "Math/Vector2.h"
#include "Math/Vector4.h"
#include <memory>
#include <string>
#include <vector>

struct ImVec2;

class Text;
class Sprite;
class SpriteRenderer;
class Input;

enum class UINodeType { Node, Text, Image };

class UINode {
public:
  UINode() = default;
  virtual ~UINode() = default;

  std::string name = "Node";
  UINodeType type = UINodeType::Node;

  Vector2 position = {0, 0};
  Vector2 scale = {1, 1};
  Vector2 anchorPoint = {0.0f, 0.0f};

  // Text specific
  std::string textString = "Text";
  std::string fontName = "Roboto";
  Vector4 color = {1, 1, 1, 1};
  std::unique_ptr<Text> textObject;

  // Image specific
  std::string textureName = "";
  std::unique_ptr<Sprite> spriteObject;

  // Selectable properties
  bool isSelectable = false;
  bool isFocused = false;
  Vector4 focusedColor = {1.0f, 1.0f, 0.0f, 1.0f};

  // Hierarchy
  std::vector<std::unique_ptr<UINode>> children;

  virtual void Initialize(SpriteRenderer *renderer);
  virtual void Update();
  virtual void Draw();
};

class UIManager {
public:
  static UIManager *GetInstance();

  void Initialize(SpriteRenderer *renderer);
  void Finalize();

  void Update(Input *input = nullptr);
  void Draw();
  void DrawEditor();
  void DrawEditorGizmo(const ImVec2 &screenPos, const ImVec2 &imageSize);

  void Load(const std::string &filePath);
  void Save(const std::string &filePath);

  // 全ノードの不透明度に掛ける値（0〜1）。Load で 1 に戻る
  void SetGlobalAlpha(float alpha) { globalAlpha_ = alpha; }
  float GetGlobalAlpha() const { return globalAlpha_; }

  UINode *GetRootNode() { return rootNode_.get(); }
  UINode *GetNodeByName(const std::string &name);
  std::string GetFocusedNodeName();

private:
  UIManager() = default;
  ~UIManager() = default;
  UIManager(UIManager &) = delete;
  UIManager &operator=(UIManager &) = delete;

  void DrawNodeHierarchy(UINode *node);
  void DrawNodeProperties(UINode *node);

  bool isDrawnThisFrame_ = false;
  float globalAlpha_ = 1.0f;

  SpriteRenderer *renderer_ = nullptr;
  std::unique_ptr<UINode> rootNode_;
  std::string currentFilePath_ = "";

  UINode *selectedNode_ = nullptr;
};
