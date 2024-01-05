#include "StaticMeshResource.h"
#include "SerializationLog.h"

namespace resource_management
{
	void StaticMeshResource::Serialzie(std::vector<std::byte>& data)
	{
		zpp::bits::out out(data);
		auto result = out(*this);
		if (failure(result)) {
			LogZPPError("serialize failed", result);
		}
	}
	void StaticMeshResource::Deserialzie(std::vector<std::byte>& data)
	{
		zpp::bits::in in(data);
		auto result = in(*this);
		if (failure(result)) {
			LogZPPError("deserialize failed", result);
		}
	}
	StaticMeshImporter::StaticMeshImporter()
	{

	}
	void StaticMeshImporter::ImportResource(ResourceManagingSystem* resourceManager, std::string const& resourcePath, std::string const& outPath)
	{
		const aiScene* scene = m_Importer.ReadFile(resourcePath,
			aiProcess_GenNormals |
			aiProcess_CalcTangentSpace |
			aiProcess_Triangulate |
			aiProcess_JoinIdenticalVertices |
			aiProcess_FlipWindingOrder |
			aiProcess_SortByPType);

		if (scene == nullptr) {
			return;
		}

		if (scene->HasMeshes())
		{
			StaticMeshResource* resource = resourceManager->AllocResource<StaticMeshResource>(outPath);
			uint32_t vertexOffset = 0;
			uint32_t indexOffset = 0;
			for (int i = 0; i < scene->mNumMeshes; ++i)
			{
				auto itrMesh = scene->mMeshes[i];
				if (itrMesh->HasPositions() && itrMesh->HasTangentsAndBitangents() && itrMesh->HasNormals() && itrMesh->HasFaces())
				{
					resource->m_Attributes.resize(vertexOffset + itrMesh->mNumVertices);
					for (int iv = 0; iv < itrMesh->mNumVertices; ++iv)
					{
						resource->m_Attributes[vertexOffset + iv].pos = glm::vec3(itrMesh->mVertices[iv].x, itrMesh->mVertices[iv].y, itrMesh->mVertices[iv].z);
						resource->m_Attributes[vertexOffset + iv].normal = glm::vec3(itrMesh->mNormals[iv].x, itrMesh->mNormals[iv].y, itrMesh->mNormals[iv].z);
						resource->m_Attributes[vertexOffset + iv].tangent = glm::vec3(itrMesh->mTangents[iv].x, itrMesh->mTangents[iv].y, itrMesh->mTangents[iv].z);
						resource->m_Attributes[vertexOffset + iv].bitangent = glm::vec3(itrMesh->mBitangents[iv].x, itrMesh->mBitangents[iv].y, itrMesh->mBitangents[iv].z);
						if (itrMesh->HasTextureCoords(0))
						{
							resource->m_Attributes[vertexOffset + iv].uv = glm::vec2(itrMesh->mTextureCoords[0][iv].x, itrMesh->mTextureCoords[0][iv].y);
						}
						else
						{
							resource->m_Attributes[vertexOffset + iv].uv = glm::vec2(0.0f, 0.0f);
						}
					}
					resource->m_Indices16.resize(indexOffset + itrMesh->mNumFaces * 3);
					for (int fi = 0; fi < itrMesh->mNumFaces; ++fi)
					{
						CA_ASSERT(itrMesh->mFaces[fi].mNumIndices == 3, "Faces Should Be Triangles");
						for (int ii = 0; ii < 3; ++ii)
						{
							resource->m_Indices16[indexOffset + fi * 3 + ii] = itrMesh->mFaces[fi].mIndices[ii];
						};
					}
					resource->m_SubmeshInfos.emplace_back(itrMesh->mNumFaces * 3, indexOffset, vertexOffset);

					indexOffset += itrMesh->mNumFaces * 3;
					vertexOffset += itrMesh->mNumVertices;
				}
			}
		}
	}
}

