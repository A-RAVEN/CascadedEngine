using System;
using System.Collections.Generic;
using Scripting.Generated.Bindings.ResourceManagement;

namespace Scripting.Managed.Assets
{
    /// <summary>
    /// C# 资产工厂基类（csharp-resource-asset-foundation design D4）：继承反向基类
    /// `ResourceFactoryBase`，并在 `Create()` 里把刚造的对象入队。
    ///
    /// 为什么要队列：`Create()` 无参数（既有契约），不知道路径；`AssetDatabase` 在门面
    /// `CreateResource` 返回后立即出队，才知道该填哪个对象。
    /// 为什么不是"最后造的那一个"：一次阶段里可能连造多个（贴图 + 网格），单值会丢前一个。
    /// 为什么不是按路径键：`Create()` 时路径还不知道（路径是门面 `CreateResource` 的参数）。
    ///
    /// 线程边界：反向代理拒绝非注册线程调用（错误码 1005，既有），本批全部操作在主循环，
    /// 所以队列不需要并发容器。
    /// </summary>
    public abstract class AssetFactoryBase : ResourceFactoryBase
    {
        // 所有资产工厂共用一条先进先出队列（按 `Create()` 的发生顺序），跨类型也保持顺序 ——
        // 验收要求"连造两种类型按序取回"（tasks 3.2 / 4.2）。
        private static readonly Queue<ResourceBase> Pending = new Queue<ResourceBase>();

        /// <summary>队列里还没被取走的对象数（阶段末必须为 0；造了没取 = C# 框架 bug）。</summary>
        public static int PendingCount => Pending.Count;

        /// <summary>`Create()` 里调用：把新对象交出去等待 `AssetDatabase` 出队。</summary>
        protected static void Enqueue(ResourceBase resource)
        {
            if (resource == null)
            {
                throw new ArgumentNullException(nameof(resource));
            }
            Pending.Enqueue(resource);
        }

        /// <summary>按先进先出取回一个对象；队列为空就是框架错位，必须显式失败。</summary>
        internal static ResourceBase Dequeue(string typeName)
        {
            if (Pending.Count == 0)
            {
                throw new InvalidOperationException(
                    $"资产工厂队列为空：类型 \"{typeName}\" 的 CreateResource 没有造出对象，或者对象已被取走");
            }
            ResourceBase resource = Pending.Dequeue();
            return resource;
        }

        /// <summary>测试用：清空队列（阶段开始时复位，避免上一个阶段残留干扰断言）。</summary>
        internal static void ClearPending()
        {
            Pending.Clear();
        }
    }
}
