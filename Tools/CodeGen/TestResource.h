//
// TestResource.h — `TResource` 新旧字节对拍的**新侧**夹具。
//
// `codegen_resource::NewRes` 带 `CA_REFLECT`，读写由生成的 `CodeGenSerializer` 负责，并且像真实资源
// 一样继承 `resource_management::TResource<T>`（`TResource::Serialize` 把生成代码写出的字节**整段**
// 交给批）。
//
// 老侧（旧的批序列化器 + 老宏那种"按成员名逐个列出"的描述符）**不写在这里**：这个头是被 CodeGen 解析的
// 目标头，而那种写法在这套解析环境里判不出常量表达式（实测：该行每出现一次就报 6 条错误级诊断），
// 会让整行出码被拒（exit 6）。这也是"老的一侧"最后一次改写：三份旧头删掉之后，消费方
// `Test/CoreTests/ResourceBatchTest.cpp` 里的老字节已经写成期望值，不再需要老描述符。
#pragma once

#include "CodeGenMeta.h"
#include <CAResource/IResource.h>       // resource_management::TResource

namespace codegen_resource
{
    CA_REFLECT(serializable)
    struct NewRes : resource_management::TResource<NewRes>
    {
        int    a = 0;
        float  b = 0.0f;
        double c = 0.0;
    };
} // namespace codegen_resource
