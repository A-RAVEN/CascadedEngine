// 托管启动自检用的布局清单类型（`complete-csharp-window-system-bindings` 任务 3.1）。
//
// 生成物（`<接口>.Bindings.cs`）在 `<接口>Bindings` 里发一份 `LayoutManifest`：每个过界记录类型
// 的大小、每个字段的原始名字、C 镜像类型名与偏移。`Host.Boot` 拿它和引擎侧运行期清单（真实
// `sizeof` / `offsetof`）逐项比对，不一致就拒绝继续初始化 —— 这样"生成器算错布局"不会只靠
// C++ 的 `static_assert` 兜底，托管侧启动过程本身也有判据。
//
// 这里只放数据形状，不放任何 native 细节：字段全是 string / int，公开类型里不出现指针。
using System;
using System.Reflection;
using System.Runtime.InteropServices;

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

    /// <summary>
    /// 公开记录 / 枚举的**真实 CLR 布局**自检（csharp-render-binding-foundations 任务 1.3 / 2.2）。
    ///
    /// 只比较生成器写出来的两组常量是假判据：两边一起错照样绿。这里读的是 `Marshal.SizeOf` /
    /// `Marshal.OffsetOf` 与反射拿到的真实字段顺序，和同源清单里的原生读数对不上就把类型与
    /// 字段名报出来。
    /// </summary>
    internal static class BindingLayoutCheck
    {
        /// <summary>
        /// 核对一个公开记录的 CLR 布局：真实大小、字段顺序、每个字段的真实偏移。
        /// 返回 null 表示通过；否则返回点名类型 / 字段 / 两个读数的错误文本。
        /// </summary>
        internal static string VerifyRecord(Type type, int size, params (string Field, int Offset)[] fields)
        {
            int realSize;
            try
            {
                realSize = Marshal.SizeOf(type);
            }
            catch (Exception ex)
            {
                return $"布局自检：{type.Name} 的 Marshal.SizeOf 抛异常 {ex.GetType().Name}: {ex.Message}";
            }
            if (realSize != size)
            {
                return $"布局自检：{type.Name} 的 CLR 大小 {realSize} 与清单 {size} 不一致";
            }
            FieldInfo[] actual = type.GetFields(BindingFlags.Public | BindingFlags.Instance);
            if (actual.Length != fields.Length)
            {
                return $"布局自检：{type.Name} 的公开实例字段数 {actual.Length} 与清单 {fields.Length} 不一致";
            }
            for (int index = 0; index < fields.Length; ++index)
            {
                if (actual[index].Name != fields[index].Field)
                {
                    return $"布局自检：{type.Name} 的第 {index} 个字段叫 {actual[index].Name}，清单写 {fields[index].Field}";
                }
                int offset;
                try
                {
                    offset = (int)Marshal.OffsetOf(type, fields[index].Field);
                }
                catch (Exception ex)
                {
                    return $"布局自检：{type.Name}.{fields[index].Field} 的 Marshal.OffsetOf 抛异常 {ex.GetType().Name}";
                }
                if (offset != fields[index].Offset)
                {
                    return $"布局自检：{type.Name}.{fields[index].Field} 的 CLR 偏移 {offset} 与清单 {fields[index].Offset} 不一致";
                }
            }
            return null;
        }

        /// <summary>
        /// 核对公开枚举：底层类型与每个常量的实际数值。返回 null 表示通过。
        /// </summary>
        internal static string VerifyEnum(Type type, Type underlying, params (string Name, ulong Value)[] values)
        {
            if (Enum.GetUnderlyingType(type) != underlying)
            {
                return $"枚举自检：{type.Name} 的底层类型是 {Enum.GetUnderlyingType(type).Name}，期望 {underlying.Name}";
            }
            foreach ((string name, ulong value) in values)
            {
                if (!Enum.IsDefined(type, name))
                {
                    return $"枚举自检：{type.Name} 缺少常量 {name}";
                }
                ulong actual = Convert.ToUInt64(Enum.Parse(type, name));
                if (actual != value)
                {
                    return $"枚举自检：{type.Name}.{name} 的值是 {actual}，期望 {value}";
                }
            }
            return null;
        }

        /// <summary>枚举是否带 `[Flags]`（用于 `EnumFlags<T>` 的枚举）。</summary>
        internal static bool HasFlagsAttribute(Type type)
        {
            return type.GetCustomAttribute<FlagsAttribute>() != null;
        }
    }
}
