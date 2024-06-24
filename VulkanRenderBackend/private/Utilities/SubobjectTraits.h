#pragma once

namespace graphics_backend
{
	template<typename T, typename...TArgs>
	concept has_create = requires(T t)
	{
		t.Create(castl::remove_cvref_t <TArgs>{}...);
	};

	template<typename T, typename...TArgs>
	concept has_initialize = requires(T t)
	{
		t.Initialize(castl::remove_cvref_t<TArgs>{}...);
	};


	template<typename T>
	concept has_release = requires(T t)
	{
		t.Release();
	};

	template<typename T>
	struct SubObjectDefaultDeleter {
		void operator()(T* deleteObject)
		{
			if constexpr (has_release<T>)
			{
				deleteObject->Release();
			}
			delete deleteObject;
		}
	};
}