#pragma once

#include "CodeGenMeta.h"

namespace game
{
    // 外层的 RenderState 没有被标注，只有里面的 Stencil 被标注 —— 说明标记写在类体里时，
    // 工具认的是"这个类型自己"，与它套在谁里面无关。
    struct RenderState
    {
        CA_REFLECT(serializable)
        struct Stencil
        {
            int compareOp;
            int ref;
        };

        int depthTest;
    };
}
