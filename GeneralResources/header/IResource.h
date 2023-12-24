#pragma once
#include <vector>
namespace resource_management
{
	class IResource
	{
	public:
		virtual void Serialzie(std::vector<std::byte>& out) = 0;
		virtual void Deserialzie(std::vector<std::byte>& in) = 0;
	};
}