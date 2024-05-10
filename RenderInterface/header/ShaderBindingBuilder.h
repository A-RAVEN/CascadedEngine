#pragma once
#include <CASTL/CAString.h>
#include <CASTL/CAVector.h>
#include <DebugUtils.h>
#include <Hasher.h>

enum class EShaderBindingNumericType
{
	eFloat = 0,
	eInt32,
	eUInt32,
};

enum class ETextureDimension
{
	e2D = 0,
	e3D
};

template <class T>
struct is_shaderbinding_arighmetic_type : public std::integral_constant<bool, (std::is_integral<T>::value && sizeof(T) == 4)> 
{
public:
	static constexpr EShaderBindingNumericType numericType = EShaderBindingNumericType::eUInt32;
};

template <>
struct is_shaderbinding_arighmetic_type<float> : public std::true_type
{
public:
	static constexpr EShaderBindingNumericType numericType = EShaderBindingNumericType::eFloat;
};

template <>
struct is_shaderbinding_arighmetic_type<int32_t> : public std::true_type
{
public:
	static constexpr EShaderBindingNumericType numericType = EShaderBindingNumericType::eInt32;
};

struct ShaderBindingDescriptor
{
public:
	EShaderBindingNumericType numericType;
	uint32_t count = 1;
	uint32_t x = 1;
	uint32_t y = 1;
	ShaderBindingDescriptor(EShaderBindingNumericType inType
		, uint32_t inX = 1, uint32_t inY = 1, uint32_t inCount = 1) :
		numericType(inType)
		, count(inCount)
		, x(inX)
		, y(inY)
	{
	}

	auto operator <=>(const ShaderBindingDescriptor&) const = default;
};
CA_REFLECTION(ShaderBindingDescriptor, numericType, count, x, y);


struct ShaderTextureDescriptor
{
public:
	EShaderBindingNumericType numericType;
	uint32_t channels = 4;
	uint32_t count = 1;
	ETextureDimension dimension = ETextureDimension::e2D;
	bool isRW = false;
	ShaderTextureDescriptor(EShaderBindingNumericType inType
		, bool inIsRW, ETextureDimension inDim, uint32_t inChannel = 4, uint32_t inCount = 1) :
		numericType(inType)
		, channels(inChannel)
		, count(inCount)
		, dimension(inDim)
		, isRW(inIsRW)
	{
	}

	auto operator <=>(const ShaderTextureDescriptor&) const = default;

};
CA_REFLECTION(ShaderTextureDescriptor, numericType, channels, count, dimension, isRW);


class ShaderConstantsBuilder
{
public:
	ShaderConstantsBuilder(castl::string const& name) : m_Name(name){}

	template<typename T>
	ShaderConstantsBuilder& Scalar(castl::string const& name)
	{
		static_assert(is_shaderbinding_arighmetic_type<T>::value, "shader binding arighmetic type only support 32 bit arithmetic type");
		m_NumericDescriptors.push_back(castl::make_pair(name, ShaderBindingDescriptor{ 
			is_shaderbinding_arighmetic_type<T>::numericType, 1 }));
		return *this;
	}

	template<typename T>
	ShaderConstantsBuilder& Vec2(castl::string const& name)
	{
		static_assert(is_shaderbinding_arighmetic_type<T>::value, "shader binding arighmetic type only support 32 bit arithmetic type");
		m_NumericDescriptors.push_back(castl::make_pair(name, ShaderBindingDescriptor{
			is_shaderbinding_arighmetic_type<T>::numericType, 1, 2 }));
		return *this;
	}

	template<typename T>
	ShaderConstantsBuilder& Vec3(castl::string const& name)
	{
		static_assert(is_shaderbinding_arighmetic_type<T>::value, "shader binding arighmetic type only support 32 bit arithmetic type");
		m_NumericDescriptors.push_back(castl::make_pair(name, ShaderBindingDescriptor{
			is_shaderbinding_arighmetic_type<T>::numericType, 1, 3 }));
		return *this;
	}

