#pragma once
#include "GPUTexture.h"
#include "GPUBuffer.h"
#include "WindowHandle.h"
#include <HashKeys.h>
#include <CodeGenHashFwd.h>
#include <CodeGenMeta.h>
#include <CASTL/CAMutex.h>

namespace graphics_backend
{
	struct ThreadID
	{
	public:
		static uint32_t Get();
	};

	struct ThreadLocalID
	{
	public:
		static uint32_t Get();
	};


	// 句柄的键：它本身是**值语义**（`NameHash` + 两个 uint32），没有指针成员，所以它走生成表 ——
	// `ResourceHandleKey = HashObj<ResourceHandleKeyData, FullCompare>` 要当哈希键用，换驱动之后
	// 必须自己有生成特化（新驱动对没标注、也没自定义特化的类类型是硬拒绝）。
	CA_REFLECT(serializable)
	struct ResourceHandleKeyData
	{
		cacore::NameHash name;
		uint32_t uniqueID;
		uint32_t threadID;
		auto operator<=>(const ResourceHandleKeyData&) const = default;
		static ResourceHandleKeyData Default() { return { {}, std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max() }; }
		static ResourceHandleKeyData Shared(cacore::NameHash const& name){ return { name,  std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max() }; }
		static ResourceHandleKeyData Unique(cacore::NameHash const& name){ return { name, ThreadLocalID::Get(), ThreadID::Get() }; }
		static ResourceHandleKeyData Create(cacore::NameHash const& name, bool unique){
			return unique ? Unique(name) : Shared(name);
		}
		//static ResourceHandleKeyData Create(cacore::NameHash const& name, uint32_t uniqueID) { return { name, uniqueID }; }
	};

	using ResourceHandleKey = cacore::HashObj<ResourceHandleKeyData, cacore::EHashObjCompareMode::FullCompare>;

	class ImageHandle
	{
	public:
		enum class ImageType
		{
			Internal,
			External,
			Backbuffer,
			Invalid,
		};
		ImageHandle()
			: m_Key(ResourceHandleKeyData::Default())
			, m_ExternalManagedTexture(nullptr)
			, m_Backbuffer(nullptr)
			, m_Type(ImageType::Invalid)
		{
		}
		ImageHandle(ImageHandle const& other)
			: m_Key(other.m_Key)
			, m_ExternalManagedTexture(other.m_ExternalManagedTexture)
			, m_Backbuffer(other.m_Backbuffer)
			, m_Type(other.m_Type)
		{
		}
		ImageHandle(cacore::NameHash const& name, bool unique = false)
			: m_Key(ResourceHandleKeyData::Create(name, unique))
			, m_ExternalManagedTexture(nullptr)
			, m_Backbuffer(nullptr)
			, m_Type(ImageType::Internal)
		{
		}
		ImageHandle(castl::shared_ptr<GPUTexture> const& texture)
			: m_Key(ResourceHandleKeyData::Default())
			, m_ExternalManagedTexture(texture)
			, m_Backbuffer(nullptr)
			, m_Type(ImageType::External)
		{
		}
		ImageHandle(castl::shared_ptr<WindowHandle> const& window)
			: m_Key(ResourceHandleKeyData::Default())
			, m_ExternalManagedTexture(nullptr)
			, m_Backbuffer(window)
			, m_Type(ImageType::Backbuffer)
		{
		}
		ImageHandle(ResourceHandleKeyData const& key)
			: m_Key(key)
			, m_ExternalManagedTexture(nullptr)
			, m_Backbuffer(nullptr)
			, m_Type(ImageType::Internal)
		{
		}
		bool IsIntternal() const { return m_Type == ImageType::Internal; }
		bool IsValid() const { return m_Type != ImageType::Invalid; }
		ImageType GetType() const { return m_Type; }
		castl::string_view const& GetName() const { return m_Key.Get().name.Get(); }
		ResourceHandleKey const& GetKey() const { return m_Key; }
		castl::shared_ptr<GPUTexture> GetExternalManagedTexture() const { return m_ExternalManagedTexture; }
		castl::shared_ptr<WindowHandle> GetWindowHandle() const { return m_Backbuffer; }
		template<typename TWindow>
		TWindow* GetWindowPtr() const
		{
			return static_cast<TWindow*>(GetWindowHandle().get());
		}
		template<typename TTexture>
		TTexture* GetTexturePtr() const
		{
			return static_cast<TTexture*>(GetExternalManagedTexture().get());
		}
		auto operator<=>(const ImageHandle&) const = default;
	private:
		ResourceHandleKey m_Key;
		castl::shared_ptr<GPUTexture> m_ExternalManagedTexture;
		castl::shared_ptr<WindowHandle> m_Backbuffer;
		ImageType m_Type;

	};

