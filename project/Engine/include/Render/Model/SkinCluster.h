#pragma once
#include <vector>
#include <array>
#include <span>
#include <wrl.h>
#include <d3d12.h>
#include "Core/SrvHandle.h"
#include "Math/MathUtil.h"
#include "Render/Model/Model.h"

class Dx12Core;
class SrvManager;
struct Skeleton;

// 最大4Jointの影響を受ける
const uint32_t kNumMaxInfluence = 4;

struct VertexInfluence {
    std::array<float, kNumMaxInfluence> weights;
    std::array<int32_t, kNumMaxInfluence> jointIndices;
};

struct WellForGPU {
    Matrix4x4 skeletonSpaceMatrix; // 位置用
    Matrix4x4 skeletonSpaceInverseTransposeMatrix; // 法線用
};

struct SkinningInformation {
    uint32_t numVertices;
};


struct SkinCluster {
    std::vector<Matrix4x4> inverseBindPoseMatrices;

    Microsoft::WRL::ComPtr<ID3D12Resource> influenceResource;
    std::span<VertexInfluence> mappedInfluence;
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> influenceSrvHandle;
    SrvHandle influenceSrv;

    Microsoft::WRL::ComPtr<ID3D12Resource> paletteResource;
    std::span<WellForGPU> mappedPalette;
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> paletteSrvHandle;
    SrvHandle paletteSrv;

    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> inputVertexSrvHandle;
    SrvHandle inputVertexSrv;

    Microsoft::WRL::ComPtr<ID3D12Resource> skinnedVertexResource;
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> skinnedVertexUavHandle;
    SrvHandle skinnedVertexUav;
    D3D12_VERTEX_BUFFER_VIEW skinnedVertexBufferView;

    Microsoft::WRL::ComPtr<ID3D12Resource> skinningInformationResource;
};

SkinCluster CreateSkinCluster(Dx12Core* dx12Core, SrvManager* srvManager, const Skeleton& skeleton, Model* model);
void Update(SkinCluster& skinCluster, const Skeleton& skeleton);
