#pragma once
//#include "CASTL/CA"
//#include <type_traits>
#include <memory>
#include "Reflection.h"

namespace cacore
{
    using namespace careflection;

    template <typename ByteBuffer>
    class serializer
    {
    public:
        serializer(ByteBuffer& buffer) : buffer(buffer) {}

        template<typename Obj>
        constexpr void inline serialize(const Obj& object) requires is_bytebuffer<std::remove_cvref_t<ByteBuffer>>
        {
            using objType = std::remove_cvref_t<decltype(object)>;
            if constexpr (std::is_pointer_v<objType> || managed_pointer_traits<objType>::is_managed_pointer)
            {
                //ignore
            }
            else if constexpr (managed_wrapper_traits<objType>::is_managed_wrapper)
            {
                serialize(managed_wrapper_traits<objType>::get_data(object));
            }
            else if constexpr (std::is_fundamental_v<objType> || std::is_enum_v<objType>)
            {
                serialize_one(object);
            }
			else if constexpr (containerStates<objType>::is_container_with_size)
			{
                serialize_container_with_size(object);
			}
            else if constexpr (std::is_class_v<objType>)
            {
                visit_members(object, [this](auto &&...items) CONSTEXPR_INLINE_LAMBDA{
                    (serialize(items), ...);
                }); //解包结构体
            }
        }
    private:
        template<typename Obj>
        constexpr void serialize_one(const Obj& object)
        {
            append_to_buffer(object);
        }

        template<typename Obj>
        constexpr void serialize_container_with_size(const Obj& object)
        {
            using objType = std::remove_reference_t<decltype(object)>;
            using arrElemType = containerInfo<objType>::elementType;
            uint64_t objSize = containerInfo<objType>::container_size(object);
            append_to_buffer(objSize);
            if constexpr (has_foreach_loop<objType>)
            {
                for (auto& item : object)
                {
                    serialize(item);
                }
            }
            else if constexpr (containerStates<objType>::has_indexer)
            {
                for (auto id = 0; id < objSize; ++id)
                {
                    serialize(object[id]);
                }
            }
            else
            {

            }
        }

        template<typename Obj>
        constexpr void append_to_buffer(const Obj& object)
        {
            using objType = std::remove_cvref_t<decltype(object)>;
            static_assert(std::is_trivially_copyable_v<objType>, "Object must be trivially copyable");
            append_to_buffer(&object, sizeof(objType));
        }

        constexpr void append_to_buffer(const void* data, size_t size)
		{
			auto endPoint = buffer.size();
			buffer.resize(endPoint + size);
			memcpy(buffer.data() + endPoint, data, size);
		}

        ByteBuffer& buffer;
    };

    template <typename ByteBuffer>
    class deserializer
    {
    public:
        deserializer(ByteBuffer const& buffer, uint64_t offset = 0u) : buffer(buffer), m_Offset(offset){}

        template<typename Obj>
        constexpr void inline deserialize(Obj& object) requires is_byte_source<std::remove_cvref_t<ByteBuffer>>
        {
            using objType = std::remove_cvref_t<decltype(object)>;
            //对象如果是const的，需要去掉const
            auto& mutableObject = const_cast<objType&>(object);
            if constexpr (managed_pointer_traits<objType>::is_managed_pointer)
            {
                managed_pointer_traits<objType>::set_pointer_null(mutableObject);
            }
            else if constexpr (std::is_pointer_v<objType>)
            {
                //赋值空
                mutableObject = nullptr;
            }
            else if constexpr (managed_wrapper_traits<objType>::is_managed_wrapper)
            {
                typename managed_wrapper_traits<objType>::inner_type innerItem{};
                deserialize(innerItem);
                managed_wrapper_traits<objType>::set_data(mutableObject, innerItem);
            }
            else if constexpr (std::is_fundamental_v<objType> || std::is_enum_v<objType>)
            {
                load_from_buffer(mutableObject);
            }
            else if constexpr (containerStates<objType>::is_container_with_size)
            {
                deserialize_container_with_size(mutableObject);
            }
            else if constexpr (std::is_class_v<objType>)
            {
                visit_members(mutableObject, [&](auto &&...items) CONSTEXPR_INLINE_LAMBDA{
                    (deserialize(items), ...);
                }); //解包结构体
            }
        }
    private:

        template<typename Obj>
        constexpr void inline deserialize_container_with_size(Obj& object)
        {
            using objType = std::remove_cvref_t<decltype(object)>;
            using arrElemType = containerInfo<objType>::elementType;
            uint64_t objSize;
            load_from_buffer(objSize);
            if constexpr (containerStates<objType>::has_reserve)
            {
                object.reserve(static_cast<size_t>(objSize));
            }
            if constexpr (containerStates<objType>::has_push_back_element)
            {
                for(uint64_t i = 0; i < objSize; i++)
				{
					arrElemType item;
					deserialize(item);
					object.push_back(item);
				}
            }
            else if constexpr (containerStates<objType>::has_insert_element)
			{
                for (uint64_t i = 0; i < objSize; i++)
                {
                    arrElemType item;
                    deserialize(item);
                    object.insert(item);
                }
			}
            else if constexpr (containerStates<objType>::element_assignable)
            {
                if constexpr (containerStates<objType>::has_resize)
                {
                    object.resize(objSize);
                }
                if(objSize != containerInfo<objType>::container_size(object))
				{
                    return;
				}
                for (uint64_t i = 0; i < objSize; i++)
                {
                    arrElemType item;
                    deserialize(item);
                    object[i] = item;
                }
            }
            else
            {
                std::is_same<objType, castl::string> a{};
                static_assert(std::is_same<objType, castl::string>::value, "Wrong");
            }
        }

        template<typename Obj>
        constexpr void load_from_buffer(Obj& object)
        {
            using objType = std::remove_cvref_t<decltype(object)>;
            static_assert(std::is_trivially_copyable_v<objType>, "Object must be trivially copyable");
            load_from_buffer(&object, sizeof(objType));
        }

        constexpr void load_from_buffer(void* dest, size_t size)
        {
            memcpy(dest, buffer.data() + m_Offset, size);
            m_Offset += size;
        }

        ByteBuffer const& buffer;
        uint64_t m_Offset = 0;
    };

    template <typename Obj, typename ByteBuffer>
    static constexpr void serialize(ByteBuffer& buffer, Obj const& object)
    {
        serializer<ByteBuffer> srser{ buffer };
        srser.serialize(object);
    }

    template <typename Obj, typename ByteBuffer>
    static constexpr void deserialize(ByteBuffer const& buffer, Obj& object, uint64_t offset = 0u)
    {
        deserializer<ByteBuffer> desrser{ buffer, offset };
        desrser.deserialize(object);
    }
}