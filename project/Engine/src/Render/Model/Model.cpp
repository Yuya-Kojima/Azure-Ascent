#include "Model/Model.h"
#include "Debug/Logger.h"
#include "Math/MathUtil.h"
#include "Renderer/ModelRenderer.h"
#include "Texture/TextureManager.h"
#include "Render/Model/SkinCluster.h"
#include <fstream>
#include <stdexcept>

void Model::Initialize(ModelRenderer* modelRenderer,
	const std::string& directorypath,
	const std::string& filename) {

	modelRenderer_ = modelRenderer;

	dx12Core_ = modelRenderer_->GetDx12Core();

	LoadModelFile(directorypath, filename);

	CreateVertexData();

	defaultMaterial_.color = Vector4(1, 1, 1, 1);
	defaultMaterial_.enableLighting = true;
	defaultMaterial_.uvTransform = MakeIdentity4x4();
	defaultMaterial_.shininess = 30.0f;
	defaultMaterial_.environmentCoefficient = 0.0f;

	// objデータが参照しているテクスチャ読み込み
	if (!modelData_.material.textureFilePath.empty()) {
		TextureManager::GetInstance()->LoadTexture(
			modelData_.material.textureFilePath);
	} else {
		// テクスチャがない場合は白テクスチャを割り当てる
		modelData_.material.textureFilePath = "resources/white1x1.png";
		TextureManager::GetInstance()->LoadTexture(
			modelData_.material.textureFilePath);
	}

	// 読み込んだテクスチャの番号を取得
	// modelData_.material.textureIndex =
	//    TextureManager::GetInstance()->GetTextureIndexByFilePath(
	//        modelData_.material.textureFilePath);
}

void Model::InitializeFromVertices(ModelRenderer* modelRenderer, const std::vector<VertexData>& vertices) {
	modelRenderer_ = modelRenderer;
	dx12Core_ = modelRenderer_->GetDx12Core();

	modelData_.vertices = vertices;
	modelData_.material.textureFilePath = "resources/white1x1.png";
	TextureManager::GetInstance()->LoadTexture(modelData_.material.textureFilePath);
	modelData_.rootNode.localMatrix = MakeIdentity4x4();
	modelData_.rootNode.name = "RootNode";

	CreateVertexData();

	defaultMaterial_.color = Vector4(1, 1, 1, 1);
	defaultMaterial_.enableLighting = true;
	defaultMaterial_.uvTransform = MakeIdentity4x4();
	defaultMaterial_.shininess = 30.0f;
	defaultMaterial_.environmentCoefficient = 0.0f;
}

void Model::InitializeFromModelData(ModelRenderer* modelRenderer, const ModelData& modelData) {
	modelRenderer_ = modelRenderer;
	dx12Core_ = modelRenderer_->GetDx12Core();

	modelData_ = modelData;
	if (modelData_.material.textureFilePath.empty()) {
		modelData_.material.textureFilePath = "resources/white1x1.png";
	}
	TextureManager::GetInstance()->LoadTexture(modelData_.material.textureFilePath);

	CreateVertexData();

	defaultMaterial_.color = Vector4(1, 1, 1, 1);
	defaultMaterial_.enableLighting = true;
	defaultMaterial_.uvTransform = MakeIdentity4x4();
	defaultMaterial_.shininess = 30.0f;
	defaultMaterial_.environmentCoefficient = 0.0f;
}

