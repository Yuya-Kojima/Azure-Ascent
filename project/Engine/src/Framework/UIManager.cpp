#include "Framework/UIManager.h"
#include "Input/Input.h"
#include "Render/Renderer/SpriteRenderer.h"
#include "Render/Sprite/Sprite.h"
#include "Render/Text/FontManager.h"
#include "Render/Text/Text.h"
#include <fstream>
#include <nlohmann/json.hpp>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

using json = nlohmann::json;

void UINode::Initialize(SpriteRenderer *renderer) {
  if (type == UINodeType::Text) {
    if (!textObject) {
      textObject = std::make_unique<Text>();
      textObject->Initialize(renderer, fontName);
    }
  } else if (type == UINodeType::Image) {
    if (!spriteObject) {
      spriteObject = std::make_unique<Sprite>();
      if (!textureName.empty()) {
        spriteObject->Initialize(renderer, textureName);
      }
    }
  }

  for (auto &child : children) {
    child->Initialize(renderer);
  }
}

void UINode::Update() {
  Vector4 drawColor = color;
  if (isSelectable && isFocused) {
    drawColor = focusedColor;
  }
  drawColor.w *= UIManager::GetInstance()->GetGlobalAlpha();

  if (type == UINodeType::Text && textObject) {
    textObject->SetText(textString);
    textObject->SetPosition(position);
    textObject->SetScale(scale);
    textObject->SetColor(drawColor);
    textObject->SetAnchorPoint(anchorPoint);
    textObject->Update();
  } else if (type == UINodeType::Image && spriteObject) {
    spriteObject->SetPosition(position);
    spriteObject->SetScale(scale);
    spriteObject->SetColor(drawColor);
    spriteObject->SetAnchorPoint(anchorPoint);
    spriteObject->Update(Transform{{1, 1, 1}, {0, 0, 0}, {0, 0, 0}});
  }

  for (auto &child : children) {
    child->Update();
  }
}

void UINode::Draw() {
  if (type == UINodeType::Text && textObject) {
    textObject->Draw();
  } else if (type == UINodeType::Image && spriteObject) {
    spriteObject->Draw();
  }

  for (auto &child : children) {
    child->Draw();
  }
}

UIManager *UIManager::GetInstance() {
  static UIManager instance;
  return &instance;
}

void UIManager::Initialize(SpriteRenderer *renderer) {
  renderer_ = renderer;
  rootNode_ = std::make_unique<UINode>();
  rootNode_->name = "Canvas";
  rootNode_->type = UINodeType::Node;
}

void UIManager::Finalize() { rootNode_.reset(); }

void UIManager::Update(Input *input) {
  isDrawnThisFrame_ = false;

  if (input && rootNode_) {
    std::vector<UINode *> selectables;
    std::function<void(UINode *)> gather = [&](UINode *node) {
      if (node->isSelectable)
        selectables.push_back(node);
      for (auto &child : node->children)
        gather(child.get());
    };
    gather(rootNode_.get());

    if (!selectables.empty()) {
      int focusedIdx = -1;
      for (int i = 0; i < selectables.size(); ++i) {
        if (selectables[i]->isFocused) {
          focusedIdx = i;
          break;
        }
      }
      if (focusedIdx == -1)
        focusedIdx = 0;

      if (input->IsTriggerKey(DIK_UP) ||
          input->IsPadTrigger(PadButton::DPadUp)) {
        focusedIdx--;
        if (focusedIdx < 0)
          focusedIdx = (int)selectables.size() - 1;
      }
      if (input->IsTriggerKey(DIK_DOWN) ||
          input->IsPadTrigger(PadButton::DPadDown)) {
        focusedIdx++;
        if (focusedIdx >= (int)selectables.size())
          focusedIdx = 0;
      }

      for (int i = 0; i < selectables.size(); ++i) {
        selectables[i]->isFocused = (i == focusedIdx);
      }
    }
  }

  if (rootNode_) {
    rootNode_->Update();
  }
}

