#pragma once
#include "Common.h"
#include "ShaderProvider.h"
#include "CPipelineStateObject.h"
#include "CVertexInputDescriptor.h"
#include "TextureHandle.h"
#include "ShaderBindingBuilder.h"
#include "ShaderBindingSet.h"
#include "ShaderBindingSetHandle.h"
#include "CCommandList.h"
#include "IMeshInterface.h"
#include <functional>
#include <vector>
#include <SharedTools/header/uhash.h>

namespace graphics_backend
{
	constexpr uint32_t INVALID_ATTACHMENT_INDEX = 256;


	struct CAttachmentInfo
	{
		ETextureFormat format = ETextureFormat::E_R8G8B8A8_UNORM;
		EAttachmentLoadOp loadOp = EAttachmentLoadOp::eLoad;
		EAttachmentStoreOp storeOp = EAttachmentStoreOp::eStore;
		EAttachmentLoadOp stencilLoadOp = EAttachmentLoadOp::eDontCare;
		EAttachmentStoreOp stencilStoreOp = EAttachmentStoreOp::eDontCare;
		GraphicsClearValue clearValue = GraphicsClearValue::ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		EMultiSampleCount multiSampleCount = EMultiSampleCount::e1;

		bool operator==(CAttachmentInfo const& rhs) const
		{
			return format == rhs.format
				&& loadOp == rhs.loadOp
				&& storeOp == rhs.storeOp
				&& multiSampleCount == rhs.multiSampleCount
				&& stencilLoadOp == rhs.stencilLoadOp
				&& stencilStoreOp == rhs.stencilStoreOp
				&& clearValue == rhs.clearValue;
		}
	};

	struct CSubpassInfo
	{
		std::vector<uint32_t> colorAttachmentIDs{};
		uint32_t depthAttachmentID = INVALID_ATTACHMENT_INDEX;
		bool depthAttachmentReadOnly = false;
		std::vector<uint32_t> pixelInputAttachmentIDs{};
		std::vector<uint32_t> preserveAttachmentIDs{};

		bool operator==(CSubpassInfo const& rhs) const
		{
			return colorAttachmentIDs == rhs.colorAttachmentIDs
				&& pixelInputAttachmentIDs == rhs.pixelInputAttachmentIDs
				&& preserveAttachmentIDs == rhs.preserveAttachmentIDs
				&& depthAttachmentID == rhs.depthAttachmentID;
		}

		template <class HashAlgorithm>
		friend void hash_append(HashAlgorithm& h, CSubpassInfo const& subpassInfo) noexcept
		{
			hash_append(h, subpassInfo.colorAttachmentIDs);
			hash_append(h, subpassInfo.pixelInputAttachmentIDs);
			hash_append(h, subpassInfo.preserveAttachmentIDs);
			hash_append(h, subpassInfo.depthAttachmentID);
		}
	};

	struct CRenderPassInfo
	{
		std::vector<CAttachmentInfo> attachmentInfos{};
		std::vector<CSubpassInfo> subpassInfos{};

		bool operator==(CRenderPassInfo const& rhs) const
		{
			return attachmentInfos == rhs.attachmentInfos && subpassInfos == rhs.subpassInfos;
		}

		template <class HashAlgorithm>
		friend void hash_append(HashAlgorithm& h, CRenderPassInfo const& renderPassInfo) noexcept
		{
			hash_append(h, renderPassInfo.attachmentInfos);
			hash_append(h, renderPassInfo.subpassInfos);
		}
	};

	enum class ESubpassType
	{
		eSimpleDraw,
		eMeshInterface,
	};

	struct SimpleDrawcallSubpassData
	{
		CPipelineStateObject pipelineStateObject;
		CVertexInputDescriptor vertexInputDescriptor;
		GraphicsShaderSet shaderSet;
		ShaderBindingList shaderBindingList;
		std::function<void(CInlineCommandList&)> commandFunction;
	};

	struct DrawcallInterfaceSubpassData
	{
		ShaderBindingList shaderBindingList;
		IMeshInterface* meshInterface;
	};

