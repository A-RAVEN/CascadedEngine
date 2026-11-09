#pragma once
// 绑定标注宏（CA_BIND / CA_BIND_INSTANTIATE）：这个头自己也进绑定面（2.2）。
#include <CodeGenMeta.h>
#include "ShaderResourceHandle.h"
#include "ShaderBindingBuilder.h"
#include "TextureSampler.h"
#include <CASTL/CAVector.h>
#include <CASTL/CAUnorderedMap.h>
//#include <CASTL/CAPair.h>

namespace graphics_backend
{
	// 12 字节三分量浮点值（csharp-native-direct-binding 2.2 / design D9）：着色器 uniform 里的
	// `float3` 字段就是 12 字节，`SetValue<T>` 需要的具体形态由这个公开值类型给出 —— 只是新增一个
	// **值类型**，不改任何既有签名、不新增任何原生方法。
	struct float3
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};

	//Interface for setting shader arguments
	CA_BIND()
	class ShaderStruct
	{
	protected:
		virtual void SetValueInternal(cacore::NameHash const& name
			, void const* pValue
			, uint32_t sizeInBytes
			, uint32_t elementIndex) = 0;
		
		virtual void SetImageInternal(cacore::NameHash const& name
			, ImageHandle const& imageHandle, GPUTextureView const& view
			, uint32_t elementIndex) = 0;

		virtual void SetBufferInternal(cacore::NameHash const& name
			, BufferHandle const& bufferHandle
			, uint32_t elementIndex) = 0;

		virtual void SetSamplerInternal(cacore::NameHash const& name
			, TextureSamplerDescriptor const& samplerDesc
			, uint32_t elementIndex) = 0;

		virtual void SetStructInternal(cacore::NameHash const& name
			, castl::shared_ptr<ShaderStruct> const& subStruct
			, uint32_t elementIndex) = 0;


	public:
		template<typename T>
		ShaderStruct& SetValue(cacore::NameHash const& name, T const& value, uint32_t elementIndex = 0)
			requires (!std::is_same_v<T, ImageHandle> && !std::is_same_v<T, BufferHandle> && !std::is_same_v<T, TextureSamplerDescriptor>)
		{
			static_assert(!std::is_same_v<T, ImageHandle>);
			static_assert(!std::is_same_v<T, BufferHandle>);
			static_assert(!std::is_same_v<T, TextureSamplerDescriptor>);
			SetValueInternal(name, &value, sizeof(T), elementIndex);
			return *this;
		}

		ShaderStruct& SetImage(cacore::NameHash const& name
			, ImageHandle const& imageHandle, GPUTextureView const& view
			, uint32_t elementIndex = 0)
		{
			SetImageInternal(name, imageHandle, view, elementIndex);
			return *this;
		}

		inline ShaderStruct& SetImage(cacore::NameHash const& name
			, castl::shared_ptr<GPUTexture> const& pImage
			, uint32_t elementIndex = 0)
		{
			return SetImage(name, pImage, GPUTextureView::CreateDefaultForSampling(pImage->GetDescriptor().format), elementIndex);
		}

		ShaderStruct& SetBuffer(cacore::NameHash const& name
			, BufferHandle const& bufferHandle
			, uint32_t elementIndex = 0)
		{
			SetBufferInternal(name, bufferHandle, elementIndex);
			return *this;
		}

		ShaderStruct& SetSampler(cacore::NameHash const& name
			, TextureSamplerDescriptor const& samplerDesc
			, uint32_t elementIndex = 0)
		{
			SetSamplerInternal(name, samplerDesc, elementIndex);
			return *this;
		}

		ShaderStruct& SetStruct(cacore::NameHash const& name
			, castl::shared_ptr<ShaderStruct> const& subStruct
			, uint32_t elementIndex = 0)
		{
			SetStructInternal(name, subStruct, elementIndex);
			return *this;
		}

		virtual cacore::NameHash const& GetStructTypeName() const = 0;
	};
	// 模板参写的显式实例化声明（design D9）：只绑 `float3`（12 字节着色器 float3 字段）与 `float`
	// （标量字段）两个形态；其它形态不进任何表面。C++ 侧不新增函数、不改签名。
	CA_BIND_INSTANTIATE(SetValue, float3, float)
	// 返回位置的包装值（`NameHash const&`）本批只定义入参方向 ⇒ 显式排除（诊断用，脚本不需要）。
	CA_BIND_SKIP(GetStructTypeName, "本轮未实现：返回位置的包装值（NameHash const&）只定义入参方向")
}
