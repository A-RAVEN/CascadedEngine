#include <stb_image.h>
#include "StaticMeshResource.h"
#include "SerializationLog.h"
#include "TextureResource.h"
#include <filesystem>

namespace resource_management
{
	void StaticMeshResource::Serialzie(castl::vector<uint8_t>& data)
	{
		cacore::serialize(data, *this);
	}
	void StaticMeshResource::Deserialzie(castl::vector<uint8_t>& data)
	{
		cacore::deserializer<castl::vector<uint8_t>> deserializer(data);
		deserializer.deserialize(*this);
	}
	StaticMeshImporter::StaticMeshImporter()
	{

	}
	void StaticMeshImporter::ImportResource(ResourceManagingSystem* resourceManager, castl::string const& resourcePath, castl::string const& outPath)
	{
		const aiScene* scene = m_Importer.ReadFile(resourcePath.c_str(),
			aiProcess_GenNormals |
			aiProcess_CalcTangentSpace |
			aiProcess_Triangulate |
			aiProcess_JoinIdenticalVertices |
			aiProcess_FlipWindingOrder |
			aiProcess_FlipUVs |
			aiProcess_SortByPType);

		if (scene == nullptr) {
			return;
		}

		if (scene->HasMeshes())
		{
			StaticMeshResource* meshResource = resourceManager->AllocSubResource<StaticMeshResource>(outPath, "mesh.scene");
			uint32_t vertexOffset = 0;
			uint32_t indexOffset = 0;
			for (int i = 0; i < scene->mNumMeshes; ++i)
			{
				auto itrMesh = scene->mMeshes[i];
				if (itrMesh->HasPositions() && itrMesh->HasTangentsAndBitangents() && itrMesh->HasNormals() && itrMesh->HasFaces())
				{
					meshResource->m_Attributes.resize(vertexOffset + itrMesh->mNumVertices);
					for (int iv = 0; iv < itrMesh->mNumVertices; ++iv)
					{
						meshResource->m_Attributes[vertexOffset + iv].pos = glm::vec3(itrMesh->mVertices[iv].x, itrMesh->mVertices[iv].y, itrMesh->mVertices[iv].z);
						meshResource->m_Attributes[vertexOffset + iv].normal = glm::vec3(itrMesh->mNormals[iv].x, itrMesh->mNormals[iv].y, itrMesh->mNormals[iv].z);
						meshResource->m_Attributes[vertexOffset + iv].tangent = glm::vec3(itrMesh->mTangents[iv].x, itrMesh->mTangents[iv].y, itrMesh->mTangents[iv].z);
						meshResource->m_Attributes[vertexOffset + iv].bitangent = glm::vec3(itrMesh->mBitangents[iv].x, itrMesh->mBitangents[iv].y, itrMesh->mBitangents[iv].z);
						if (itrMesh->HasTextureCoords(0))
						{
							meshResource->m_Attributes[vertexOffset + iv].uv = glm::vec2(itrMesh->mTextureCoords[0][iv].x, itrMesh->mTextureCoords[0][iv].y);
						}
						else
						{
							meshResource->m_Attributes[vertexOffset + iv].uv = glm::vec2(0.0f, 0.0f);
						}
					}
					meshResource->m_Indices16.resize(indexOffset + itrMesh->mNumFaces * 3);
					for (int fi = 0; fi < itrMesh->mNumFaces; ++fi)
					{
						CA_ASSERT(itrMesh->mFaces[fi].mNumIndices == 3, "Faces Should Be Triangles");
						for (int ii = 0; ii < 3; ++ii)
						{
							meshResource->m_Indices16[indexOffset + fi * 3 + ii] = itrMesh->mFaces[fi].mIndices[ii];
						};
					}
					meshResource->m_SubmeshInfos.emplace_back(itrMesh->mMaterialIndex, itrMesh->mNumFaces * 3, indexOffset, vertexOffset);

					indexOffset += itrMesh->mNumFaces * 3;
					vertexOffset += itrMesh->mNumVertices;
				}
			}

			if (scene->mNumTextures > 0)
			{
				for (int textureID = 0; textureID < scene->mNumTextures; ++textureID)
				{
					aiTexture* pTexture = scene->mTextures[textureID];
					std::filesystem::path texturePath = pTexture->mFilename.C_Str();
					texturePath = texturePath.replace_extension(".texture");

					if (pTexture->mHeight == 0)
					{
						TextureResource* textureResource = resourceManager->AllocSubResource<TextureResource>(outPath, castl::to_ca(texturePath.string()));
						int w, h, channel_num;
						stbi_info_from_memory(reinterpret_cast<stbi_uc*>(pTexture->pcData), pTexture->mWidth, &w, &h, &channel_num);
						int desiredChannel = channel_num;
						ETextureFormat format;
						switch (channel_num)
						{
						case 1:
							format = ETextureFormat::E_R8_UNORM;
							break;
						case 2:
							format = ETextureFormat::E_R8G8_UNORM;
							break;
						case 3:
						case 4:
							format = ETextureFormat::E_R8G8B8A8_UNORM;
							desiredChannel = 4;
							break;
						}
						auto data = stbi_load_from_memory(reinterpret_cast<stbi_uc*>(pTexture->pcData), pTexture->mWidth, &w, &h, &channel_num, desiredChannel);
						textureResource->SetData(data, w * h * sizeof(uint8_t) * desiredChannel);
						textureResource->SetMetaData(w, h, 1, 1, format, ETextureType::e2D);
						stbi_image_free(data);
					}
				}
			}

			castl::deque<aiNode*> nodeQueue;

			nodeQueue.push_back(scene->mRootNode);
			while (!nodeQueue.empty())
			{
				aiNode* currentNode = nodeQueue.front();
				nodeQueue.pop_front();
				if (currentNode->mNumChildren > 0)
				{
					for (int childID = 0; childID < currentNode->mNumChildren; ++childID)
					{
						nodeQueue.push_back(currentNode->mChildren[childID]);
					}
				}
				if (currentNode->mNumMeshes > 0)
				{
					aiMatrix4x4 nodeTrans = currentNode->mTransformation;
					auto itrParentNode = currentNode->mParent;
					while (itrParentNode != nullptr)
					{
						nodeTrans = itrParentNode->mTransformation * nodeTrans;
						itrParentNode = itrParentNode->mParent;
					}

					glm::mat4 meshMat {
						nodeTrans.a1, nodeTrans.a2, nodeTrans.a3, nodeTrans.a4,
							nodeTrans.b1, nodeTrans.b2, nodeTrans.b3, nodeTrans.b4,
							nodeTrans.c1, nodeTrans.c2, nodeTrans.c3, nodeTrans.c4,
							nodeTrans.d1, nodeTrans.d2, nodeTrans.d3, nodeTrans.d4,
					};
					meshMat = glm::transpose(meshMat);

					for (int meshID = 0; meshID < currentNode->mNumMeshes; ++meshID)
					{
						StaticMeshResource::InstanceInfo newInst{};
						newInst.m_InstanceTransform = meshMat;
						newInst.m_SubmeshID = currentNode->mMeshes[meshID];
						meshResource->m_Instance.push_back(newInst);
					}
				}
			}
		}
	}
}

