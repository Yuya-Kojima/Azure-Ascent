#include "Render/Light/LightManager.h"
#include "Debug/Logger.h"
#include "Math/MathUtil.h"
#include "Render/Light/TiledLightCuller.h"
#include "Renderer/LineRenderer.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <numbers>
#include <string>

#ifdef USE_IMGUI
#include <imgui.h>
#endif

// StructuredBuffer は 1 要素の大きさ（ストライド）で次の要素を読むので、シェーダーの構造体と同じ大きさにする
static_assert(sizeof(LightManager::PointLight) == 48, "PointLight はシェーダーと同じ 48 バイトにする");
static_assert(sizeof(LightManager::SpotLight) == 64, "SpotLight はシェーダーと同じ 64 バイトにする");

void LightManager::Initialize(Dx12Core *dx12Core) {
	dx12Core_ = dx12Core;

	pointLights_.reserve(kMaxPointLights_);
	spotLights_.reserve(kMaxSpotLights_);

	// 名簿のバッファは上限の大きさで最初に 1 回だけ作る
	pointLightResource_ = dx12Core_->CreateBufferResource(sizeof(PointLight) * kMaxPointLights_);
	pointLightResource_->Map(0, nullptr, reinterpret_cast<void **>(&pointLightData_));

	spotLightResource_ = dx12Core_->CreateBufferResource(sizeof(SpotLight) * kMaxSpotLights_);
	spotLightResource_->Map(0, nullptr, reinterpret_cast<void **>(&spotLightData_));

	// 定数バッファは 256 バイト単位で作る
	const size_t countsSize = (sizeof(LightCounts) + 255) & ~size_t(255);
	countsResource_ = dx12Core_->CreateBufferResource(countsSize);
	countsResource_->Map(0, nullptr, reinterpret_cast<void **>(&countsData_));
	*countsData_ = {};
}

void LightManager::Clear() {
	pointLights_.clear();
	spotLights_.clear();
}

void LightManager::AddPointLight(const PointLight &light) {
	if (pointLights_.size() >= kMaxPointLights_) {
		if (!hasReportedOverflow_) {
			Logger::Log("[LightManager] 点の光が上限 " + std::to_string(kMaxPointLights_) + " を超えた\n");
			hasReportedOverflow_ = true;
		}
		assert(false && "LightManager: 点の光が上限を超えた");
		return;
	}
	pointLights_.push_back(light);
}

void LightManager::AddSpotLight(const SpotLight &light) {
	if (spotLights_.size() >= kMaxSpotLights_) {
		if (!hasReportedOverflow_) {
			Logger::Log("[LightManager] スポットライトが上限 " + std::to_string(kMaxSpotLights_) + " を超えた\n");
			hasReportedOverflow_ = true;
		}
		assert(false && "LightManager: スポットライトが上限を超えた");
		return;
	}
	spotLights_.push_back(light);
}

void LightManager::Upload() {
	// 書き込む数だけ写す。数より後ろに残った古い値は、シェーダーが数までしか読まないので使われない
	if (!pointLights_.empty()) {
		std::memcpy(pointLightData_, pointLights_.data(), sizeof(PointLight) * pointLights_.size());
	}
	if (!spotLights_.empty()) {
		std::memcpy(spotLightData_, spotLights_.data(), sizeof(SpotLight) * spotLights_.size());
	}
	countsData_->pointLightCount = GetPointLightCount();
	countsData_->spotLightCount = GetSpotLightCount();
	countsData_->tileCountX = tiledLightCuller_ ? tiledLightCuller_->GetTileCountX() : 0;
	countsData_->useTiles = (tiledLightCuller_ && tiledLightCuller_->IsActive()) ? 1u : 0u;
}