	template<typename T, uint32_t count = 1>
	ShaderConstantsBuilder& Vec4(castl::string const& name)
	{
		static_assert(is_shaderbinding_arighmetic_type<T>::value, "shader binding arighmetic type only support 32 bit arithmetic type");
		static_assert(count > 0, "shader binding count must be greater than 0");
		m_NumericDescriptors.push_back(castl::make_pair(name, ShaderBindingDescriptor{ 
			is_shaderbinding_arighmetic_type<T>::numericType, count, 4 }));
		return *this;
	}

	template<typename T, uint32_t count = 1>
	ShaderConstantsBuilder& Mat4(castl::string const& name)
	{
		static_assert(is_shaderbinding_arighmetic_type<T>::value, "shader binding arighmetic type only support 32 bit arithmetic type");
		static_assert(count > 0, "shader binding count must be greater than 0");
		m_NumericDescriptors.push_back(castl::make_pair(name, ShaderBindingDescriptor{ 
			is_shaderbinding_arighmetic_type<T>::numericType, count, 4, 4 }));
		return *this;
	}

	castl::string const& GetName() const { return m_Name; }

	bool operator==(ShaderConstantsBuilder const& rhs) const
	{
		return m_NumericDescriptors == rhs.m_NumericDescriptors
			&& m_Name == rhs.m_Name;
	}

	template <class HashAlgorithm>
	friend void hash_append(HashAlgorithm& h, ShaderConstantsBuilder const& bindingBuilder) noexcept
	{
		hash_append(h, bindingBuilder.m_Name);
		hash_append(h, bindingBuilder.m_NumericDescriptors);
	}

	castl::vector<castl::pair<castl::string, ShaderBindingDescriptor>> const& GetNumericDescriptors() const{
		return m_NumericDescriptors;
	}
protected:
	castl::string m_Name;
	castl::vector<castl::pair<castl::string, ShaderBindingDescriptor>> m_NumericDescriptors;
	CA_PRIVATE_REFLECTION(ShaderConstantsBuilder);
};
CA_REFLECTION(ShaderConstantsBuilder, m_Name, m_NumericDescriptors);


class ShaderBindingBuilder
{
public:
	ShaderBindingBuilder() = default;

	ShaderBindingBuilder(castl::string const& space_name) : m_SpaceName(space_name) {}

	ShaderBindingBuilder& ConstantBuffer(ShaderConstantsBuilder const& constantDescs)
	{
		m_ConstantBufferDescriptors.emplace_back(constantDescs);
		return *this;
	}

	inline ShaderBindingBuilder& TextureGeneral(castl::string const& name
		, EShaderBindingNumericType numericType, ETextureDimension dimension, bool unorderedAccess, uint32_t channels, uint32_t count)
	{
		CA_ASSERT(channels > 0 && channels <= 4, "texture binding supports 1-4 channels");
		CA_ASSERT(count > 0, "shader binding count must be greater than 0");
		m_TextureDescriptors.push_back(castl::make_pair(name, ShaderTextureDescriptor{
			numericType
			, unorderedAccess
			, dimension
			, channels
			, count }));
		return *this;
	}

	template<typename T, uint32_t channels = 4, uint32_t count = 1>
	ShaderBindingBuilder& Texture2D(castl::string const& name)
	{
		static_assert(is_shaderbinding_arighmetic_type<T>::value, "shader binding arighmetic type only support 32 bit arithmetic type");
		static_assert(channels > 0 && channels <= 4, "texture binding supports 1-4 channels");
		static_assert(count > 0, "shader binding count must be greater than 0");
		m_TextureDescriptors.push_back(castl::make_pair(name, ShaderTextureDescriptor{
		is_shaderbinding_arighmetic_type<T>::numericType
		, false, ETextureDimension::e2D, channels, count }));
		return *this;
	}

	template<typename T, uint32_t channels = 4, uint32_t count = 1>
	ShaderBindingBuilder& Texture3D(castl::string const& name)
	{
		static_assert(is_shaderbinding_arighmetic_type<T>::value, "shader binding arighmetic type only support 32 bit arithmetic type");
		static_assert(channels > 0 && channels <= 4, "texture binding supports 1-4 channels");
		static_assert(count > 0, "shader binding count must be greater than 0");
		m_TextureDescriptors.push_back(castl::make_pair(name, ShaderTextureDescriptor{
		is_shaderbinding_arighmetic_type<T>::numericType
		, false, ETextureDimension::e3D, channels, count }));
		return *this;
	}

