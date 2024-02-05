#pragma once
#include <CASTL/CAVector.h>
namespace resource_management
{
	class IResource
	{
	public:
		virtual void Serialzie(castl::vector<uint8_t>& out) = 0;
		virtual void Deserialzie(castl::vector<uint8_t>& in) = 0;
		virtual void Load() {};
		virtual void Unload() {};
	};
}