void Model::Draw(const SkinCluster* skinCluster) {

	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList =
		dx12Core_->GetCommandList();

	if (skinCluster) {
		// Skinning描画の場合、CSで計算されたSkinnedVertexのVBVをセット
		commandList->IASetVertexBuffers(0, 1, &skinCluster->skinnedVertexBufferView);
	} else {
		// 通常の描画の場合、元のVBVだけをセット
		commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
	}

	// SRVのDescriptorTableの先頭を設定。2はrootParameter[2]である。
	commandList->SetGraphicsRootDescriptorTable(
		2, TextureManager::GetInstance()->GetSrvHandleGPU(
			modelData_.material.textureFilePath));

	if (!modelData_.indices.empty()) {
		// インデックスがある場合はIndexBufferを使って描画
		commandList->IASetIndexBuffer(&indexBufferView_);
		commandList->DrawIndexedInstanced(UINT(modelData_.indices.size()), 1, 0, 0, 0);
	} else {
		// インデックスがない場合（Primitive等）はVertexBufferのみで描画
		commandList->DrawInstanced(UINT(modelData_.vertices.size()), 1, 0, 0);
	}
}

Model::MaterialData
Model::LoadMaterialTemplateFile(const std::string& directoryPath,
	const std::string& filename) {
	// 1. 中で必要となる変数の宣言
	MaterialData materialData; // 構築するMaterialData
	std::string line;          // ファイルから読んだ一行を格納する

	// 2. ファイルを開く
	std::ifstream file(directoryPath + "/" + filename);
	if (!file.is_open()) {
		throw std::runtime_error("Material file not found: " + directoryPath + "/" + filename);
	}

	// 3. 実際にファイルを読み、MaterialDataを構築していく
	while (std::getline(file, line)) {

		std::string identifier;
		std::istringstream s(line);

		s >> identifier;

		// identifierに応じた処理
		if (identifier == "map_Kd") {
			std::string textureFilename;
			s >> textureFilename;
			// 連結してファイルパスにする
			materialData.textureFilePath = directoryPath + "/" + textureFilename;
		}
	}

	// 4. MaterialDataを返す
	return materialData;
}

