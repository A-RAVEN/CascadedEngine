// 场景渲染失败（csharp-scene-foundation 任务 3.1 / design D4 的报错口径）。
//
// 抛点只有 `SceneRuntime.Tick` / `SceneRenderer`：没有可用的后端或窗口、着色器路径没登记、
// 顶点个数不对、建图或提交被拒。消息一律点名**是哪个实体的哪个资源**，宿主把它转成
// 「Tick 抛异常：…」+ 非零返回码（`Host.ErrException`），引擎侧从 `LastError()` 读得到。
//
// 与 `SceneFormatException` 分开：那是文件内容不合格式（加载期、无后端），
// 这是渲染期资源 / 后端问题 —— 两者调用方的处置完全不同，合在一起会让用例分不清是哪一类。
using System;

namespace Scripting.Managed.Scene
{
    /// <summary>场景渲染期的明确失败（不静默跳过实体、不假成功）。</summary>
    public sealed class SceneRenderException : Exception
    {
        public SceneRenderException(string message) : base(message)
        {
        }

        public SceneRenderException(string message, Exception innerException) : base(message, innerException)
        {
        }
    }
}
