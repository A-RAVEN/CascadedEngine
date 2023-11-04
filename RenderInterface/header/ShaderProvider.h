#pragma once
#include <memory>
#include <string>
#include <SharedTools/header/uhash.h>

using namespace hash_utils;
class ShaderProvider
{
public:
	struct ShaderSourceInfo
	{
		uint64_t dataLength;
		void const* dataPtr;
		std::string const& entryPoint;
	};

	virtual uint64_t GetDataLength(std::string const& codeType) const = 0;
	virtual void const* GetDataPtr(std::string const& codeType) const = 0;
	virtual std::string GetUniqueName() const = 0;
	virtual ShaderSourceInfo const& GetDataInfo(std::string const& codeType) const = 0;
	bool operator==(ShaderProvider const& other) const
	{
		return GetUniqueName() == other.GetUniqueName();
	}
	template <class HashAlgorithm>
	friend void hash_append(HashAlgorithm& h, ShaderProvider const& provider) noexcept
	{
		hash_append(h, provider.GetUniqueName());
	}
};

struct GraphicsShaderSet
{
	std::shared_ptr<ShaderProvider const> vert;
	std::shared_ptr<ShaderProvider const> frag;

	bool operator==(GraphicsShaderSet const& other) const
	{
		return (vert == other.vert || (vert != nullptr && other.vert != nullptr && (*vert.get() == *other.vert.get())))
			&& (frag == other.frag || (frag != nullptr && other.frag != nullptr && (*frag.get() == *other.frag.get())));
	}

	template <class HashAlgorithm>
	friend void hash_append(HashAlgorithm& h, GraphicsShaderSet const& provider) noexcept
	{
		hash_append(h, provider.vert);
		hash_append(h, provider.frag);
	}
};