void Model::LoadModelFile(const std::string& directoryPath,
	const std::string& filename) {

	// assimp読み込み
	Assimp::Importer importer;
	std::string filePath = directoryPath + "/" + filename;

	// 1. 中で必要となる変数の宣言
	ModelData modelData; // 構築するModelData

	const aiScene* scene = importer.ReadFile(
		filePath.c_str(),
		aiProcess_Triangulate | aiProcess_FlipWindingOrder | aiProcess_FlipUVs);

	if (!scene || !scene->HasMeshes()) {
		throw std::runtime_error("Assimp failed to load model: " + filePath + " - " + importer.GetErrorString());
	}

	modelData.rootNode = ReadNode(scene->mRootNode);

	for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
		aiMesh* mesh = scene->mMeshes[meshIndex];

		assert(mesh->HasNormals());        // 法線がないMeshは今回は非対応
		// assert(mesh->HasTextureCoords(0)); // TexcoordがないMeshは今回は非対応

		// 最初から頂点数分のメモリを確保しておく
		modelData.vertices.resize(mesh->mNumVertices);
		for (uint32_t vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex) {
			aiVector3D& position = mesh->mVertices[vertexIndex];
			aiVector3D& normal = mesh->mNormals[vertexIndex];
			
			aiVector3D texcoord = {0.0f, 0.0f, 0.0f};
			if (mesh->HasTextureCoords(0)) {
				texcoord = mesh->mTextureCoords[0][vertexIndex];
			}

			// 右手系->左手系への変換を忘れずに
			modelData.vertices[vertexIndex].position = { -position.x, position.y, position.z, 1.0f };
			modelData.vertices[vertexIndex].normal = { -normal.x, normal.y, normal.z };
			modelData.vertices[vertexIndex].texcoord = { texcoord.x, texcoord.y };
		}

		// 面からIndexの情報を取得する
		for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {
			aiFace& face = mesh->mFaces[faceIndex];
			assert(face.mNumIndices == 3);

			for (uint32_t element = 0; element < face.mNumIndices; ++element) {
				uint32_t vertexIndex = face.mIndices[element];
				modelData.indices.push_back(vertexIndex);
			}
		}

		// SkinCluster構築用のデータ取得を追加
		for (uint32_t boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {
			aiBone* bone = mesh->mBones[boneIndex];
			std::string jointName = bone->mName.C_Str();
			JointWeightData& jointWeightData = modelData.skinClusterData[jointName];

			aiMatrix4x4 bindPoseMatrixAssimp = bone->mOffsetMatrix.Inverse();
			aiVector3D scale, translate;
			aiQuaternion rotate;
			bindPoseMatrixAssimp.Decompose(scale, rotate, translate);
			Matrix4x4 bindPoseMatrix = MakeAffineMatrix(
				{ scale.x, scale.y, scale.z }, { rotate.x, -rotate.y, -rotate.z, rotate.w }, { -translate.x, translate.y, translate.z });
			jointWeightData.inverseBindPoseMatrix = Inverse(bindPoseMatrix);

			for (uint32_t weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex) {
				jointWeightData.vertexWeights.push_back({ bone->mWeights[weightIndex].mWeight, bone->mWeights[weightIndex].mVertexId });
			}
		}
	}

	for (uint32_t materialIndex = 0; materialIndex < scene->mNumMaterials;
		++materialIndex) {
		aiMaterial* material = scene->mMaterials[materialIndex];

		if (material->GetTextureCount(aiTextureType_DIFFUSE) != 0) {
			aiString textureFilePath;
			material->GetTexture(aiTextureType_DIFFUSE, 0, &textureFilePath);

			modelData.material.textureFilePath =
				directoryPath + "/" + textureFilePath.C_Str();
		}
	}

	// std::vector<Vector4> positions; // 位置
	// std::vector<Vector3> normals;   // 法線
	// std::vector<Vector2> texcoords; // テクスチャ座標
	// std::string line;               // ファイルから読んだ一行を格納する

	// VertexData triangle[3];

	//// 2. ファイルを開く
	// std::ifstream file(directoryPath + "/" + filename); // ファイルを開く
	// assert(file.is_open()); // とりあえず開けなかったら止める

	//// 3. 実際にファイルを読み、ModelDataを構築していく
	// while (std::getline(file, line)) {
	//   std::string identifier;
	//   std::istringstream s(line);
	//   s >> identifier; // 先頭の識別子を読む

	//  if (identifier == "v") {
	//    Vector4 position;

	//    s >> position.x >> position.y >> position.z;

	//    position.w = 1.0f;
	//    positions.push_back(position);
	//  } else if (identifier == "vt") {
	//    Vector2 texcoord;

	//    s >> texcoord.x >> texcoord.y;

	//    texcoords.push_back(texcoord);
	//  } else if (identifier == "vn") {
	//    Vector3 normal;

	//    s >> normal.x >> normal.y >> normal.z;

	//    normals.push_back(normal);
	//  } else if (identifier == "f") {

	//    // 面は三角形限定
	//    for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex) {
	//      std::string vertexDefinition;
	//      s >> vertexDefinition;

	//      //
	//      頂点の要素へのIndexは「位置/UV/法線」で格納されているので、分解してIndexを取得する
	//      std::istringstream v(vertexDefinition);

	//      uint32_t elementIndices[3];

	//      for (int32_t element = 0; element < 3; ++element) {
	//        std::string index;
	//        std::getline(v, index, '/'); // 区切りでインデックスを読んでいく
	//        elementIndices[element] = std::stoi(index);
	//      }

	//      // 要素へのIndexから、実際の要素の値を取得して、頂点を構築する
	//      Vector4 position = positions[elementIndices[0] - 1];
	//      Vector2 texcoord = texcoords[elementIndices[1] - 1];
	//      Vector3 normal = normals[elementIndices[2] - 1];

	//      // 右手座標なので反転
	//      position.x *= -1.0f;
	//      normal.x *= -1.0f;
	//      texcoord.y = 1.0f - texcoord.y;

	//      triangle[faceVertex] = {position, texcoord, normal};
	//    }

	//    // 頂点を逆順で登録することで、回り順を逆にする
	//    modelData.vertices.push_back(triangle[2]);
	//    modelData.vertices.push_back(triangle[1]);
	//    modelData.vertices.push_back(triangle[0]);
	//  } else if (identifier == "mtllib") {

	//    // materialTemplateLibraryファイルの名前を取得
	//    std::string materialFilename;

	//    s >> materialFilename;

	//    //
	//    基本的にobjファイルと同一階層にmtlは存在させるので、ディレクトリ名とファイル名を渡す
	//    modelData.material =
	//        LoadMaterialTemplateFile(directoryPath, materialFilename);
	//  }
	//}

	// 4. ModelDataを返す
	modelData_ = modelData;
}