	class BufferHandle
	{
	public:
		enum class BufferType
		{
			Internal,
			External,
			Invalid,
		};
		BufferHandle()
			: m_Key(ResourceHandleKeyData::Default())
			, m_ExternalManagedBuffer(nullptr)
			, m_Type(BufferType::Invalid)
		{
		}
		BufferHandle(BufferHandle const& other)
			: m_Key(other.m_Key)
			, m_ExternalManagedBuffer(other.m_ExternalManagedBuffer)
			, m_Type(other.m_Type)
		{
		}
		BufferHandle(cacore::NameHash const& name, bool unique = false)
			: m_Key(ResourceHandleKeyData::Create(name, unique))
			, m_ExternalManagedBuffer(nullptr)
			, m_Type(BufferType::Internal)
		{
		}
		BufferHandle(castl::shared_ptr<GPUBuffer> const& buffer)
			: m_Key(ResourceHandleKeyData::Default())
			, m_ExternalManagedBuffer(buffer)
			, m_Type(BufferType::External)
		{
		}
		BufferHandle(ResourceHandleKeyData const& key)
			: m_Key(key)
			, m_ExternalManagedBuffer(nullptr)
			, m_Type(BufferType::Internal)
		{
		}
		bool IsIntternal() const { return m_Type == BufferType::Internal; }
		bool IsValid() const { return m_Type != BufferType::Invalid; }
		BufferType GetType() const { return m_Type; }
		castl::string_view const& GetName() const { return m_Key.Get().name.Get(); }
		ResourceHandleKey const& GetKey() const { return m_Key; }
		castl::shared_ptr<GPUBuffer> GetExternalManagedBuffer() const { return m_ExternalManagedBuffer; }
		template<typename TBuffer>
		TBuffer* GetBufferPtr() const
		{
			return static_cast<TBuffer*>(GetExternalManagedBuffer().get());
		}
		auto operator<=>(const BufferHandle&) const = default;
	private:
		ResourceHandleKey m_Key;
		castl::shared_ptr<GPUBuffer> m_ExternalManagedBuffer;
		BufferType m_Type;

	};
}

// 句柄键的生成物（反射 / 序列化 / 哈希）：消费方（本模块的 `Rendering` 与各后端）在实例化
// `HashObj<ResourceHandleKeyData>` 与 `std::unordered_map<…, cacore::hash<…>>` 时就要看到它。
#include "ResourceHandleKeyData.Reflection.h"
#include "ResourceHandleKeyData.Serialization.h"
#include "ResourceHandleKeyData.Hash.h"

// 这两个句柄是**哈希键**（`std::unordered_map<BufferHandle, …>` 之类），而它们**不能进生成表**：
// 两个类各有一个 `castl::shared_ptr` 成员，那是工具明令拒绝的形状（托管指针成员，退出码 15），
// 所以它们没有 `CA_REFLECT`、也不该有 —— 要落盘就得先按 7.2 改结构（那是另一条任务）。
//
// 但"能不能当哈希键"与"能不能落盘"是两件事：这里按设计给的第二条路手写一份哈希特化
// （"给它加标记生成特化，**或者**给它写一个 custom_hash_trait 特化"）。算的口径与老驱动那份
// 描述符一致：键 + 两个指针成员的**地址** + 类型；不再经过老反射，所以文件末尾那两行老宏
// 可以删掉 —— 那两行正是 CodeGen 解析这个头时的 12 条错误级诊断的来源。
namespace cacore::codegen
{
	template<>
	struct custom_hash_trait<graphics_backend::ImageHandle>
	{
		static void hash(graphics_backend::ImageHandle const& obj, auto& hasher)
		{
			hasher.hash(obj.GetKey());
			hasher.hash(obj.GetExternalManagedTexture().get());
			hasher.hash(obj.GetWindowHandle().get());
			hasher.hash(obj.GetType());
		}
	};

	template<>
	struct custom_hash_trait<graphics_backend::BufferHandle>
	{
		static void hash(graphics_backend::BufferHandle const& obj, auto& hasher)
		{
			hasher.hash(obj.GetKey());
			hasher.hash(obj.GetExternalManagedBuffer().get());
			hasher.hash(obj.GetType());
		}
	};
}