void LightManager::DrawRanges() const {
	if (!showRanges_) {
		return;
	}
	LineRenderer *lineRenderer = LineRenderer::GetInstance();
	const float step = 2.0f * std::numbers::pi_v<float> / kRangeCircleSegments_;

	// 中心 center、半径 radius の円を、互いに直交する 2 本の軸 axisA・axisB が張る面に描く
	auto drawCircle = [&](const Vector3 &center, const Vector3 &axisA, const Vector3 &axisB, float radius,
	                      const Vector4 &color) {
		for (int i = 0; i < kRangeCircleSegments_; ++i) {
			const float a0 = step * i;
			const float a1 = step * (i + 1);
			const Vector3 p0 = center + axisA * (std::cos(a0) * radius) + axisB * (std::sin(a0) * radius);
			const Vector3 p1 = center + axisA * (std::cos(a1) * radius) + axisB * (std::sin(a1) * radius);
			lineRenderer->DrawLine(p0, p1, color);
		}
	};

	const Vector3 axisX = {1.0f, 0.0f, 0.0f};
	const Vector3 axisY = {0.0f, 1.0f, 0.0f};
	const Vector3 axisZ = {0.0f, 0.0f, 1.0f};

	// 点の光: XY・YZ・XZ の 3 つの円で、届く範囲の球を表す
	for (const PointLight &light : pointLights_) {
		const Vector4 color = {light.color.x, light.color.y, light.color.z, 1.0f};
		drawCircle(light.position, axisX, axisY, light.radius, color);
		drawCircle(light.position, axisY, axisZ, light.radius, color);
		drawCircle(light.position, axisX, axisZ, light.radius, color);
	}

	// スポットライト: 届く距離の所の円と、光の元からその円へ伸びる 4 本の線で円錐を表す
	for (const SpotLight &light : spotLights_) {
		const Vector4 color = {light.color.x, light.color.y, light.color.z, 1.0f};
		const Vector3 direction = SafeNormalize(light.direction);
		// 向きとほぼ平行でない軸を選び、向きに直交する 2 本の軸を作る
		const Vector3 helper = (std::abs(direction.y) < 0.99f) ? axisY : axisX;
		const Vector3 sideA = Normalize(Cross(direction, helper));
		const Vector3 sideB = Cross(direction, sideA);

		const float cosAngle = std::clamp(light.cosAngle, -1.0f, 1.0f);
		const float sinAngle = std::sqrt(1.0f - cosAngle * cosAngle);
		// 円錐の母線の長さを distance とし、底の円の中心までの長さと半径を出す
		const Vector3 baseCenter = light.position + direction * (light.distance * cosAngle);
		const float baseRadius = light.distance * sinAngle;

		drawCircle(baseCenter, sideA, sideB, baseRadius, color);
		lineRenderer->DrawLine(light.position, baseCenter + sideA * baseRadius, color);
		lineRenderer->DrawLine(light.position, baseCenter - sideA * baseRadius, color);
		lineRenderer->DrawLine(light.position, baseCenter + sideB * baseRadius, color);
		lineRenderer->DrawLine(light.position, baseCenter - sideB * baseRadius, color);
	}
}

void LightManager::DrawImGui() {
#ifdef USE_IMGUI
	ImGui::Text("Point Lights %u / %u", GetPointLightCount(), kMaxPointLights_);
	ImGui::Text("Spot Lights  %u / %u", GetSpotLightCount(), kMaxSpotLights_);
	ImGui::Checkbox("Show Light Ranges", &showRanges_);
	ImGui::SameLine();
	ImGui::TextDisabled("(scenes that draw lines)");

	// オフにすると、マスに分けずに画素ごとに名簿の全部を足す
	if (tiledLightCuller_) {
		bool tiledCulling = tiledLightCuller_->IsEnabled();
		if (ImGui::Checkbox("Tiled Culling", &tiledCulling)) {
			tiledLightCuller_->SetEnabled(tiledCulling);
		}
	}

	// 名簿は毎フレーム作り直されるので、一覧は読むだけにする
	if (ImGui::TreeNode("Point Light List")) {
		for (uint32_t i = 0; i < GetPointLightCount(); ++i) {
			const PointLight &light = pointLights_[i];
			ImGui::ColorButton(("##PointColor" + std::to_string(i)).c_str(),
			                   ImVec4(light.color.x, light.color.y, light.color.z, 1.0f),
			                   ImGuiColorEditFlags_NoTooltip, ImVec2(12.0f, 12.0f));
			ImGui::SameLine();
			ImGui::Text("%3u pos(%.1f, %.1f, %.1f) int %.2f r %.1f decay %.1f", i, light.position.x,
			            light.position.y, light.position.z, light.intensity, light.radius, light.decay);
		}
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("Spot Light List")) {
		for (uint32_t i = 0; i < GetSpotLightCount(); ++i) {
			const SpotLight &light = spotLights_[i];
			ImGui::ColorButton(("##SpotColor" + std::to_string(i)).c_str(),
			                   ImVec4(light.color.x, light.color.y, light.color.z, 1.0f),
			                   ImGuiColorEditFlags_NoTooltip, ImVec2(12.0f, 12.0f));
			ImGui::SameLine();
			ImGui::Text("%3u pos(%.1f, %.1f, %.1f) dir(%.2f, %.2f, %.2f) int %.2f dist %.1f", i, light.position.x,
			            light.position.y, light.position.z, light.direction.x, light.direction.y,
			            light.direction.z, light.intensity, light.distance);
		}
		ImGui::TreePop();
	}
#endif
}