void Model::CreateVertexData() {
	// 頂点リソースを作る
	vertexResource = dx12Core_->CreateBufferResource(sizeof(VertexData) *
		modelData_.vertices.size());

	// リソースの先頭アドレスから使う
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();

	// 使用するリソースのサイズは頂点のサイズ
	vertexBufferView.SizeInBytes =
		UINT(sizeof(VertexData) * modelData_.vertices.size());

	// 1頂点あたりのサイズ
	vertexBufferView.StrideInBytes = sizeof(VertexData);

	vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));

	std::memcpy(vertexData, modelData_.vertices.data(),
		sizeof(VertexData) * modelData_.vertices.size());

	// インデックスデータがある場合はインデックスバッファも作成
	if (!modelData_.indices.empty()) {
		indexResource_ = dx12Core_->CreateBufferResource(sizeof(uint32_t) * modelData_.indices.size());
		
		indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
		indexBufferView_.SizeInBytes = UINT(sizeof(uint32_t) * modelData_.indices.size());
		indexBufferView_.Format = DXGI_FORMAT_R32_UINT;

		uint32_t* mappedIndex = nullptr;
		indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&mappedIndex));
		std::memcpy(mappedIndex, modelData_.indices.data(), sizeof(uint32_t) * modelData_.indices.size());
	}
}

Model::Node Model::ReadNode(aiNode* node) {

	Node result{};

	aiVector3D scale, translate;
	aiQuaternion rotate;
	node->mTransformation.Decompose(scale, rotate, translate);
	
	result.transform.scale = { scale.x, scale.y, scale.z };
	result.transform.rotate = { rotate.x, -rotate.y, -rotate.z, rotate.w };
	result.transform.translate = { -translate.x, translate.y, translate.z };
	
	result.localMatrix = MakeAffineMatrix(result.transform.scale, result.transform.rotate, result.transform.translate);

	// Node名を格納
	result.name = node->mName.C_Str();

	// 子供の数だけ確保
	result.children.resize(node->mNumChildren);

	// 再帰的に読んで階層構造を作る
	for (uint32_t childIndex = 0; childIndex < node->mNumChildren; ++childIndex) {
		result.children[childIndex] = ReadNode(node->mChildren[childIndex]);
	}

	return result;
}

