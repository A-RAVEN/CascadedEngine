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
        constexpr void inline serialize(const Obj& object) requires is_bytebuffer<ByteBuffer>
        {
            using objType = std::remove_reference_t<decltype(object)>;
            if constexpr (std::is_fundamental_v<objType> || std::is_enum_v<objType>)
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
            for (auto& item : object)
            {
                serialize(item);
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
        deserializer(ByteBuffer const& buffer) : buffer(buffer) {}

        template<typename Obj>
        constexpr void inline deserialize(Obj& object) requires is_byte_source<ByteBuffer>
        {
            using objType = std::remove_cvref_t<decltype(object)>;
            //对象如果是const的，需要去掉const
            auto& mutableObject = const_cast<objType&>(object);
            if constexpr (std::is_fundamental_v<objType> || std::is_enum_v<objType>)
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
					//throw std::runtime_error("Container size mismatch");
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
                //static_assert(containerStates<castl::string>::element_assignable, "Wrong");
                std::is_same<objType, castl::string> a{};
                static_assert(std::is_same<objType, castl::string>::value, "Wrong");
                //static_assert(false, __FUNCSIG__ "Container type not supported");
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
            memcpy(dest, buffer.data() + offset, size);
            offset += size;
        }

        ByteBuffer const& buffer;
        size_t offset = 0;
    };

}