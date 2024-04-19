#pragma once
#include <CAResource/IResource.h>
#include <CAResource/ResourceImporter.h>
#include <CAResource/ResourceManagingSystem.h>
#include <Compiler.h>
#include <library_loader.h>
#include <zpp_bits.h>
#include <assimp/Importer.hpp>      // C++ importer interface
#include <assimp/scene.h>           // Output data structure
#include <assimp/postprocess.h>     // Post processing flags
#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <CRenderBackend.h>

namespace glm
{
	constexpr auto serialize(auto& archive, glm::vec3& invec)
	{
		return archive(invec.x, invec.y, invec.z);
	}

	constexpr auto serialize(auto& archive, glm::vec2& invec)
	{
		return archive(invec.x, invec.y);
	}

	constexpr auto serialize(auto& archive, glm::vec4& invec)
	{
		return archive(invec.x, invec.y, invec.z, invec.w);
	}


	constexpr auto serialize(auto& archive, glm::mat4& inMat)
	{
		return archive(inMat[0], inMat[1], inMat[2], inMat[3]);
	}
}

namespace resource_management
{
	struct CommonVertexData
	{
		glm::vec3 pos;
		glm::vec2 uv;
		glm::vec3 normal;
		glm::vec3 tangent;
		glm::vec3 bitangent;

		static auto GetVertexInputDescs(uint32_t baseOffset)
		{
			return castl::vector{
				VertexAttribute{ baseOffset, offsetof(CommonVertexData, pos), VertexInputFormat::eR32G32B32_SFloat }
				, VertexAttribute{ baseOffset + 1, offsetof(CommonVertexData, uv), VertexInputFormat::eR32G32_SFloat }
				, VertexAttribute{ baseOffset + 2, offsetof(CommonVertexData, normal), VertexInputFormat::eR32G32B32_SFloat }
				, VertexAttribute{ baseOffset + 3, offsetof(CommonVertexData, tangent), VertexInputFormat::eR32G32B32_SFloat }
				, VertexAttribute{ baseOffset + 4, offsetof(CommonVertexData, bitangent), VertexInputFormat::eR32G32B32_SFloat }
			};
		}
	};

	using namespace library_loader;
	class StaticMeshResource : public IResource
	{
	public:
		struct SubmeshInfo
		{
			int m_MaterialID;
			int m_IndicesCount;
			int m_IndexArrayOffset;
			int m_VertexArrayOffset;
		};

		struct InstanceInfo
		{
			uint32_t m_SubmeshID;
			glm::mat4 m_InstanceTransform;
		};
		friend zpp::bits::access;
		using serialize = zpp::bits::members<4>;
		virtual void Serialzie(castl::vector<uint8_t>& out) override;
		virtual void Deserialzie(castl::vector<uint8_t>& in) override;
		uint32_t GetVertexCount() const { return m_Attributes.size(); }
		uint32_t GetIndicesCount() const { return m_Indices16.size(); }
		void const* GetVertexData() const { return m_Attributes.data(); }
		void const* GetIndicesData() const { return m_Indices16.data(); }
		std::vector<SubmeshInfo> const& GetSubmeshInfos() const { return m_SubmeshInfos; }
		std::vector<InstanceInfo> const& GetInstanceInfos() const { return m_Instance; }
	private:
		friend class StaticMeshImporter;
		std::vector<CommonVertexData> m_Attributes;
		std::vector<uint16_t> m_Indices16;
		std::vector<SubmeshInfo> m_SubmeshInfos;
		std::vector<InstanceInfo> m_Instance;
	};

	class StaticMeshImporter : public ResourceImporter<StaticMeshResource>
	{
	public:
		StaticMeshImporter();
		virtual castl::string GetSourceFilePostfix() const override { return ".fbx"; }
		virtual castl::string GetDestFilePostfix() const override { return ""; }
		virtual castl::string GetTags() const override { return ""; }
		virtual void ImportResource(ResourceManagingSystem* resourceManager, castl::string const& resourcePath, castl::string const& outPath) override;
	private:
		Assimp::Importer m_Importer;
	};
}