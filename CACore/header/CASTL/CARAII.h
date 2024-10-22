#pragma once
#include "CAFunctional.h"

namespace castl
{
	template<typename T>
	class raii_wrapper
	{
	public:
		template<typename TT = T, typename = std::enable_if_t<std::is_default_constructible<TT>::value>>
		raii_wrapper() : m_RAIIData(), _Releaser(nullptr)
			, _RAII_Aquired(false)
		{
		}

		raii_wrapper(T&& rttiData, std::function<void(T& releaseObj)> releaser) : m_RAIIData(std::move(rttiData))
			, _Releaser(std::move(releaser))
			, _RAII_Aquired(true)
		{
		}

		void RAIIRelease()
		{
			if (!_RAII_Aquired)
				return;
			_RAII_Aquired = false;
			_Releaser(m_RAIIData);
		}

		virtual ~raii_wrapper()
		{
			RAIIRelease();
		}

		//raii_wrapper(raii_wrapper& other) :
		//	m_RAIIData(std::move(other.m_RAIIData))
		//	, _Releaser(std::move(other._Releaser))
		//	, _RAII_Aquired(other._RAII_Aquired)
		//{
		//	other._RAII_Aquired = false;
		//}

		//raii_wrapper& operator=(raii_wrapper& other)
		//{
		//	RAIIRelease();
		//	m_RAIIData = std::move<T&>(other.m_RAIIData);
		//	_Releaser = std::move(other._Releaser);
		//	_RAII_Aquired = std::move(other._RAII_Aquired);
		//	other._RAII_Aquired = false;
		//	return *this;
		//}

		raii_wrapper(raii_wrapper&& other) : 
			m_RAIIData(std::move(other.m_RAIIData))
			, _Releaser(std::move(other._Releaser))
			, _RAII_Aquired(other._RAII_Aquired)
		{
			other._RAII_Aquired = false;
		}
		raii_wrapper& operator=(raii_wrapper&& other)
		{
			RAIIRelease();
			m_RAIIData = std::move<T&>(other.m_RAIIData);
			_Releaser = std::move(other._Releaser);
			_RAII_Aquired = std::move(other._RAII_Aquired);
			other._RAII_Aquired = false;
			return *this;
		}


		bool IsRAIIAquired() const
		{
			return _RAII_Aquired;
		}

		T& Get()
		{
			return m_RAIIData;
		}

		T const& Get() const
		{
			return m_RAIIData;
		}

		T& operator*()
		{
			return Get();
		}

		T const& operator*()const
		{
			return Get();
		}

		template<typename TT = T>
		typename std::enable_if_t<std::is_pointer_v<TT>, TT> operator->() const noexcept
		{
			return m_RAIIData;
		}

		template<typename TT = T>
		typename std::enable_if_t<std::is_pointer_v<TT>, TT> operator->() noexcept
		{
			return m_RAIIData;
		}

		template<typename TT = T>
		typename std::enable_if_t<std::is_class_v<TT>, TT const*> operator->() const noexcept
		{
			return &m_RAIIData;
		}

		template<typename TT = T>
		typename std::enable_if_t<std::is_class_v<TT>, TT*> operator->() noexcept
		{
			return &m_RAIIData;
		}

	private:
		mutable T m_RAIIData;
		mutable std::function<void(T& releaseObj)> _Releaser;
		mutable bool _RAII_Aquired = false;
	};
}