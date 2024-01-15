#pragma once

namespace serialization
{
	template<typename ByteView, typename T>
	struct Archive
	{
		explicit Archive(ByteView& data, T)
			: m_Data(data)
		{
		}

		auto Position() const
		{
			return m_Position{};
		}

		auto operator()(auto&& ... objs)
		{
			return serialize_many(objs...);
		}

		auto serialize_many(auto& first, auto&& ... remains)
		{
			if (auto result = serialize_one(first); failure(result))
			{
				return result;
			}
			return serialize_many(remains...);
		}


		ByteView& m_Data;
		std::size_t m_Position = 0;
	};
}