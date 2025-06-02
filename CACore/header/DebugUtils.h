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

	template <typename... T>
	FMT_INLINE void log_with_location(std::source_location const& location, bool show_location, format_string<T...> fmt, T&&... args) {
		style_log_with_location(fg(fmt::color::gray), location, show_location, fmt, std::forward<T>(args)...);
	}

	template <typename... T>
	FMT_INLINE void log_with_location(std::source_location const& location, bool show_location, castl::string_view const& str) {
		log_with_location(location, show_location, "{}", str);
	}
}

#define CA_LOG(_log, ...) {cacore::log_with_location(std::source_location::current(), false, _log __VA_OPT__(, __VA_ARGS__ ));}
#define CA_LOG_IF( _condition , _log, ...) {if(_condition){CA_LOG(_log, __VA_ARGS__);}}
#define CA_LOG_ERR(_log, ...) {cacore::error_with_location(std::source_location::current(), _log __VA_OPT__(, __VA_ARGS__ ));}
#define CA_LOG_ERR_BREAK(_log, ...) {CA_LOG_ERR(_log __VA_OPT__(, __VA_ARGS__ ));__debugbreak();}
#define CA_ASSERT( _condition , _log, ...) {if(!(_condition)){CA_LOG_ERR(_log __VA_OPT__(, __VA_ARGS__ ));}}
#define CA_ASSERT_BREAK( _condition , _log, ...) {if(!(_condition)){CA_LOG_ERR(_log __VA_OPT__(, __VA_ARGS__ ));__debugbreak();}}
#define CA_BREAK_IF(_condition ) {if(_condition){__debugbreak();}}
#define CA_CLASS_NAME(_class) (typeid(_class).name())
