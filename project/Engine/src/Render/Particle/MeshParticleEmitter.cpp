#include "Particle/MeshParticleEmitter.h"
#include "Particle/ParticleManager.h"
#include "Math/MathUtil.h"
#include "Render/Primitive/Ring.h"
#include "Render/Primitive/Sphere.h"
#include "Render/Primitive/Cylinder.h"
#include <cstring>

void MeshParticleEmitter::InitializeAsPlane(const std::string& textureFilePath) {
    BaseInitialize(textureFilePath, kDefaultMaxParticles);
    materialData_->isBillboard = 0;
    CreateVertexDataFromPlane();
}

void MeshParticleEmitter::InitializeAsRing(const std::string& textureFilePath, 
    uint32_t divide, float outerRadius, float innerRadius) {
    BaseInitialize(textureFilePath, kDefaultMaxParticles);
    materialData_->isBillboard = 0;
    
    Ring ring;
    ring.Build(divide, outerRadius, innerRadius);
    CreateVertexDataFromVertices(ring.GetVertices());
}

void MeshParticleEmitter::InitializeAsSphere(const std::string& textureFilePath, uint32_t divide, float radius) {
    BaseInitialize(textureFilePath, kDefaultMaxParticles);
    materialData_->isBillboard = 0;

    Sphere sphere;
    sphere.Build(divide, radius);
    CreateVertexDataFromVertices(sphere.GetVertices());
}

void MeshParticleEmitter::InitializeAsCylinder(const std::string& textureFilePath, 
    uint32_t divide, float topRadius, float bottomRadius, float height) {
    BaseInitialize(textureFilePath, kDefaultMaxParticles);
    materialData_->isBillboard = 0;
    
    Cylinder cylinder;
    cylinder.Build(divide, topRadius, bottomRadius, height);
    CreateVertexDataFromVertices(cylinder.GetVertices());
}

void MeshParticleEmitter::CreateVertexDataFromPlane() {
    auto dx12Core = ParticleManager::GetInstance()->GetDx12Core();

    vertexResource_ = dx12Core->CreateBufferResource(sizeof(VertexData) * 6);

    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = sizeof(VertexData) * 6;
    vertexBufferView_.StrideInBytes = sizeof(VertexData);

    VertexData* vertexData = nullptr;
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));

    vertexData[0].position = { -1.0f, 1.0f, 0.0f, 1.0f };
    vertexData[0].texcoord = { 0.0f, 0.0f };
    vertexData[0].normal = { 0.0f, 0.0f, 1.0f };

    vertexData[1].position = { 1.0f, 1.0f, 0.0f, 1.0f };
    vertexData[1].texcoord = { 1.0f, 0.0f };
    vertexData[1].normal = { 0.0f, 0.0f, 1.0f };

    vertexData[2].position = { -1.0f, -1.0f, 0.0f, 1.0f };
    vertexData[2].texcoord = { 0.0f, 1.0f };
    vertexData[2].normal = { 0.0f, 0.0f, 1.0f };

    vertexData[3].position = { -1.0f, -1.0f, 0.0f, 1.0f };
    vertexData[3].texcoord = { 0.0f, 1.0f };
    vertexData[3].normal = { 0.0f, 0.0f, 1.0f };

    vertexData[4].position = { 1.0f, 1.0f, 0.0f, 1.0f };
    vertexData[4].texcoord = { 1.0f, 0.0f };
    vertexData[4].normal = { 0.0f, 0.0f, 1.0f };

    vertexData[5].position = { 1.0f, -1.0f, 0.0f, 1.0f };
    vertexData[5].texcoord = { 1.0f, 1.0f };
    vertexData[5].normal = { 0.0f, 0.0f, 1.0f };

    vertexCount_ = 6;
}

void MeshParticleEmitter::CreateVertexDataFromVertices(const std::vector<VertexData>& vertices) {
    auto dx12Core = ParticleManager::GetInstance()->GetDx12Core();
    
    vertexCount_ = static_cast<uint32_t>(vertices.size());
    if (vertexCount_ == 0) return;

    vertexResource_ = dx12Core->CreateBufferResource(sizeof(VertexData) * vertices.size());
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = UINT(sizeof(VertexData) * vertices.size());
    vertexBufferView_.StrideInBytes = sizeof(VertexData);

    VertexData* dst = nullptr;
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&dst));
    std::memcpy(dst, vertices.data(), sizeof(VertexData) * vertices.size());
}

void MeshParticleEmitter::Update(const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix) {
    auto pm = ParticleManager::GetInstance();
    if (pm->GetPerViewData()) {
        pm->GetPerViewData()->viewProjection = Multiply(viewMatrix, projectionMatrix);
        Matrix4x4 cameraMatrix = Inverse(viewMatrix);
        pm->GetPerViewData()->cameraPosition = {cameraMatrix.m[3][0], cameraMatrix.m[3][1], cameraMatrix.m[3][2]};
    }
}