Animation LoadAnimationFile(const std::string& directoryPath, const std::string& filename) {
	Animation animation; // 今回作るアニメーション
	Assimp::Importer importer;
	std::string filePath = directoryPath + "/" + filename;
	const aiScene* scene = importer.ReadFile(filePath.c_str(), 0);
	assert(scene); // ファイルが読めない
	// アニメーションが入っていないファイルは、尺 0 の空のアニメーションを返す
	if (scene->mNumAnimations == 0) {
		return animation;
	}
	aiAnimation* animationAssimp = scene->mAnimations[0]; // 最初のアニメーションだけ採用
	animation.duration = float(animationAssimp->mDuration / animationAssimp->mTicksPerSecond); // 時間の単位を秒に変換

	// assimpでは個々のNodeのAnimationをchannelと呼んでいるのでchannelを回してNodeAnimationの情報をとってくる
	for (uint32_t channelIndex = 0; channelIndex < animationAssimp->mNumChannels; ++channelIndex) {
		aiNodeAnim* nodeAnimationAssimp = animationAssimp->mChannels[channelIndex];
		NodeAnimation& nodeAnimation = animation.nodeAnimations[nodeAnimationAssimp->mNodeName.C_Str()];

		// Translate
		for (uint32_t keyIndex = 0; keyIndex < nodeAnimationAssimp->mNumPositionKeys; ++keyIndex) {
			aiVectorKey& keyAssimp = nodeAnimationAssimp->mPositionKeys[keyIndex];
			KeyframeVector3 keyframe;
			keyframe.time = float(keyAssimp.mTime / animationAssimp->mTicksPerSecond); // ここも秒に変換
			keyframe.value = { -keyAssimp.mValue.x, keyAssimp.mValue.y, keyAssimp.mValue.z }; // 右手->左手
			nodeAnimation.translate.keyframes.push_back(keyframe);
		}

		// Rotate
		for (uint32_t keyIndex = 0; keyIndex < nodeAnimationAssimp->mNumRotationKeys; ++keyIndex) {
			aiQuatKey& keyAssimp = nodeAnimationAssimp->mRotationKeys[keyIndex];
			KeyframeQuaternion keyframe;
			keyframe.time = float(keyAssimp.mTime / animationAssimp->mTicksPerSecond); // ここも秒に変換
			// RotateはQuaternionで、右手->左手に変換するために、yとzを反転させる必要がある
			keyframe.value = { keyAssimp.mValue.x, -keyAssimp.mValue.y, -keyAssimp.mValue.z, keyAssimp.mValue.w };
			nodeAnimation.rotate.keyframes.push_back(keyframe);
		}

		// Scale
		for (uint32_t keyIndex = 0; keyIndex < nodeAnimationAssimp->mNumScalingKeys; ++keyIndex) {
			aiVectorKey& keyAssimp = nodeAnimationAssimp->mScalingKeys[keyIndex];
			KeyframeVector3 keyframe;
			keyframe.time = float(keyAssimp.mTime / animationAssimp->mTicksPerSecond); // ここも秒に変換
			// Scaleはそのままで良い
			keyframe.value = { keyAssimp.mValue.x, keyAssimp.mValue.y, keyAssimp.mValue.z };
			nodeAnimation.scale.keyframes.push_back(keyframe);
		}
	}
	// 解析完了
	return animation;
}

Vector3 CalculateValue(const std::vector<KeyframeVector3>& keyframes, float time) {
    assert(!keyframes.empty()); // キーがないものはダメ
    if (keyframes.size() == 1 || time <= keyframes[0].time) {
        return keyframes[0].value;
    }

    for (size_t index = 0; index < keyframes.size() - 1; ++index) {
        size_t nextIndex = index + 1;
        if (keyframes[index].time <= time && time <= keyframes[nextIndex].time) {
            float t = (time - keyframes[index].time) / (keyframes[nextIndex].time - keyframes[index].time);
            return Lerp(keyframes[index].value, keyframes[nextIndex].value, t);
        }
    }
    return (*keyframes.rbegin()).value;
}

Quaternion CalculateValue(const std::vector<KeyframeQuaternion>& keyframes, float time) {
    assert(!keyframes.empty());
    if (keyframes.size() == 1 || time <= keyframes[0].time) {
        return keyframes[0].value;
    }

    for (size_t index = 0; index < keyframes.size() - 1; ++index) {
        size_t nextIndex = index + 1;
        if (keyframes[index].time <= time && time <= keyframes[nextIndex].time) {
            float t = (time - keyframes[index].time) / (keyframes[nextIndex].time - keyframes[index].time);
            return Slerp(keyframes[index].value, keyframes[nextIndex].value, t);
        }
    }
    return (*keyframes.rbegin()).value;
}
