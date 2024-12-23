#pragma once
#include <CASTL/CAVector.h>
#include <IOManager/IOManager.h>
namespace resource_management
{
	class IResource
	{
	public:
		virtual ~IResource() {}
		virtual void Serialzie(castl::vector<uint8_t>& out) = 0;
		virtual void Deserialzie(castl::vector<uint8_t>& in) = 0;
		virtual void Deserialzie(ca_io::IOBatch* inReader) = 0;
		virtual void Load() {};
		virtual void Unload() {};
	};
}