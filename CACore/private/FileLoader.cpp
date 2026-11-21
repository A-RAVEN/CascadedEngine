#include <FileLoader.h>
#include "CASTL/CAVector.h"
#include "CASTL/CAString.h"
#include <fstream>

namespace cacore
{
	using namespace castl;

	// failure-visibility-hygiene D2：三个函数失败必抛 `cacore::file_error` 并点名路径；
	// 空文件合法（返回空容器，不抛）。消息形状与 `ca_io::io_error` 一致：`动作："路径"（原因）`。
	castl::string LoadStringFile(castl::string const& file_source)
	{
		std::ifstream file_src(file_source);
		if (!file_src.is_open())
		{
			throw file_error("加载文本文件", file_source, "无法打开文件");
		}
		castl::string result;
		std::string line;
		while (std::getline(file_src, line))
		{
			result += line + "\n";
		}
		// 读到文件末尾是正常结束；流状态坏却没到 eof = 读取未完成（D2 的"文本读取流状态坏且非正常结束"）。
		if (file_src.bad() || !file_src.eof())
		{
			throw file_error("加载文本文件", file_source, "读取未完成（流状态异常且未到文件末尾）");
		}
		return result;
	}

	castl::vector<uint8_t> LoadBinaryFile(castl::string const& file_source)
	{
		std::ifstream file_src(file_source, std::ios::in | std::ios::binary);
		if (!file_src.is_open())
		{
			throw file_error("加载二进制文件", file_source, "无法打开文件");
		}
		file_src.seekg(0, std::ios::end);
		if (!file_src)
		{
			throw file_error("加载二进制文件", file_source, "无法定位到文件末尾");
		}
		const std::streamoff size = static_cast<std::streamoff>(file_src.tellg());
		if (size < 0)
		{
			throw file_error("加载二进制文件", file_source, "无法读取文件尺寸");
		}
		castl::vector<uint8_t> result;
		result.resize(static_cast<size_t>(size));
		file_src.seekg(0, std::ios::beg);
		if (!file_src)
		{
			throw file_error("加载二进制文件", file_source, "无法回到文件开头");
		}
		// 空文件合法：size == 0 时不读，直接返回空容器。
		if (size > 0)
		{
			file_src.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(size));
			// 读不足必须抛，不许把未填充的缓冲当完整内容返回（D2 的"binary 按 tellg 尺寸读后 gcount() 不足"）。
			if (static_cast<size_t>(file_src.gcount()) != static_cast<size_t>(size))
			{
				throw file_error("加载二进制文件", file_source, "实际读到的字节数少于文件尺寸");
			}
		}
		return result;
	}

	void WriteBinaryFile(castl::string const& file_dest, void const* data, size_t size)
	{
		std::ofstream file_dst(file_dest, std::ios::out | std::ios::binary);
		if (!file_dst.is_open())
		{
			throw file_error("写入二进制文件", file_dest, "无法创建或打开目标文件");
		}
		file_dst.write(static_cast<char const*>(data), static_cast<std::streamsize>(size));
		if (!file_dst)
		{
			throw file_error("写入二进制文件", file_dest, "写入失败");
		}
		file_dst.flush();
		if (!file_dst)
		{
			throw file_error("写入二进制文件", file_dest, "刷新缓冲区失败");
		}
		file_dst.close();
		if (!file_dst)
		{
			throw file_error("写入二进制文件", file_dest, "关闭文件失败");
		}
	}
}
