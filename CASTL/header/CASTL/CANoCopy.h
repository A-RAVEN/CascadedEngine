#pragma once

namespace castl
{
	class nocopiable
	{
	public:
		nocopiable(const nocopiable&) = delete;
		nocopiable& operator=(const nocopiable&) = delete;
		nocopiable(nocopiable&&) = default;
	protected:
		nocopiable() = default;
		~nocopiable() = default;
	};
}