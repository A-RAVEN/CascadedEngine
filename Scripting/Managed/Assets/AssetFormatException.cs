using System;
using System.IO;

namespace Scripting.Managed.Assets
{
    /// <summary>
    /// C# 资产格式错误（csharp-resource-asset-foundation design D5/D6）：魔数不符 / 版本不是 1 /
    /// 长度越界 / 文件缺失与 IO 错误都抛这个类型。消息必须点名路径与原因 ——
    /// MUST NOT 静默返回空对象或把垃圾灌进资产。
    ///
    /// 文件缺失 / IO 错误必须**包装**成这个类型（`InnerException` 保留原始异常）：直接裸抛
    /// `FileNotFoundException` 会让"异常类型断言"这条判据被喂绿（见 tasks 3.4 的修订记录）。
    /// </summary>
    public sealed class AssetFormatException : Exception
    {
        /// <summary>出错的资产路径（相对资源根，与调用方传入的一致）。</summary>
        public string AssetPath { get; }

        public AssetFormatException(string assetPath, string reason)
            : base($"资产 \"{assetPath}\"：{reason}")
        {
            AssetPath = assetPath;
        }

        public AssetFormatException(string assetPath, string reason, Exception innerException)
            : base($"资产 \"{assetPath}\"：{reason}", innerException)
        {
            AssetPath = assetPath;
        }
    }
}
