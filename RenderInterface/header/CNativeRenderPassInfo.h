#pragma once
#include <CASTL/CAFunctional.h>
#include <CASTL/CAVector.h>
#include "Common.h"
#include "ShaderProvider.h"
#include "CPipelineStateObject.h"
#include "CVertexInputDescriptor.h"
#include "ShaderBindingBuilder.h"
#include "ShaderBindingSet.h"
#include "ShaderBindingSetHandle.h"
#include "CCommandList.h"
#include "IMeshInterface.h"

namespace graphics_backend
{
	struct CAttachmentInfo
	{
		ETextureFormat format = ETextureFormat::E_R8G8B8A8_UNORM;
		EAttachmentLoadOp loadOp = EAttachmentLoadOp::eLoad;
		EAttachmentStoreOp storeOp = EAttachmentStoreOp::eStore;
		EAttachmentLoadOp stencilLoadOp = EAttachmentLoadOp::eDontCare;
		EAttachmentStoreOp stencilStoreOp = EAttachmentStoreOp::eDontCare;
		EMultiSampleCount multiSampleCount = EMultiSampleCount::e1;

		auto operator<=>(const CAttachmentInfo&) const = default;

		constexpr static CAttachmentInfo Create(ETextureFormat format
			, EAttachmentLoadOp loadOp = EAttachmentLoadOp::eLoad,
			, EAttachmentStoreOp storeOp = EAttachmentStoreOp::eStore,
			, EMultiSampleCount multiSampleCount = EMultiSampleCount::e1)
		{
			CAttachmentInfo result{};
			result.format = format;
			result.loadOp = loadOp;
			result.storeOp = storeOp;
			result.stencilLoadOp = loadOp;
			result.stencilStoreOp = storeOp;
			result.multiSampleCount = multiSampleCount;
			return result;
		}

		constexpr static CAttachmentInfo CreateDS(ETextureFormat format
			, EAttachmentLoadOp loadOp = EAttachmentLoadOp::eLoad,
			, EAttachmentStoreOp storeOp = EAttachmentStoreOp::eStore,
			, EAttachmentLoadOp stencilLoadOp = EAttachmentLoadOp::eLoad,
			, EAttachmentStoreOp stencilStoreOp = EAttachmentStoreOp::eStore,
			, EMultiSampleCount multiSampleCount = EMultiSampleCount::e1)
		{
			CAttachmentInfo result{};
			result.format = format;
			result.loadOp = loadOp;
			result.storeOp = storeOp;
			result.stencilLoadOp = stencilLoadOp;
			result.stencilStoreOp = stencilStoreOp;
			result.multiSampleCount = multiSampleCount;
			return result;
		}
	};

	struct CSubpassInfo
	{
		castl::vector<uint32_t> colorAttachmentIDs;
		castl::vector<uint32_t> pixelInputAttachmentIDs;
		castl::vector<uint32_t> preserveAttachmentIDs;
		uint32_t depthAttachmentID;
		bool depthAttachmentReadOnly;

		auto operator<=>(const CSubpassInfo&) const = default;
	};

	struct CRenderPassInfo
	{
		castl::vector<CAttachmentInfo> attachmentInfos{};
		castl::vector<CSubpassInfo> subpassInfos{};

		auto operator<=>(const CRenderPassInfo&) const = default;
	};

	enum class ESubpassType
	{
		eSimpleDraw,
		eMeshInterface,
		eBatchDrawInterface,
	};

	struct SimpleDrawcallSubpassData
	{
		CPipelineStateObject pipelineStateObject;
		CVertexInputDescriptor vertexInputDescriptor;
		GraphicsShaderSet shaderSet;
		ShaderBindingList shaderBindingList;
		castl::function<void(CInlineCommandList&)> commandFunction;
	};

	struct DrawcallInterfaceSubpassData
	{
		ShaderBindingList shaderBindingList;
		IMeshInterface* meshInterface;
	};

	struct BatchDrawInterfaceSubpassData
	{
		ShaderBindingList shaderBindingList;
		IDrawBatchInterface* batchInterface;
	};

	class CRenderpassBuilder
	{
	public:
		CRenderpassBuilder(castl::vector<CAttachmentInfo> const& inAttachmentInfo)
		{
			mRenderPassInfo.attachmentInfos = inAttachmentInfo;
			m_TextureHandles.resize(inAttachmentInfo.size());
			castl::fill(m_TextureHandles.begin(), m_TextureHandles.end(), INVALID_INDEX);
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
			, ShaderBindingList const& shaderBindingList
			, IDrawBatchInterface* batchInterface)
		{
			mRenderPassInfo.subpassInfos.push_back(inSubpassInfo);
			m_SubpassData_BatchDrawInterfaces.push_back(BatchDrawInterfaceSubpassData{
				shaderBindingList
				, batchInterface });
			m_SubpassDataReferences.emplace_back(ESubpassType::eBatchDrawInterface
				, static_cast<uint32_t>(m_SubpassData_BatchDrawInterfaces.size() - 1));
			return *this;
		}

		CRenderpassBuilder& Subpass(CSubpassInfo const& inSubpassInfo
			, CPipelineStateObject const& pipelineStates
			, CVertexInputDescriptor const& vertexInputs
			, GraphicsShaderSet const& shaderSet
			, ShaderBindingList const& shaderBindingList
			, castl::function<void(CInlineCommandList&)> commandFunction)
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

		uint32_t GetSubpassDataIndex(uint32_t subpassIndex) const
		{
			return m_SubpassDataReferences[subpassIndex].second;
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

		BatchDrawInterfaceSubpassData const& GetSubpassData_BatchDrawInterface(uint32_t subpassIndex) const
		{
			CA_ASSERT(m_SubpassDataReferences[subpassIndex].first == ESubpassType::eBatchDrawInterface, "Wrong Subpass Type");
			return m_SubpassData_BatchDrawInterfaces[m_SubpassDataReferences[subpassIndex].second];
		}

		castl::vector<TIndex> const& GetAttachmentTextureHandles() const
		{
			return m_TextureHandles;
		}

	private:
		CRenderPassInfo mRenderPassInfo{};
		castl::vector<TIndex> m_TextureHandles;
		castl::vector<castl::pair<ESubpassType, uint32_t>> m_SubpassDataReferences{};
		castl::vector<SimpleDrawcallSubpassData> m_SubpassData_SimpleDraws{};
		castl::vector<DrawcallInterfaceSubpassData> m_SubpassData_MeshInterfaces{};
		castl::vector<BatchDrawInterfaceSubpassData> m_SubpassData_BatchDrawInterfaces{};
	};
}

template<>
struct hash_utils::is_contiguously_hashable<graphics_backend::CAttachmentInfo> : public castl::true_type {};