#include "Render/Model/SkinCluster.h"
#include "Render/Model/Skeleton.h"
#include "Core/Dx12Core.h"
#include "Core/SrvManager.h"
#include <algorithm>

SkinCluster CreateSkinCluster(Dx12Core* dx12Core, SrvManager* srvManager, const Skeleton& skeleton, Model* model) {
    SkinCluster skinCluster;
    const Model::ModelData& modelData = model->GetModelData();

    // 1. palette用のResourceを確保
    skinCluster.paletteResource = dx12Core->CreateBufferResource(sizeof(WellForGPU) * skeleton.joints.size());
    WellForGPU* mappedPalette = nullptr;
    skinCluster.paletteResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedPalette));
    skinCluster.mappedPalette = { mappedPalette, skeleton.joints.size() };

    // 2. palette用のsrvを作成 (StructuredBuffer)
    skinCluster.paletteSrv = srvManager->Allocate();
    uint32_t srvIndex = skinCluster.paletteSrv.Index();
    skinCluster.paletteSrvHandle.first = srvManager->GetCPUDescriptorHandle(srvIndex);
    skinCluster.paletteSrvHandle.second = srvManager->GetGPUDescriptorHandle(srvIndex);
    srvManager->CreateSRVforStructuredBuffer(srvIndex, skinCluster.paletteResource.Get(), static_cast<UINT>(skeleton.joints.size()), sizeof(WellForGPU));

    // 3. influence用のResourceを確保
    skinCluster.influenceResource = dx12Core->CreateBufferResource(sizeof(VertexInfluence) * modelData.vertices.size());
    VertexInfluence* mappedInfluence = nullptr;
    skinCluster.influenceResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedInfluence));
    std::memset(mappedInfluence, 0, sizeof(VertexInfluence) * modelData.vertices.size()); // 0埋め。weightを0にしておく。
    skinCluster.mappedInfluence = { mappedInfluence, modelData.vertices.size() };

    // 4. Influence用のSRVを作成
    skinCluster.influenceSrv = srvManager->Allocate();
    uint32_t influenceSrvIndex = skinCluster.influenceSrv.Index();
    skinCluster.influenceSrvHandle.first = srvManager->GetCPUDescriptorHandle(influenceSrvIndex);
    skinCluster.influenceSrvHandle.second = srvManager->GetGPUDescriptorHandle(influenceSrvIndex);
    srvManager->CreateSRVforStructuredBuffer(influenceSrvIndex, skinCluster.influenceResource.Get(), static_cast<UINT>(modelData.vertices.size()), sizeof(VertexInfluence));

    // 5. InverseBindPoseMatrixの保存領域を作成して、単位行列で埋める
    skinCluster.inverseBindPoseMatrices.resize(skeleton.joints.size());
    std::generate(skinCluster.inverseBindPoseMatrices.begin(), skinCluster.inverseBindPoseMatrices.end(), MakeIdentity4x4);

    // 6. ModelDataのSkinCluster情報を解析してInfluenceの中身を埋める
    for (const auto& jointWeight : modelData.skinClusterData) {
        auto it = skeleton.jointMap.find(jointWeight.first);
        if (it == skeleton.jointMap.end()) {
            continue; // そんな名前のJointは存在しないので次に回す
        }

        // (*it).secondにはjointのindexが入っているので、該当のindexのinverseBindPoseMatrixを代入
        skinCluster.inverseBindPoseMatrices[(*it).second] = jointWeight.second.inverseBindPoseMatrix;

        for (const auto& vertexWeight : jointWeight.second.vertexWeights) {
            auto& currentInfluence = skinCluster.mappedInfluence[vertexWeight.vertexIndex]; // 該当のvertexIndexのinfluence情報を参照しておく
            for (uint32_t index = 0; index < kNumMaxInfluence; ++index) {
                if (currentInfluence.weights[index] == 0.0f) { // weight==0が空いている状態なので、その場所にweightとjointのindexを代入
                    currentInfluence.weights[index] = vertexWeight.weight;
                    currentInfluence.jointIndices[index] = (*it).second;
                    break;
                }
            }
        }
    }

    // 7. 入力頂点用のSRVを作成 (ModelのVertexResourceを利用)
    skinCluster.inputVertexSrv = srvManager->Allocate();
    uint32_t inputVertexSrvIndex = skinCluster.inputVertexSrv.Index();
    skinCluster.inputVertexSrvHandle.first = srvManager->GetCPUDescriptorHandle(inputVertexSrvIndex);
    skinCluster.inputVertexSrvHandle.second = srvManager->GetGPUDescriptorHandle(inputVertexSrvIndex);
    srvManager->CreateSRVforStructuredBuffer(inputVertexSrvIndex, model->GetVertexResource(), static_cast<UINT>(modelData.vertices.size()), sizeof(Model::VertexData));

    // 8. 出力頂点用のResource (SkinnedVertex) を確保 (UAVとして書き込めるようにDEFAULTヒープで作成)
    skinCluster.skinnedVertexResource = dx12Core->CreateUAVBufferResource(sizeof(Model::VertexData) * modelData.vertices.size());

    // 9. 出力頂点用のUAVを作成
    skinCluster.skinnedVertexUav = srvManager->Allocate();
    uint32_t skinnedVertexUavIndex = skinCluster.skinnedVertexUav.Index();
    skinCluster.skinnedVertexUavHandle.first = srvManager->GetCPUDescriptorHandle(skinnedVertexUavIndex);
    skinCluster.skinnedVertexUavHandle.second = srvManager->GetGPUDescriptorHandle(skinnedVertexUavIndex);
    srvManager->CreateUAVforStructuredBuffer(skinnedVertexUavIndex, skinCluster.skinnedVertexResource.Get(), static_cast<UINT>(modelData.vertices.size()), sizeof(Model::VertexData));

    // 10. 出力頂点用のVBVを作成 (描画用)
    skinCluster.skinnedVertexBufferView.BufferLocation = skinCluster.skinnedVertexResource->GetGPUVirtualAddress();
    skinCluster.skinnedVertexBufferView.SizeInBytes = UINT(sizeof(Model::VertexData) * modelData.vertices.size());
    skinCluster.skinnedVertexBufferView.StrideInBytes = sizeof(Model::VertexData);

    // 11. SkinningInformation (CBV) を作成 (256バイトアラインメント)
    UINT cbvSize = (sizeof(SkinningInformation) + 255) & ~255;
    skinCluster.skinningInformationResource = dx12Core->CreateBufferResource(cbvSize);
    SkinningInformation* mappedInfo = nullptr;
    skinCluster.skinningInformationResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedInfo));
    mappedInfo->numVertices = static_cast<uint32_t>(modelData.vertices.size());
    // mappedInfoのUnmapは省略（MapしっぱなしでOK）

    return skinCluster;
}

void Update(SkinCluster& skinCluster, const Skeleton& skeleton) {
    for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex) {
        assert(jointIndex < skinCluster.inverseBindPoseMatrices.size());
        
        // パレットの位置用行列を計算： 初期姿勢の逆行列 * 現在のアニメーション姿勢行列
        skinCluster.mappedPalette[jointIndex].skeletonSpaceMatrix =
            Multiply(skinCluster.inverseBindPoseMatrices[jointIndex], skeleton.joints[jointIndex].skeletonSpaceMatrix);
        
        // パレットの法線用行列を計算： 位置用行列の逆転置行列
        skinCluster.mappedPalette[jointIndex].skeletonSpaceInverseTransposeMatrix =
            Transpose(Inverse(skinCluster.mappedPalette[jointIndex].skeletonSpaceMatrix));
    }
}
