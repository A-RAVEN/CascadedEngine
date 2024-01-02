#pragma once
#include <GeneralResources/header/IResource.h>
#include <GeneralResources/header/ResourceImporter.h>
#include <GeneralResources/header/ResourceManagingSystem.h>
#include "TestShaderProvider.h"
#include <ShaderCompiler/header/Compiler.h>
#include <SharedTools/header/library_loader.h>
#include <ExternalLib/zpp_bits/zpp_bits.h>
#include <assimp/Importer.hpp>      // C++ importer interface
#include <assimp/scene.h>           // Output data structure
#include <assimp/postprocess.h>     // Post processing flags
#include <ExternalLib/glm/glm/mat4x4.hpp>
#include <ExternalLib/glm/glm/gtc/matrix_transform.hpp>
#include <ExternalLib/zpp_bits/zpp_bits.h>
#include <RenderInterface/header/CRenderBackend.h>

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

		constexpr static auto GetVertexInputDescs()
		{
			return std::vector{
				VertexAttribute{ 0, offsetof(CommonVertexData, pos), VertexInputFormat::eR32G32B32_SFloat }
				, VertexAttribute{ 1, offsetof(CommonVertexData, uv), VertexInputFormat::eR32G32_SFloat }
				, VertexAttribute{ 2, offsetof(CommonVertexData, normal), VertexInputFormat::eR32G32B32_SFloat }
				, VertexAttribute{ 3, offsetof(CommonVertexData, tangent), VertexInputFormat::eR32G32B32_SFloat }
				, VertexAttribute{ 4, offsetof(CommonVertexData, bitangent), VertexInputFormat::eR32G32B32_SFloat }
			};
		}
	};

	using namespace library_loader;
	class StaticMeshResource : public IResource
	{
	public:
		struct SubmeshInfo
		{
			int m_IndicesCount;
			int m_IndexArrayOffset;
			int m_VertexArrayOffset;
		};
		friend zpp::bits::access;
		using serialize = zpp::bits::members<3>;
		virtual void Serialzie(std::vector<std::byte>& out) override;
		virtual void Deserialzie(std::vector<std::byte>& in) override;
		constexpr uint32_t GetVertexCount() const { return m_Attributes.size(); }
		constexpr uint32_t GetIndicesCount() const { return m_Indices16.size(); }
		constexpr void const* GetVertexData() const { return m_Attributes.data(); }
		constexpr void const* GetIndicesData() const { return m_Indices16.data(); }
		std::vector<SubmeshInfo> const& GetSubmeshInfos() const { return m_SubmeshInfos; }
	private:
		friend class StaticMeshImporter;
		std::vector<CommonVertexData> m_Attributes;
		std::vector<uint16_t> m_Indices16;
		std::vector<SubmeshInfo> m_SubmeshInfos;
	};

	class StaticMeshImporter : public ResourceImporter<StaticMeshResource>
	{
	public:
		StaticMeshImporter();
		virtual std::string GetSourceFilePostfix() const override { return ".fbx"; }
		virtual std::string GetDestFilePostfix() const override { return ".mesh"; }
		virtual std::string GetTags() const override { return ""; }
		virtual void ImportResource(ResourceManagingSystem* resourceManager, std::string const& resourcePath, std::string const& outPath) override;
	private:
		Assimp::Importer m_Importer;
	};
}