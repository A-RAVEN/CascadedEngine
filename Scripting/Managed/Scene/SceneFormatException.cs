// 场景文件的格式 / 结构错误类型（csharp-scene-foundation 任务 1.3，design D3）。
//
// 抛点只有 `SceneCodec.Load`：JSON 解析失败 / 文件截断、formatVersion 缺失或不支持、
// 必填字段缺失、字段类型错。消息一律点名具体字段与文件路径，调用方据此能说清「哪个文件的哪个字段错」。
//
// 边界：文件读写本身的失败（目录不存在 / 读不到 / 写不进）走 `IOException`（消息里同样带路径名），
// 不套用本异常 —— 「文件内容不合格式」与「文件根本读不到」是两件事，混在一起会让上层无法区分。
using System;

namespace Scripting.Managed.Scene
{
    /// <summary>场景文件格式 / 结构错误。抛本异常时**不会**返回任何场景对象（design D3）。</summary>
    public sealed class SceneFormatException : Exception
    {
        public SceneFormatException(string message) : base(message)
        {
        }

        public SceneFormatException(string message, Exception innerException) : base(message, innerException)
        {
        }
    }
}