	template<typename T, uint32_t channels = 4, uint32_t count = 1>
	ShaderBindingBuilder& RWTexture2D(castl::string const& name)
	{
		static_assert(is_shaderbinding_arighmetic_type<T>::value, "shader binding arighmetic type only support 32 bit arithmetic type");
		static_assert(channels > 0 && channels <= 4, "texture binding supports 1-4 channels");
		static_assert(count > 0, "shader binding count must be greater than 0");
		m_TextureDescriptors.push_back(castl::make_pair(name, ShaderTextureDescriptor{
		is_shaderbinding_arighmetic_type<T>::numericType
		, true, ETextureDimension::e2D, channels, count }));
		return *this;
	}

	template<typename T, uint32_t channels = 4, uint32_t count = 1>
	ShaderBindingBuilder& RWTexture3D(castl::string const& name)
	{
		static_assert(is_shaderbinding_arighmetic_type<T>::value, "shader binding arighmetic type only support 32 bit arithmetic type");
		static_assert(channels > 0 && channels <= 4, "texture binding supports 1-4 channels");
		static_assert(count > 0, "shader binding count must be greater than 0");
		m_TextureDescriptors.push_back(castl::make_pair(name, ShaderTextureDescriptor{
		is_shaderbinding_arighmetic_type<T>::numericType
		, true, ETextureDimension::e3D, channels, count }));
		return *this;
	}

	ShaderBindingBuilder& SamplerState(castl::string const& name)
	{
		m_TextureSamplers.push_back(name);
		return *this;
	}

	ShaderBindingBuilder& StructuredBuffer(castl::string const& name)
	{
		m_StructuredBuffers.push_back(name);
		return *this;
	}

	castl::string const& GetSpaceName() const {
		return m_SpaceName;
	}

	castl::vector<ShaderConstantsBuilder> const& GetConstantBufferDescriptors() const {
		return m_ConstantBufferDescriptors;
	}

	castl::vector<castl::pair<castl::string, ShaderTextureDescriptor>> const& GetTextureDescriptors() const {
		return m_TextureDescriptors;
	}

	castl::vector<castl::string> const& GetTextureSamplers() const
	{
		return m_TextureSamplers;
	}

	castl::vector<castl::string> const& GetStructuredBuffers() const
	{
		return m_StructuredBuffers;
	}

	bool operator==(ShaderBindingBuilder const& rhs) const
	{
		return m_ConstantBufferDescriptors == rhs.m_ConstantBufferDescriptors
			&& m_TextureDescriptors == rhs.m_TextureDescriptors
			&& m_TextureSamplers == rhs.m_TextureSamplers
			&& m_StructuredBuffers == rhs.m_StructuredBuffers
			;
	}

	template <class HashAlgorithm>
	friend void hash_append(HashAlgorithm& h, ShaderBindingBuilder const& bindingBuilder) noexcept
	{
		hash_append(h, bindingBuilder.m_ConstantBufferDescriptors);
		hash_append(h, bindingBuilder.m_TextureDescriptors);
		hash_append(h, bindingBuilder.m_TextureSamplers);
		hash_append(h, bindingBuilder.m_StructuredBuffers);
	}

protected:
	castl::string m_SpaceName;
	castl::vector<ShaderConstantsBuilder> m_ConstantBufferDescriptors;
	castl::vector<castl::pair<castl::string, ShaderTextureDescriptor>> m_TextureDescriptors;
	castl::vector<castl::string> m_TextureSamplers;
	castl::vector<castl::string> m_StructuredBuffers;

	CA_PRIVATE_REFLECTION(ShaderBindingBuilder);
};
CA_REFLECTION(ShaderBindingBuilder, m_SpaceName, m_ConstantBufferDescriptors, m_TextureDescriptors, m_TextureSamplers, m_StructuredBuffers);


class ShaderBindingDescriptorList
{
public:
	ShaderBindingDescriptorList(std::initializer_list<ShaderBindingBuilder> binding_sets) : shaderBindingDescs(binding_sets) {}
	castl::vector<ShaderBindingBuilder> shaderBindingDescs;
};
CA_REFLECTION(ShaderBindingDescriptorList, shaderBindingDescs);

