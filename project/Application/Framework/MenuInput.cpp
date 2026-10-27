#include "MenuInput.h"
#include "Input/Input.h"
#include "Input/InputKeyState.h"

namespace MenuInput {

bool IsDecideTriggered(Input *input) {
  return input->IsTriggerKey(DIK_SPACE) || input->IsPadTrigger(PadButton::A);
}

bool IsSkipHeld(Input *input) {
  return input->IsPressKey(DIK_SPACE) || input->IsPadPress(PadButton::A) ||
         input->IsPadPress(PadButton::Start);
}

bool IsUpTriggered(Input *input) {
  return input->IsTriggerKey(DIK_W) || input->IsTriggerKey(DIK_UP) ||
         input->IsPadTrigger(PadButton::DPadUp);
}

bool IsDownTriggered(Input *input) {
  return input->IsTriggerKey(DIK_S) || input->IsTriggerKey(DIK_DOWN) ||
         input->IsPadTrigger(PadButton::DPadDown);
}

bool IsLeftTriggered(Input *input) {
  return input->IsTriggerKey(DIK_A) || input->IsTriggerKey(DIK_LEFT) ||
         input->IsPadTrigger(PadButton::DPadLeft);
}

bool IsRightTriggered(Input *input) {
  return input->IsTriggerKey(DIK_D) || input->IsTriggerKey(DIK_RIGHT) ||
         input->IsPadTrigger(PadButton::DPadRight);
}

} // namespace MenuInput
