#pragma once
#include <CASTL/CAString.h>
#include <CACore/CAFormat.h>
#include <source_location>

namespace cacore
{
	template<typename T>
	concept equal_test = requires(T const& a, T const& b)
	{
		{ a == b } -> std::same_as<bool>;
	};

	template <typename... T>
	FMT_INLINE void style_log_with_location(const text_style& ts, std::source_location const& location, bool show_location, format_string<T...> fmt, T&&... args) {
		try
		{
			if (show_location)
			{
				fmt::print(ts, "\n[{}]\n{})\n[{}]\n{}\n", location.function_name(), location.line(), location.file_name(), format(fmt, std::forward<T>(args)...));
			}
			else
			{
				fmt::print(ts, "{}\n", format(fmt, std::forward<T>(args)...));
			}
		}
		catch (format_error err)
		{
			fmt::print(fg(fmt::color::crimson) | fmt::emphasis::bold
				, "\n{}) [{}]\n{}\n", location.line(), location.file_name(), err.what());
		}
	}

	template <typename... T>
	FMT_INLINE void error_with_location(std::source_location const& location, format_string<T...> fmt, T&&... args) {
		style_log_with_location(fg(fmt::color::crimson) | fmt::emphasis::bold, location, true, fmt, std::forward<T>(args)...);
	}

	template <typename... T>
	FMT_INLINE void error_with_location(std::source_location const& location, castl::string_view const& str) {
		error_with_location(location, "{}", str);
	}

	// `const char*`（含字符串字面量）走这一条：数组到指针是**标准转换**，胜过上面两条各自需要的
	// 用户定义转换，于是"字面量该选哪个重载"不再由编译器替你决定（clang 报
	// `call to 'error_with_location' is ambiguous`，MSVC 自己挑一个）。只接不带实参的调用：
	// 带实参时格式串必须在调用点是常量表达式，只能由上面 `format_string` 那条接手。
	template <typename... T>
		requires (sizeof...(T) == 0)
	FMT_INLINE void error_with_location(std::source_location const& location, const char* str) {
		error_with_location(location, castl::string_view(str == nullptr ? "" : str));
	}

	// `castl::string`（运行期拼出来的消息，例如 `ShaderArgList.h` 里
	// `castl::string("shader parameter ") + name.string() + "…"`）：精确匹配胜过上面两条各自需要的
	// 用户定义转换，`CA_ASSERT(cond, 拼接出来的字符串)` 这类调用原先同样歧义。
	template <typename... T>
		requires (sizeof...(T) == 0)
	FMT_INLINE void error_with_location(std::source_location const& location, castl::string const& str) {
		error_with_location(location, castl::string_view(str));
	}

	template <typename... T>
	FMT_INLINE void log_with_location(std::source_location const& location, bool show_location, format_string<T...> fmt, T&&... args) {
		style_log_with_location(fg(fmt::color::gray), location, show_location, fmt, std::forward<T>(args)...);
	}

	template <typename... T>
	FMT_INLINE void log_with_location(std::source_location const& location, bool show_location, castl::string_view const& str) {
		log_with_location(location, show_location, "{}", str);
	}

	// 与上面 error_with_location 的 `const char*` 重载同一根因、同一处置（`CA_LOG("文字")` 这类
	// 不带实参的调用原先也会歧义）。
	template <typename... T>
		requires (sizeof...(T) == 0)
	FMT_INLINE void log_with_location(std::source_location const& location, bool show_location, const char* str) {
		log_with_location(location, show_location, castl::string_view(str == nullptr ? "" : str));
	}

	// 与上面 `error_with_location` 的 `castl::string` 重载同一根因、同一处置。
	template <typename... T>
		requires (sizeof...(T) == 0)
	FMT_INLINE void log_with_location(std::source_location const& location, bool show_location, castl::string const& str) {
		log_with_location(location, show_location, castl::string_view(str));
	}
}

#define CA_LOG(_log, ...) {cacore::log_with_location(std::source_location::current(), false, _log __VA_OPT__(, __VA_ARGS__ ));}
#define CA_LOG_INFO(_log, ...) CA_LOG(_log __VA_OPT__(, __VA_ARGS__ ))
#define CA_LOG_WARN(_log, ...) {cacore::style_log_with_location(fg(fmt::color::yellow), std::source_location::current(), false, _log __VA_OPT__(, __VA_ARGS__ ));}
#define CA_LOG_IF( _condition , _log, ...) {if(_condition){CA_LOG(_log, __VA_ARGS__);}}
#define CA_LOG_ERR(_log, ...) {cacore::error_with_location(std::source_location::current(), _log __VA_OPT__(, __VA_ARGS__ ));}
#define CA_LOG_ERR_BREAK(_log, ...) {CA_LOG_ERR(_log __VA_OPT__(, __VA_ARGS__ ));__debugbreak();}
#define CA_ASSERT( _condition , _log, ...) {if(!(_condition)){CA_LOG_ERR(_log __VA_OPT__(, __VA_ARGS__ ));}}
#define CA_ASSERT_BREAK( _condition , _log, ...) {if(!(_condition)){CA_LOG_ERR(_log __VA_OPT__(, __VA_ARGS__ ));__debugbreak();}}
#define CA_BREAK_IF(_condition ) {if(_condition){__debugbreak();}}
#define CA_CLASS_NAME(_class) (typeid(_class).name())
