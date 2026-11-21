#pragma once
//#include <string>
#include "CASTL/CAVector.h"
#include "CASTL/CAString.h"
#include <fstream>
#include <stdexcept>

namespace cacore
{
	/// <summary>
	/// FileLoader 的失败通道（failure-visibility-hygiene design D1）：消息形状与 `ca_io::io_error` 一致
	/// （`动作："路径"（原因）`），但类型独立 —— CACore 是最底层静态库，不得为复用异常类型反向依赖
	/// Interface/IOManager 与 ThreadManager。三个文件函数打不开 / 读不足 / 写失败 / flush 失败时抛它。
	/// </summary>
	class file_error : public std::runtime_error
	{
	public:
		file_error(char const* action, castl::string const& path, castl::string const& reason)
			: std::runtime_error(makeMessage(action, path, reason))
		{
		}

	private:
		static castl::string makeMessage(char const* action, castl::string const& path, castl::string const& reason)
		{
			castl::string message(action);
			message += "：\"";
			message += path;
			message += "\"";
			if (!reason.empty())
			{
				message += "（";
				message += reason;
				message += "）";
			}
			return message;
		}
	};

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