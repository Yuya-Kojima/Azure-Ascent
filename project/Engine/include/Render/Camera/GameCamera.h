#pragma once
#include "ICamera.h"
#include "Math/Matrix4x4.h"
#include "Math/Transform.h"

class GameCamera : public ICamera {

public:
  GameCamera();

  void Update();

  void SetRotate(const Vector3 &rotate) { transform.rotate = rotate; }
  void SetTranslate(const Vector3 &translate) {
    transform.translate = translate;
  }
  void SetFovY(float fovY) { fov = fovY; }
  float GetFovY() const { return fov; }
  void SetAspectRatio(float aspect) { aspectRatio = aspect; }
  void SetNearClip(float nearClipValue) { nearClip = nearClipValue; }
  void SetFarClip(float farClipValue) { farClip = farClipValue; }

  const Matrix4x4 &GetWorldMatrix() const { return worldMatrix; }
  const Matrix4x4 &GetViewMatrix() const override { return viewMatrix; }
  const Matrix4x4 &GetProjectionMatrix() const override {
    return projectionMatrix;
  }
  const Matrix4x4 &GetViewProjectionMatrix() const override {
    return viewProjectionMatrix;
  }
  const Vector3 &GetRotate() const { return transform.rotate; }
  Vector3 GetTranslate() const override { return transform.translate; }

private:
  Transform transform;
  Matrix4x4 worldMatrix;
  Matrix4x4 viewMatrix;

private:
  Matrix4x4 projectionMatrix;
  float fov;
  float aspectRatio;
  float nearClip;
  float farClip;

private:
  Matrix4x4 viewProjectionMatrix;
};
