// 托管启动自检用的布局清单类型（`complete-csharp-window-system-bindings` 任务 3.1）。
//
// 生成物（`<接口>.Bindings.cs`）在 `<接口>Bindings` 里发一份 `LayoutManifest`：每个过界记录类型
// 的大小、每个字段的原始名字、C 镜像类型名与偏移。`Host.Boot` 拿它和引擎侧运行期清单（真实
// `sizeof` / `offsetof`）逐项比对，不一致就拒绝继续初始化 —— 这样"生成器算错布局"不会只靠
// C++ 的 `static_assert` 兜底，托管侧启动过程本身也有判据。
//
// 这里只放数据形状，不放任何 native 细节：字段全是 string / int，公开类型里不出现指针。
namespace Scripting.Managed
{
    /// <summary>布局清单里的一个字段：名字、C 镜像类型名、真实偏移。</summary>
    internal sealed class BindingLayoutField
    {
        public string Name;
        public string Type;
        public int Offset;
    }

    /// <summary>布局清单里的一个记录类型：C 镜像名、真实大小、字段列表（保持声明顺序）。</summary>
    internal sealed class BindingLayoutType
    {
        public string Name;
        public int Size;
        public BindingLayoutField[] Fields;
    }
}
