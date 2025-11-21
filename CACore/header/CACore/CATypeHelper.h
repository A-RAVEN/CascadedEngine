#pragma once
#include <cstring>

namespace castl_typename_internal
{
    static const unsigned int FRONT_SIZE = sizeof("castl_typename_internal::GetTypeNameHelper<") - 1u;
    static const unsigned int BACK_SIZE = sizeof(">::GetTypeName") - 1u;

    template <typename T>
    struct GetTypeNameHelper
    {
        static const char* GetTypeName()
        {
			//CA_LOG("GetTypeName:[{}]", __FUNCTION__);
            static const size_t size = sizeof(__FUNCTION__) - FRONT_SIZE - BACK_SIZE;
            static char typeName[size] = {};
            memcpy(typeName, __FUNCTION__ + FRONT_SIZE, size - 1u);
            return typeName;
        }
    };
}

template <typename T>
const char* CAGetTypeName()
{
    return castl_typename_internal::GetTypeNameHelper<T>::GetTypeName();
}