#pragma once
#include "Core/Dx12Core.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include <cstdint>
#include <vector>
#include <wrl.h>

class TiledLightCuller;

/// <summary>
/// 点の光とスポットライトの名簿。
/// 毎フレーム Clear で白紙にし、光らせたい物が Update の中で Add を呼んで書き込む。
/// Upload で名簿を GPU のバッファへ書き写し、Object3d のピクセルシェーダーが名簿の全部を足し合わせる
/// </summary>
class LightManager {
public:
	// シェーダーの StructuredBuffer<PointLight> と同じ並び（48 バイト）
	struct PointLight {
		Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f}; // 色（rgb を使う）
		Vector3 position = {};                    // 位置
		float intensity = 1.0f;                   // 輝度
		float radius = 5.0f;                      // ライトの届く最大距離
		float decay = 2.0f;                       // 減衰率
		float padding[2] = {};
	};

	// シェーダーの StructuredBuffer<SpotLight> と同じ並び（64 バイト）
	struct SpotLight {
		Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f}; // 色（rgb を使う）
		Vector3 position = {};                    // 位置
		float intensity = 1.0f;                   // 輝度
		Vector3 direction = {0.0f, -1.0f, 0.0f};  // 照らす向き（長さ 1）
		float distance = 7.0f;                    // ライトの届く最大距離
		float decay = 2.0f;                       // 減衰率
		float cosAngle = 0.5f;                    // 光の円錐の半分の角度の余弦
		float padding[2] = {};
	};

	// 名簿に書ける数の上限
	static constexpr uint32_t kMaxPointLights_ = 256;
	static constexpr uint32_t kMaxSpotLights_ = 64;

	/// <summary>
	/// 上限の大きさで GPU のバッファを作り、書き込み先を開いたままにする
	/// </summary>
	void Initialize(Dx12Core *dx12Core);

	/// <summary>
	/// 名簿を白紙にする（毎フレーム、シーンの Update より前に呼ぶ）
	/// </summary>
	void Clear();

	/// <summary>
	/// 点の光を名簿に書く。上限を超えたら Debug では止め、それ以外では無視する
	/// </summary>
	void AddPointLight(const PointLight &light);

	/// <summary>
	/// スポットライトを名簿に書く。上限を超えたら Debug では止め、それ以外では無視する
	/// </summary>
	void AddSpotLight(const SpotLight &light);

	/// <summary>
	/// 名簿とライトの数を GPU のバッファへ書き写す（3D を描く前、Begin3DPass の後に呼ぶ）。
	/// このフレームでマス分けをしているかと横のマスの数も、ライトの数と一緒に書く
	/// </summary>
	void Upload();

	/// <summary>
	/// マス分けを渡す（ImGui のオン・オフと、Upload で書くマスの情報に使う）
	/// </summary>
	void SetTiledLightCuller(TiledLightCuller *tiledLightCuller) { tiledLightCuller_ = tiledLightCuller; }

	/// <summary>
	/// 範囲の表示が有効なとき、届く範囲を線で LineRenderer に登録する
	/// （点は 3 つの円の球、スポットは円錐）
	/// </summary>
	void DrawRanges() const;

	/// <summary>
	/// ImGui にライトの数と一覧を出す（今いるウィンドウの中に描く）
	/// </summary>
	void DrawImGui();

	uint32_t GetPointLightCount() const { return static_cast<uint32_t>(pointLights_.size()); }
	uint32_t GetSpotLightCount() const { return static_cast<uint32_t>(spotLights_.size()); }

	// ライトの数の定数バッファ（b3）
	D3D12_GPU_VIRTUAL_ADDRESS GetCountsAddress() const { return countsResource_->GetGPUVirtualAddress(); }
	// 点の光の名簿（t3）
	D3D12_GPU_VIRTUAL_ADDRESS GetPointLightsAddress() const { return pointLightResource_->GetGPUVirtualAddress(); }
	// スポットライトの名簿（t4）
	D3D12_GPU_VIRTUAL_ADDRESS GetSpotLightsAddress() const { return spotLightResource_->GetGPUVirtualAddress(); }

private:
	// シェーダーの LightCounts と同じ並び（16 バイト）
	struct LightCounts {
		uint32_t pointLightCount;
		uint32_t spotLightCount;
		uint32_t tileCountX; // 横のマスの数
		uint32_t useTiles;   // 1 = マスごとの名簿を使う
	};

	// 範囲の線の円 1 周の分割数
	static constexpr int kRangeCircleSegments_ = 32;

	Dx12Core *dx12Core_ = nullptr;
	TiledLightCuller *tiledLightCuller_ = nullptr;

	// CPU 側の名簿
	std::vector<PointLight> pointLights_;
	std::vector<SpotLight> spotLights_;

	// GPU 側の名簿（アップロードヒープ。開いたままの書き込み先を持つ）
	Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> countsResource_;
	PointLight *pointLightData_ = nullptr;
	SpotLight *spotLightData_ = nullptr;
	LightCounts *countsData_ = nullptr;

	// 上限を超えたことをもう知らせたか（ログを 1 回だけ出す）
	bool hasReportedOverflow_ = false;

	// 届く範囲を線で描くか
	bool showRanges_ = false;
};