UINode *UIManager::GetNodeByName(const std::string &name) {
  if (!rootNode_)
    return nullptr;
  std::function<UINode *(UINode *)> search = [&](UINode *node) -> UINode * {
    if (node->name == name)
      return node;
    for (auto &child : node->children) {
      if (auto found = search(child.get()))
        return found;
    }
    return nullptr;
  };
  return search(rootNode_.get());
}

std::string UIManager::GetFocusedNodeName() {
  if (!rootNode_)
    return "";
  std::string focusedName = "";
  std::function<void(UINode *)> search = [&](UINode *node) {
    if (node->isSelectable && node->isFocused) {
      focusedName = node->name;
    }
    for (auto &child : node->children)
      search(child.get());
  };
  search(rootNode_.get());
  return focusedName;
}

void UIManager::Draw() {
  if (isDrawnThisFrame_)
    return;
  if (rootNode_) {
    rootNode_->Draw();
  }
  isDrawnThisFrame_ = true;
}

void UIManager::DrawEditor() {
#ifdef USE_IMGUI
  ImGui::Begin("UI Editor");

  if (currentFilePath_.empty()) {
    ImGui::Text("Editing: Unsaved UI");
  } else {
    ImGui::Text("Editing: %s", currentFilePath_.c_str());
  }
  ImGui::Separator();

  if (!currentFilePath_.empty()) {
    if (ImGui::Button("Save UI")) {
      Save(currentFilePath_);
    }
  } else {
    ImGui::TextDisabled("Load a JSON file from Project to edit.");
  }

  ImGui::Separator();
  ImGui::Columns(2, "UIEditorColumns");

  // Left Column: Hierarchy
  ImGui::Text("Hierarchy");
  ImGui::BeginChild("HierarchyView", ImVec2(0, 0), true);
  if (rootNode_) {
    DrawNodeHierarchy(rootNode_.get());
  }
  ImGui::EndChild();

  ImGui::NextColumn();

  // Right Column: Properties
  ImGui::Text("Properties");
  ImGui::BeginChild("PropertiesView", ImVec2(0, 0), true);
  if (selectedNode_) {
    DrawNodeProperties(selectedNode_);
  } else {
    ImGui::Text("Select a node to edit");
  }
  ImGui::EndChild();

  ImGui::Columns(1);
  ImGui::End();
#endif
}

#ifdef USE_IMGUI
void UIManager::DrawNodeHierarchy(UINode *node) {
  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                             ImGuiTreeNodeFlags_OpenOnDoubleClick |
                             ImGuiTreeNodeFlags_DefaultOpen;
  if (node == selectedNode_) {
    flags |= ImGuiTreeNodeFlags_Selected;
  }
  if (node->children.empty()) {
    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
  }

  bool nodeOpen = ImGui::TreeNodeEx((void *)node, flags, node->name.c_str());
  if (ImGui::IsItemClicked()) {
    selectedNode_ = node;
  }

  if (nodeOpen && !node->children.empty()) {
    for (auto &child : node->children) {
      DrawNodeHierarchy(child.get());
    }
    ImGui::TreePop();
  }
}

bool RemoveNodeHelper(UINode *current, UINode *target) {
  for (auto it = current->children.begin(); it != current->children.end();
       ++it) {
    if (it->get() == target) {
      current->children.erase(it);
      return true;
    }
    if (RemoveNodeHelper(it->get(), target)) {
      return true;
    }
  }
  return false;
}

