#include <FileLoader.h>
#include "CASTL/CAVector.h"
#include "CASTL/CAString.h"
#include <fstream>

namespace cacore
{
	using namespace castl;
	castl::string LoadStringFile(castl::string const& file_source)
	{
		std::ifstream file_src(castl::to_std(file_source));
		castl::string result;
		if (file_src.is_open())
		{
			std::string line;
			while (std::getline(file_src, line))
			{
				result += castl::to_ca(line) + "\n";
			}
		}
		return result;
	}

	castl::vector<uint8_t> LoadBinaryFile(castl::string const& file_source)
	{
		std::ifstream file_src(castl::to_std(file_source), std::ios::in | std::ios::binary);
		castl::vector<uint8_t> result;
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

	void WriteBinaryFile(castl::string const& file_dest, void const* data, size_t size)
	{
		std::ofstream file_dst(castl::to_std(file_dest), std::ios::out | std::ios::binary);
		if (file_dst.is_open())
		{
			file_dst.write(static_cast<char const*>(data), size);
		}
		file_dst.close();
	}
}