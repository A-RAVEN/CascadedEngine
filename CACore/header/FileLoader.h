#pragma once
#include <string>
#include <fstream>

namespace fileloading_utils
{
	static std::string LoadStringFile(std::string file_source)
	{
		std::ifstream file_src(file_source);
		std::string result;
		if (file_src.is_open())
		{
			std::string line;
			while (std::getline(file_src, line))
			{
				result += line + "\n";
			}
		}
		return result;
	}

	static std::vector<std::byte> LoadBinaryFile(std::string file_source)
	{
		std::ifstream file_src(file_source, std::ios::in | std::ios::binary);
		std::vector<std::byte> result;
		if (file_src.is_open())
		{
			file_src.seekg(0, std::ios::end);
			size_t size = file_src.tellg();
			result.resize(size);
			file_src.seekg(0, std::ios::beg);
			file_src.read(reinterpret_cast<char*>(result.data()), size);
		}
		return result;
	}

	static void WriteBinaryFile(std::string file_path, void const* data, size_t size)
	{
		std::ofstream file_dst(file_path, std::ios::out | std::ios::binary);
		if (file_dst.is_open())
		{
			file_dst.write(static_cast<char const*>(data), size);
		}
		file_dst.close();
	}
}