void UIManager::DrawNodeProperties(UINode *node) {
  char nameBuf[256];
  strcpy_s(nameBuf, node->name.c_str());
  if (ImGui::InputText("Name", nameBuf, 256)) {
    node->name = nameBuf;
  }

  int typeIdx = (int)node->type;
  const char *types[] = {"Node", "Text", "Image"};
  if (ImGui::Combo("Type", &typeIdx, types, 3)) {
    node->type = (UINodeType)typeIdx;
    node->Initialize(renderer_);
  }

  ImGui::DragFloat2("Position", &node->position.x, 1.0f);
  ImGui::DragFloat2("Scale", &node->scale.x, 0.01f);
  ImGui::DragFloat2("Anchor", &node->anchorPoint.x, 0.01f);

  if (node->type == UINodeType::Text) {
    char textBuf[1024];
    strcpy_s(textBuf, node->textString.c_str());
    if (ImGui::InputText("Text", textBuf, 1024)) {
      node->textString = textBuf;
    }

    std::vector<std::string> loadedFonts =
        FontManager::GetInstance()->GetLoadedFontNames();
    if (ImGui::BeginCombo("Font", node->fontName.c_str())) {
      for (const auto &f : loadedFonts) {
        bool is_selected = (node->fontName == f);
        if (ImGui::Selectable(f.c_str(), is_selected)) {
          node->fontName = f;
          if (node->textObject)
            node->textObject->Initialize(renderer_, node->fontName);
        }
        if (is_selected)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    ImGui::ColorEdit4("Color", &node->color.x);
  } else if (node->type == UINodeType::Image) {
    char texBuf[256];
    strcpy_s(texBuf, node->textureName.c_str());
    if (ImGui::InputText("Texture", texBuf, 256)) {
      node->textureName = texBuf;
      if (node->spriteObject)
        node->spriteObject->Initialize(renderer_, node->textureName);
    }
    ImGui::ColorEdit4("Color", &node->color.x);
  }

  if (ImGui::Button("Add Child Node")) {
    auto child = std::make_unique<UINode>();
    child->name = "New Node";
    child->Initialize(renderer_);
    node->children.push_back(std::move(child));
  }

  ImGui::Separator();
  ImGui::Checkbox("Is Selectable", &node->isSelectable);
  if (node->isSelectable) {
    ImGui::ColorEdit4("Focused Color", &node->focusedColor.x);
  }

  ImGui::Separator();

  if (node != rootNode_.get()) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    if (ImGui::Button("Delete Node")) {
      if (RemoveNodeHelper(rootNode_.get(), node)) {
        selectedNode_ = nullptr;
      }
    }
    ImGui::PopStyleColor(3);
  }
}
#endif

// Serialize a node to json
json SerializeNode(const UINode *node) {
  json j;
  j["name"] = node->name;
  j["type"] = (int)node->type;
  j["position"] = {node->position.x, node->position.y};
  j["scale"] = {node->scale.x, node->scale.y};
  j["anchorPoint"] = {node->anchorPoint.x, node->anchorPoint.y};
  j["textString"] = node->textString;
  j["fontName"] = node->fontName;
  j["color"] = {node->color.x, node->color.y, node->color.z, node->color.w};
  j["textureName"] = node->textureName;
  j["isSelectable"] = node->isSelectable;
  j["focusedColor"] = {node->focusedColor.x, node->focusedColor.y,
                       node->focusedColor.z, node->focusedColor.w};

  json children = json::array();
  for (const auto &child : node->children) {
    children.push_back(SerializeNode(child.get()));
  }
  j["children"] = children;

  return j;
}

// Deserialize a node from json
std::unique_ptr<UINode> DeserializeNode(const json &j,
                                        SpriteRenderer *renderer) {
  auto node = std::make_unique<UINode>();
  node->name = j.value("name", "Node");
  node->type = (UINodeType)j.value("type", 0);

  if (j.contains("position")) {
    node->position.x = j["position"][0];
    node->position.y = j["position"][1];
  }
  if (j.contains("scale")) {
    node->scale.x = j["scale"][0];
    node->scale.y = j["scale"][1];
  }
  if (j.contains("anchorPoint")) {
    node->anchorPoint.x = j["anchorPoint"][0];
    node->anchorPoint.y = j["anchorPoint"][1];
  }

  node->textString = j.value("textString", "");
  node->fontName = j.value("fontName", "Roboto");

  if (j.contains("color")) {
    node->color.x = j["color"][0];
    node->color.y = j["color"][1];
    node->color.z = j["color"][2];
    node->color.w = j["color"][3];
  }

  node->textureName = j.value("textureName", "");

  node->isSelectable = j.value("isSelectable", false);
  if (j.contains("focusedColor")) {
    node->focusedColor.x = j["focusedColor"][0];
    node->focusedColor.y = j["focusedColor"][1];
    node->focusedColor.z = j["focusedColor"][2];
    node->focusedColor.w = j["focusedColor"][3];
  }

  node->Initialize(renderer);

  if (j.contains("children")) {
    for (const auto &childJ : j["children"]) {
      node->children.push_back(DeserializeNode(childJ, renderer));
    }
  }

  return node;
}

void UIManager::Save(const std::string &filePath) {
  if (!rootNode_)
    return;
  json j = SerializeNode(rootNode_.get());
  std::ofstream file(filePath);
  if (file.is_open()) {
    file << j.dump(4);
  }
}

void UIManager::Load(const std::string &filePath) {
  currentFilePath_ = filePath;
  globalAlpha_ = 1.0f;
  std::ifstream file(filePath);
  if (file.is_open()) {
    json j;
    file >> j;
    rootNode_ = DeserializeNode(j, renderer_);
  } else {
    rootNode_ = std::make_unique<UINode>();
    rootNode_->name = "Canvas";
    rootNode_->type = UINodeType::Node;
    rootNode_->Initialize(renderer_);
  }
  selectedNode_ = nullptr;
}

void UIManager::DrawEditorGizmo(const ImVec2 &screenPos,
                                const ImVec2 &imageSize) {
#ifdef USE_IMGUI
  if (!selectedNode_)
    return;

  ImGuiIO &io = ImGui::GetIO();
  ImVec2 mousePos = ImGui::GetMousePos();

  // Check if mouse is within Game View area
  if (mousePos.x >= screenPos.x && mousePos.x <= screenPos.x + imageSize.x &&
      mousePos.y >= screenPos.y && mousePos.y <= screenPos.y + imageSize.y) {

    float scaleX = imageSize.x / 1280.0f;
    float scaleY = imageSize.y / 720.0f;

    float nodeX = screenPos.x + selectedNode_->position.x * scaleX;
    float nodeY = screenPos.y + selectedNode_->position.y * scaleY;

    // Calculate actual size
    float baseWidth = 100.0f;
    float baseHeight = 100.0f;

    if (selectedNode_->type == UINodeType::Text && selectedNode_->textObject) {
      Vector2 s = selectedNode_->textObject->GetSize();
      baseWidth = s.x;
      baseHeight = s.y;
    } else if (selectedNode_->type == UINodeType::Image &&
               selectedNode_->spriteObject) {
      Vector2 s = selectedNode_->spriteObject->GetSize();
      baseWidth = s.x;
      baseHeight = s.y;
    }

    float width = baseWidth * scaleX * selectedNode_->scale.x;
    float height = baseHeight * scaleY * selectedNode_->scale.y;

    // Apply anchor offset
    nodeX -= width * selectedNode_->anchorPoint.x;
    nodeY -= height * selectedNode_->anchorPoint.y;

    // Ensure minimum clickable area
    if (width < 20.0f)
      width = 20.0f;
    if (height < 20.0f)
      height = 20.0f;

    // Draw yellow bounding box
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->AddRect(ImVec2(nodeX, nodeY),
                      ImVec2(nodeX + width, nodeY + height),
                      IM_COL32(255, 255, 0, 255));

    bool isHovering = (mousePos.x >= nodeX && mousePos.x <= nodeX + width &&
                       mousePos.y >= nodeY && mousePos.y <= nodeY + height);

    static bool isDragging = false;
    static ImVec2 dragOffset = {0, 0};

    if (isHovering && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      isDragging = true;
      dragOffset = ImVec2(
          selectedNode_->position.x - (mousePos.x - screenPos.x) / scaleX,
          selectedNode_->position.y - (mousePos.y - screenPos.y) / scaleY);
    }

    if (isDragging && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
      selectedNode_->position.x =
          (mousePos.x - screenPos.x) / scaleX + dragOffset.x;
      selectedNode_->position.y =
          (mousePos.y - screenPos.y) / scaleY + dragOffset.y;
      if (selectedNode_->textObject)
        selectedNode_->textObject->SetPosition(selectedNode_->position);
      if (selectedNode_->spriteObject)
        selectedNode_->spriteObject->SetPosition(selectedNode_->position);
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
      isDragging = false;
    }
  }
#endif
}
