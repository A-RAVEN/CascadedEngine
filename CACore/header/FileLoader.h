#pragma once
//#include <string>
#include "CASTL/CAVector.h"
#include "CASTL/CAString.h"
#include <fstream>

namespace cacore
{
	/// <summary>
	/// Load a string file
	/// </summary>
	/// <param name="file_source"></param>
	/// <returns></returns>
	castl::string LoadStringFile(castl::string const& file_source);

	/// <summary>
	/// Load a binary file
	/// </summary>
	/// <param name="file_source"></param>
	/// <returns></returns>
	castl::vector<uint8_t> LoadBinaryFile(castl::string const& file_source);

	/// <summary>
	/// Write a binary file
	/// </summary>
	/// <param name="file_dest"></param>
	/// <param name="data"></param>
	/// <param name="size"></param>
	void WriteBinaryFile(castl::string const& file_dest, void const* data, size_t size);
}