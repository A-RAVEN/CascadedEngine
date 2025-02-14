#pragma once
#include <CASTL/CAVector.h>
#include <IOManager/IOManager.h>
namespace resource_management
{
	class IResource
	{
	public:
		virtual ~IResource() {}
		virtual void Serialize(ca_io::WBatch* inWriter) = 0;
		virtual void Deserialize(ca_io::IOBatch* inReader) = 0;
	};

	template<typename TRes>
	class TResource : public IResource
	{
	public:
		virtual void Serialize(ca_io::WBatch* inWriter) override
		{
			cacore::batch_serializer<ca_io::WBatch> serializer(inWriter);
			serializer.serialize<TRes>(*(static_cast<TRes const*>(this)));
			serializer.finalize();
		}

		virtual void Deserialize(ca_io::IOBatch* inReader) override
		{
			cacore::batch_deserializer<ca_io::IOBatch> deserializer(inReader);
			deserializer.deserialize<TRes>(*(static_cast<TRes *>(this)));
			deserializer.finalize();
		}
	};

	class IResourceReadOnly
	{
	public:
		virtual ~IResourceReadOnly() {}
		virtual void Deserialize(ca_io::IOBatch* inReader) = 0;
	};
}