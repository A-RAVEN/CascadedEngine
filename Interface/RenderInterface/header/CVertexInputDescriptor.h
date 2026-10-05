#pragma once
#include <CASTL/CAVector.h>
#include <CASTL/CATuple.h>
#include "Common.h"
#include <CodeGenMeta.h>

// 这个类型也是哈希键（`HashObj<InputAssemblyStates>` 出现在 D3D12 / Vulkan 的管线状态键里）。
CA_REFLECT(serializable)
struct InputAssemblyStates
{
	ETopology topology = ETopology::eTriangleList;
	auto operator<=>(const InputAssemblyStates&) const = default;
};

// 这两个类型要当哈希键用（`castl::unordered_map<cacore::NameHash, cacore::HashObj<VertexInputsDescriptor>>`
// 之类），所以必须走生成代码：新运行时的哈希对象对"没标注、也没有自定义特化"的类类型是硬拒绝，
// 而老驱动会退回结构化绑定接着算 —— 换驱动时它们就是过不去的宿主。
CA_REFLECT(serializable)
struct VertexAttribute
{
	uint32_t offset;
	VertexInputFormat format;
	cacore::NameHash semanticName;
	uint32_t sematicIndex;
	auto operator<=>(const VertexAttribute&) const = default;

	static VertexAttribute Create(uint32_t offset, VertexInputFormat format, cacore::NameHash const& semanticName, uint32_t sematicIndex = 0)
	{
		return { offset, format, semanticName, sematicIndex };
	}
};

CA_REFLECT(serializable)
struct VertexInputsDescriptor
{
	uint32_t stride = 0;
	bool perInstance = false;
	castl::vector<VertexAttribute> attributes;

	friend auto operator<=>(const VertexInputsDescriptor&, const VertexInputsDescriptor&) = default;

	static VertexInputsDescriptor Create(uint32_t stride, castl::vector<VertexAttribute> const& attributes, bool perInstance = false)
	{
		return { stride, perInstance, attributes };
	}
};

// 生成物：上面两个类型各自的反射 / 序列化 / 哈希特化。接在这里（类型定义之后）而不是让每个消费方自己
// 记一遍 —— `HashObj<T>` 与 `std::unordered_map<…, cacore::hash<T>>` 在**实例化**时就要看到这些特化，
// 少一个那一层就会退回"整块字节"或直接编不过（实测：D3D12 / Vulkan 的 GPUGraph 与 IMGUIContext 都在
// 这一层断过）。本模块的生成物目录是 PUBLIC include，所以消费方拿得到这几份头。
#include "VertexAttribute.Reflection.h"
#include "VertexAttribute.Serialization.h"
#include "VertexAttribute.Hash.h"
#include "VertexInputsDescriptor.Reflection.h"
#include "VertexInputsDescriptor.Serialization.h"
#include "VertexInputsDescriptor.Hash.h"
#include "InputAssemblyStates.Reflection.h"
#include "InputAssemblyStates.Serialization.h"
#include "InputAssemblyStates.Hash.h"
//
//class CVertexInputDescriptor
//{
//public:
//	InputAssemblyStates assemblyStates;
//
//	struct PrimitiveDesc
//	{
//		uint32_t stride;
//		castl::vector<VertexAttribute> attributes;
//		bool perInstance;
//	};
//
//	castl::vector<PrimitiveDesc> m_PrimitiveDescriptions;
//
//	inline void AddPrimitiveDescriptor(
//		uint32_t stride
//		, castl::vector<VertexAttribute> const& attributes
//		, bool perInstance = false
//	)
//	{
//		m_PrimitiveDescriptions.push_back(PrimitiveDesc{ stride, attributes, perInstance });
//	}
//
//	//inline void AddPrimitiveDescriptor(
//	//	castl::tuple<uint32_t, castl::vector<VertexAttribute>, bool> const& primitiveDescriptor
//	//)
//	//{
//	//	m_PrimitiveDescriptions.push_back(primitiveDescriptor);
//	//}
//
//	bool operator==(CVertexInputDescriptor const& rhs) const
//	{
//		return assemblyStates == rhs.assemblyStates
//			&& m_PrimitiveDescriptions == rhs.m_PrimitiveDescriptions;
//	}
//
//	template <class HashAlgorithm>
//	friend void hash_append(HashAlgorithm& h, CVertexInputDescriptor const& vertex_input_desc) noexcept
//	{
//		hash_append(h, vertex_input_desc.assemblyStates);
//		for (auto& tup : vertex_input_desc.m_PrimitiveDescriptions)
//		{
//			hash_append(h, castl::get<0>(tup));
//			hash_append(h, castl::get<1>(tup));
//			hash_append(h, castl::get<2>(tup));
//		}
//		hash_append(h, vertex_input_desc.m_PrimitiveDescriptions.size());
//	}
//};
