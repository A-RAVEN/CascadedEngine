#pragma once
#include <sha256.h>
#include <komihash.h>

namespace cahash
{
    class sha256_hash
    {
        SHA256 shaState;
    public:

        struct result_type
        {
            unsigned char data[32];
			auto operator <=>(result_type const&) const = default;
        };

        void operator()(void const* key, size_t len) noexcept
        {
            shaState.add(key, len);
        }

        explicit operator result_type() noexcept
        {
            result_type result;
            shaState.getHash(result.data);
            return result;
        }
    };

    class fnv1a
    {
        size_t state_ = 14695981039346656037u;
    public:
        using result_type = size_t;

        void operator()(void const* key, size_t len) noexcept
        {
            unsigned char const* p = static_cast<unsigned char const*>(key);
            unsigned char const* const e = p + len;
            for (; p < e; ++p)
                state_ = (state_ ^ *p) * 1099511628211u;
        }

        explicit operator result_type() noexcept
        {
            return state_;
        }
    };

    class stdHash
    {
		size_t state_ = 0;
	public:
		using result_type = size_t;

		void operator()(void const* key, size_t len) noexcept
		{
			auto newHash = std::hash<std::string>{}(std::string(static_cast<char const*>(key), len));
			if (state_ == 0)
			{
				state_ = newHash;
			}
			else
			{
				state_ = state_ ^ (newHash << 1);
			}
		}

		explicit operator result_type() noexcept
		{
			return state_;
		}
    };

    class komiHash
    {
        komihash_stream_t state_ = {};
    public:
        using result_type = size_t;
        komiHash()
        {
            komihash_stream_init(&state_, 0);
        }
        void operator()(void const* key, size_t len) noexcept
        {
            komihash_stream_update(&state_, key, len);
        }

        explicit operator result_type() noexcept
        {
            return komihash_stream_final(&state_);
        }
    };

	template <class HashAlgorithm>
	static HashAlgorithm::result_type getHash(void const* key, size_t len) noexcept
	{
		HashAlgorithm h;
		h(key, len);
		return static_cast<HashAlgorithm::result_type>(h);
	}
}