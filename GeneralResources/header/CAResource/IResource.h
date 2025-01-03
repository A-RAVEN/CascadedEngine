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

	class IResourceReadOnly
	{
	public:
		virtual ~IResourceReadOnly() {}
		virtual void Deserialize(ca_io::IOBatch* inReader) = 0;
	};
}