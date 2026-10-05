#pragma once
#include <CASTL/CAVector.h>
#include <CodeGenSerialization.h>
#include <IOManager/IOManager.h>
#include <cstddef>
#include <cstdint>

namespace resource_management
{
	class IResource
	{
	public:
		virtual ~IResource() {}
		virtual void Serialize(ca_io::WBatch* inWriter) = 0;
		virtual void Deserialize(ca_io::IOBatch* inReader) = 0;
	};

	// 资源的读写走 CodeGen 生成的代码：
	//   * 写：生成代码写进一块缓冲，然后一次性写进批 —— 批这一侧没有"总长度"，所以长度在自己这边量；
	//     落盘字节与老实现逐字节相同（生成代码的字节契约就是照老协议定的）。
	//   * 读：直接从批里流式读（`IOBatch` 问不到剩余长度，所以生成代码在这里用流式读对象）。
	// 这条路以前是 `cacore::batch_serializer` / `batch_deserializer`，它们已经退场。
	template<typename TRes>
	class TResource : public IResource
	{
	public:
		virtual void Serialize(ca_io::WBatch* inWriter) override
		{
			castl::vector<unsigned char> stage;
			cacore::codegen::CodeGenSerializer<TRes>::serialize(*static_cast<TRes const*>(this), stage);
			inWriter->Write(static_cast<uint64_t>(stage.size()), stage.empty() ? nullptr : stage.data());
			inWriter->SubmitAndWait();
		}

		virtual void Deserialize(ca_io::IOBatch* inReader) override
		{
			std::size_t position = 0;   // 流式读对象只做计数；"读到多少算越界"由来源决定
			cacore::codegen::CodeGenSerializer<TRes>::deserialize(*static_cast<TRes*>(this), *inReader, position);
			inReader->SubmitAndWait();
		}
	};

	class IResourceReadOnly
	{
	public:
		virtual ~IResourceReadOnly() {}
		virtual void Deserialize(ca_io::IOBatch* inReader) = 0;
	};
}
