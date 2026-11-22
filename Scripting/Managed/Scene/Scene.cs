// 场景数据模型（csharp-scene-foundation 任务 1.1）。
//
// 形状按 design D1 钉死：
//   Scene{ Name, Entities }
//   Entity{ Id, Name, Transform, MeshRenderer? }
//   Transform{ Position, Rotation, Scale —— 三个 float3 }
//   MeshRenderer{ ShaderPath, Color, 内联三角形顶点 }
// 首切片明确不做父子层级、不做通用组件容器、不做任意网格 / 相机 / 材质（见 design 的 Non-Goals）。
//
// 身份（design D1 / D7）：`Entity.Id` 是创建时生成的**持久 uuid 字符串**（Guid.NewGuid().ToString("D")），
// 保存再加载后逐字不变；**不复用编辑器运行期的整数编号**（`EditorService.s_nextObjectId` 是单调整数，
// 服务重启就变，不能当持久身份）。
//
// 顶点：`MeshRenderer.Vertices` 是**一个三角形**的局部坐标 —— 3 个顶点 × 3 个分量 = 9 个 float。
// 顶点色不逐顶点存：整个三角形用 `MeshRenderer.Color` 一个颜色（design D6「Color 作顶点色」）。
// 首切片一实体一三角形；任意网格留后续切片。
//
// Rotation 序列化保留、渲染暂不施加（design D6 的诚实登记，`SceneRenderer` 里只施加 Position/Scale）。
using System;
using System.Collections.Generic;
using Scripting.Generated.Bindings.GraphicsBackend;

namespace Scripting.Managed.Scene
{
    /// <summary>场景：一个名字 + 一串实体。首切片不含父子层级 / 场景设置 / 相机。</summary>
    public sealed class Scene
    {
        public string Name = string.Empty;
        public List<Entity> Entities = new List<Entity>();

        public static Scene Create(string name)
        {
            return new Scene { Name = name ?? string.Empty };
        }

        /// <summary>新建一个实体（持久 uuid 身份 + 单位 Transform）并加入本场景，返回它。</summary>
        public Entity AddEntity(string name)
        {
            Entity entity = Entity.Create(name);
            Entities.Add(entity);
            return entity;
        }
    }

    /// <summary>
    /// 实体：持久身份 + 名字 + 必有 Transform + 可选 MeshRenderer。
    /// 不做通用组件容器（首切片只有这两个具体成分，见 design D1）。
    /// </summary>
    public sealed class Entity
    {
        public string Id = string.Empty;
        public string Name = string.Empty;
        public Transform Transform = Transform.Identity();
        public MeshRenderer MeshRenderer;

        /// <summary>新建实体：id 是创建时生成的小写 uuid 字符串（"D" 格式，带连字符）。</summary>
        public static Entity Create(string name)
        {
            return new Entity
            {
                Id = Guid.NewGuid().ToString("D"),
                Name = name ?? string.Empty,
                Transform = Transform.Identity(),
            };
        }
    }

    /// <summary>
    /// 变换：三个 float3。Rotation 是欧拉角（弧度），首切片只序列化、渲染暂不施加（design D6）。
    /// </summary>
    public sealed class Transform
    {
        public float3 Position;
        public float3 Rotation;
        public float3 Scale;

        /// <summary>单位变换：位置零、旋转零、缩放 1。</summary>
        public static Transform Identity()
        {
            return new Transform
            {
                Position = SceneValues.Vec(0.0f, 0.0f, 0.0f),
                Rotation = SceneValues.Vec(0.0f, 0.0f, 0.0f),
                Scale = SceneValues.Vec(1.0f, 1.0f, 1.0f),
            };
        }
    }

    /// <summary>
    /// 网格渲染成分：着色器路径 + 颜色 + 内联三角形顶点（9 个 float）。
    /// 资源只按**名字**持久化（design D4）：这里没有任何句柄字段 —— 句柄是进程内代际值，不可持久化。
    /// </summary>
    public sealed class MeshRenderer
    {
        /// <summary>一个三角形 = 3 个顶点（首切片固定；任意网格留后续切片）。</summary>
        public const int VertexCount = 3;

        /// <summary>每个顶点 3 个分量（x / y / z）。顶点色由 <see cref="Color"/> 统一给。</summary>
        public const int ComponentsPerVertex = 3;

        /// <summary>内联顶点的 float 个数：3 顶点 × 3 分量。</summary>
        public const int VertexFloatCount = VertexCount * ComponentsPerVertex;

        public string ShaderPath = string.Empty;
        public float3 Color;
        public float[] Vertices = DefaultTriangle();

        public static MeshRenderer Create(string shaderPath, float3 color)
        {
            return new MeshRenderer
            {
                ShaderPath = shaderPath ?? string.Empty,
                Color = color,
                Vertices = DefaultTriangle(),
            };
        }

        /// <summary>
        /// 首切片的默认三角形局部坐标（与既有 `TestSimpleTriangle` 夹具同形）：
        /// (-0.25,-0.25,0) / (0.25,-0.25,0) / (0,0.5,0)。
        /// </summary>
        public static float[] DefaultTriangle()
        {
            return new[]
            {
                -0.25f, -0.25f, 0.0f,
                 0.25f, -0.25f, 0.0f,
                 0.00f,  0.50f, 0.0f,
            };
        }
    }

    /// <summary>
    /// 建 float3 的便捷入口：生成物里的 `float3` 是 struct，`new float3(...)` 没有三参数构造。
    /// 值都是普通 `public float` 字段（见 out/build/&lt;preset&gt;/codegen/bindings/graphics_backend__float3.Bindings.cs）。
    /// </summary>
    public static class SceneValues
    {
        public static float3 Vec(float x, float y, float z)
        {
            return new float3 { x = x, y = y, z = z };
        }
    }
}
