#pragma once
#include <string>
#include <vector>
#include <RenderInterface/header/Common.h>

namespace ShaderCompiler
{
	enum class EShaderNumericType
	{
		eFloat = 0,
		eInt32,
		eUInt32,
	};

	struct ShaderNumericParam
	{
		std::string name;
		EShaderNumericType numericType;
		uint32_t count = 1;
		uint32_t x = 1;
		uint32_t y = 1;
		bool operator==(ShaderNumericParam const& other) const
		{
			return (std::memcmp(this, &other, sizeof(ShaderNumericParam)) == 0);
		}
	};

	class BaseShaderParam
	{
	public:
		std::string name;
		TIndex set = INVALID_INDEX;
		TIndex binding = INVALID_INDEX;
	};

	class TextureParam : public BaseShaderParam
	{
	public:
		EShaderTextureType type;
		uint32_t subpassInputAttachmentID = INVALID_INDEX;
	};

	class BufferParam : public BaseShaderParam
	{
	public:
		EShaderBufferType type;
		size_t blockSize = 0;
	};

	class ConstantBufferParam : public BaseShaderParam
	{
	public:
		size_t blockSize = 0;
		std::vector<ShaderNumericParam> numericParams;
	};

	class SamplerParam : public BaseShaderParam
	{
	public:
		EShaderSamplerType type;
	};

	struct ShaderParams
	{
		std::vector<TextureParam> textures;
		std::vector<ConstantBufferParam> cbuffers;
		std::vector<BufferParam> buffers;
		std::vector<SamplerParam> samplers;
	};

	class IShaderCompiler
	{
	public:
		virtual ~IShaderCompiler() = default;
		virtual void AddInlcudePath(std::string const& path) = 0;
		virtual void AddMacro(std::string const& macro_name, std::string const& macro_value) = 0;
		virtual std::string PreprocessShader(
			EShaderSourceType shader_source_type
			, std::string const& file_name
			, std::string const& shader_src
			, ECompileShaderType shader_type) = 0;
		virtual std::vector<uint32_t> CompileShaderSource(
			EShaderSourceType shader_source_type
			, std::string const& file_name
			, std::string const& shader_src
			, std::string const& entry_point
			, ECompileShaderType shader_type
			, bool optimize = true
			, bool debug = false) = 0;
	private:
	};
}