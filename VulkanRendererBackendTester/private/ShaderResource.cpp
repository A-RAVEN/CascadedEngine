#include "ShaderResource.h"
#include <SharedTools/header/FileLoader.h>
#include <filesystem>

namespace resource_management
{
	ShaderResourceLoader::ShaderResourceLoader() : m_ShaderCompilerLoader("ShaderCompiler")
	{
		m_ShaderCompiler = m_ShaderCompilerLoader.New();
	}

	void print_error(const std::string& details, std::errc error_code)
	{
		std::string value_name;
		switch (error_code)
		{
		case std::errc::result_out_of_range:
			value_name = "attempting to write or read from a too short buffer";
			break;
		case std::errc::no_buffer_space:
			value_name = "growing buffer would grow beyond the allocation limits or overflow";
			break;
		case std::errc::value_too_large:
			value_name = "varint (variable length integer) encoding is beyond the representation limits";
			break;
		case std::errc::message_size:
			value_name = "message size is beyond the user defined allocation limits";
			break;
		case std::errc::not_supported:
			value_name = "attempt to call an RPC that is not listed as supported";
			break;
		case std::errc::bad_message:
			value_name = "attempt to read a variant of unrecognized type";
			break;
		case std::errc::invalid_argument:
			value_name = "attempting to serialize null pointer or a value-less variant";
			break;
		case std::errc::protocol_error:
			value_name = "attempt to deserialize an invalid protocol message";
			break;
		}
		std::cout << details << ":\n  "
			<< " (" << value_name << ")\n\n";
	}

	void ShaderResourceLoader::ImportResource(ResourceManagingSystem* resourceManager
		, std::string const& inPath
		, std::string const& outPath)
	{
		std::filesystem::path resourcePath(inPath);
		auto fileName = resourcePath.filename();
		ShaderResrouce* resource = resourceManager->AllocResource<ShaderResrouce>(outPath);
		auto shaderSource = fileloading_utils::LoadStringFile(inPath);
		auto spirVResult = m_ShaderCompiler->CompileShaderSource(EShaderSourceType::eHLSL
			, fileName.string()
			, shaderSource
			, "vert"
			, ECompileShaderType::eVert
			, true, false);
		resource->m_VertexShaderProvider.SetUniqueName(resourcePath.stem().string() + ".vert");
		resource->m_VertexShaderProvider.SetData("spirv", "vert", spirVResult.data(), spirVResult.size() * sizeof(uint32_t));
		
		spirVResult = m_ShaderCompiler->CompileShaderSource(EShaderSourceType::eHLSL
			, fileName.string()
			, shaderSource
			, "frag"
			, ECompileShaderType::eFrag
			, true, false);

		
		resource->m_FragmentShaderProvider.SetUniqueName(resourcePath.stem().string() + ".frag");
		resource->m_FragmentShaderProvider.SetData("spirv", "frag", spirVResult.data(), spirVResult.size() * sizeof(uint32_t));
	
		//auto [data, in, out] = zpp::bits::data_in_out();
		//auto result = out(*resource);
		//if (failure(result)) {
		//	// `result` is implicitly convertible to `std::errc`.
		//	// handle the error or return/throw exception.
		//	print_error("serialize failed", result);
		//}

		//fileloading_utils::WriteBinaryFile(outPath, data.data(), data.size());

		//ShaderResrouce deserializeResource;
		//result = in(deserializeResource);
		//if (failure(result)) {
		//	// `result` is implicitly convertible to `std::errc`.
		//	// handle the error or return/throw exception.
		//	print_error("deserialize failed", result);
		//}
	}
	void ShaderResrouce::Serialzie(std::vector<std::byte>& data)
	{
		zpp::bits::out out(data);
		auto result = out(*this);
		if (failure(result)) {
			print_error("serialize failed", result);
		}
	}
	void ShaderResrouce::Deserialzie(std::vector<std::byte>& data)
	{
		zpp::bits::in in(data);
		auto result = in(*this);
		if (failure(result)) {
			print_error("deserialize failed", result);
		}
	}
}