	class CRenderpassBuilder
	{
	public:
		CRenderpassBuilder(std::vector<CAttachmentInfo> const& inAttachmentInfo)// : m_TextureHandles{ static_cast<uint32_t>(inAttachmentInfo.size()), INVALID_INDEX }
		{
			mRenderPassInfo.attachmentInfos = inAttachmentInfo;
			m_TextureHandles.resize(inAttachmentInfo.size());
			std::fill(m_TextureHandles.begin(), m_TextureHandles.end(), INVALID_INDEX);
		}

		CRenderpassBuilder& SetAttachmentTarget(uint32_t attachmentIndex, TextureHandle const& textureHandle)
		{
			m_TextureHandles[attachmentIndex] = textureHandle.GetHandleIndex();
			return *this;
		}

		CRenderpassBuilder& Subpass(CSubpassInfo const& inSubpassInfo
			, ShaderBindingList const& shaderBindingList
			, IMeshInterface* meshInterface)
		{
			mRenderPassInfo.subpassInfos.push_back(inSubpassInfo);
			m_SubpassData_MeshInterfaces.push_back(DrawcallInterfaceSubpassData{ 
				shaderBindingList
				, meshInterface });
			m_SubpassDataReferences.emplace_back(ESubpassType::eMeshInterface
				, static_cast<uint32_t>(m_SubpassData_MeshInterfaces.size() - 1));
			return *this;
		}

		CRenderpassBuilder& Subpass(CSubpassInfo const& inSubpassInfo
			, CPipelineStateObject const& pipelineStates
			, CVertexInputDescriptor const& vertexInputs
			, GraphicsShaderSet const& shaderSet
			, ShaderBindingList const& shaderBindingList
			, std::function<void(CInlineCommandList&)> commandFunction)
		{
			mRenderPassInfo.subpassInfos.push_back(inSubpassInfo);
			m_SubpassData_SimpleDraws.push_back(SimpleDrawcallSubpassData{
				pipelineStates
				, vertexInputs
				, shaderSet
				, shaderBindingList
				, commandFunction
				});
			m_SubpassDataReferences.emplace_back(ESubpassType::eSimpleDraw
				, static_cast<uint32_t>(m_SubpassData_SimpleDraws.size() - 1));
			return *this;
		}

		CRenderPassInfo const& GetRenderPassInfo() const
		{
			return mRenderPassInfo;
		}

		ESubpassType GetSubpassType(uint32_t subpassIndex) const
		{
			return m_SubpassDataReferences[subpassIndex].first;
		}

		SimpleDrawcallSubpassData const& GetSubpassData_SimpleDrawcall(uint32_t subpassIndex) const
		{
			CA_ASSERT(m_SubpassDataReferences[subpassIndex].first == ESubpassType::eSimpleDraw, "Wrong Subpass Type");
			return m_SubpassData_SimpleDraws[m_SubpassDataReferences[subpassIndex].second];
		}

		DrawcallInterfaceSubpassData const& GetSubpassData_MeshInterface(uint32_t subpassIndex) const
		{
			CA_ASSERT(m_SubpassDataReferences[subpassIndex].first == ESubpassType::eMeshInterface, "Wrong Subpass Type");
			return m_SubpassData_MeshInterfaces[m_SubpassDataReferences[subpassIndex].second];
		}

		std::vector<TIndex> const& GetAttachmentTextureHandles() const
		{
			return m_TextureHandles;
		}

	private:
		CRenderPassInfo mRenderPassInfo{};
		std::vector<TIndex> m_TextureHandles;
		std::vector<SimpleDrawcallSubpassData> m_SubpassData_SimpleDraws{};
		std::vector<DrawcallInterfaceSubpassData> m_SubpassData_MeshInterfaces{};
		std::vector<std::pair<ESubpassType, uint32_t>> m_SubpassDataReferences{};
	};
}

template<>
struct hash_utils::is_contiguously_hashable<graphics_backend::CAttachmentInfo> : public std::true_type {};