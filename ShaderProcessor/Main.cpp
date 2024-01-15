//#include <clang-c/Index.h>
#define NONMINMAX
#include <wrl/client.h>
#include <dxcapi.h>
#include <filesystem>
#include <iostream>
#include <vector>
#include <CACore/header/FileLoader.h>
#include <CACore/header/DebugUtils.h>
using namespace Microsoft::WRL;

int main(int argc, char* argv[])
{
	std::filesystem::path rootPathFS{ "../../../../" , std::filesystem::path::format::native_format };
	std::filesystem::path rootPath = std::filesystem::absolute(rootPathFS);
	std::string resourceString = rootPath.string() + "CAResources";

	auto shaderSource = fileloading_utils::LoadStringFile(resourceString + "/Shaders/testShader.hlsl");

	ComPtr<IDxcCompiler3> pCompiler;
	DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(pCompiler.GetAddressOf()));


	ComPtr<IDxcUtils> pUtils;
	DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(pUtils.GetAddressOf()));
	ComPtr<IDxcBlobEncoding> pSource;
	pUtils->CreateBlob(shaderSource.c_str(), shaderSource.length(), CP_UTF8, pSource.GetAddressOf());

	std::vector<LPCWCH> arguments;

	//Preprocessing
	arguments.push_back(L"-P");

	//Spir-V
	arguments.push_back(L"-spirv");
	arguments.push_back(L"-fspv-target-env=vulkan1.3");

	// -E for the entry point (eg. 'main')
	arguments.push_back(L"-E");
	arguments.push_back(L"frag");

	// -T for the target profile (eg. 'ps_6_6')
	arguments.push_back(L"-T");
	arguments.push_back(L"ps_6_6");

	// Strip reflection data and pdbs (see later)
	//arguments.push_back(L"-Qstrip_debug");
	//arguments.push_back(L"-Qstrip_reflect");

	arguments.push_back(DXC_ARG_WARNINGS_ARE_ERRORS); //-WX
	arguments.push_back(DXC_ARG_DEBUG); //-Zi

	DxcBuffer sourceBuffer;
	sourceBuffer.Ptr = pSource->GetBufferPointer();
	sourceBuffer.Size = pSource->GetBufferSize();
	sourceBuffer.Encoding = 0;

	ComPtr<IDxcResult> pCompileResult;
	HRESULT HR{ pCompiler->Compile(&sourceBuffer, arguments.data(), (uint32_t)arguments.size(), nullptr, IID_PPV_ARGS(pCompileResult.GetAddressOf())) };

	ComPtr<IDxcBlobUtf8> pErrors;
	pCompileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(pErrors.GetAddressOf()), nullptr);

	if (pErrors && pErrors->GetStringLength() > 0)
	{
		CA_LOG_ERR(pErrors->GetStringPointer());
		return 1;
	}

	ComPtr<IDxcBlobUtf8> pPreprocessed;
	pCompileResult->GetOutput(DXC_OUT_HLSL, IID_PPV_ARGS(pPreprocessed.GetAddressOf()), nullptr);
	CA_LOG_ERR(pPreprocessed->GetStringPointer());

	//ComPtr<IDxcBlob> pResult;
	//pCompileResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(pResult.GetAddressOf()), nullptr);
	//std::vector<uint32_t> outBuffer;
	//outBuffer.resize(pResult->GetBufferSize() / sizeof(uint32_t));
	//memcpy(outBuffer.data(), pResult->GetBufferPointer(), pResult->GetBufferSize());

	return 0;
}