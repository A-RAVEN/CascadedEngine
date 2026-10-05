# 拒绝形状的读数（改前 / 改后）

本文件是「哪些形状必须拒绝、哪些必须支持」逐条的**实测读数**留档，配合 `Tools/CodeGen/README.md`
第五节的形状总表看：表里每一行在这里都能找到它是怎么被量出来的。命令都能原样跑（工具在
`out/build/x64-debug/bin/CodeGen.exe`），探针是仓库里的夹具。

## 一、本轮新增的拒绝（指针成员）

| 形状 | 探针 | 改之前 | 改之后 |
|---|---|---|---|
| 成员本身就是裸指针 | `Tools/CodeGen/TestShapes.h` 的 `RawPointerMember` | `--emit-serialization` **exit 0**（照常出码：老运行时对指针是"直接跳过"，数据静默丢失；新运行时没有这条跳过，生成的代码会去写这个地址） | **exit 15**，stderr 点名成员与类型：`member 'p' is a raw pointer ('int *')` |
| 成员本身就是托管指针 | 同文件的 `ManagedPointerMember` | `--emit-serialization` **exit 0** | **exit 15**：`member 'p' is a managed pointer ('std::shared_ptr<int>')` |

两条命令：

```
CodeGen.exe Tools/CodeGen/TestShapes.h RawPointerMember -I CACore/header --emit-serialization
CodeGen.exe Tools/CodeGen/TestShapes.h ManagedPointerMember -I CACore/header --emit-serialization
```

被拒绝后工具给出的替代做法（README 也写了同一句）：把要落盘的部分提成值语义结构，句柄写名字或索引，
另配一张随对象一起落盘的旁表。

## 二、已有的拒绝（退出码保持不变，逐条复量）

命令一律是 `CodeGen.exe Tools/CodeGen/TestShapes.h <类型> -I CACore/header --emit-serialization`：

| 类型 | 退出码 | stderr 点名 |
|---|---|---|
| `PointerElement`（容器元素是指针） | 15 | `member 'items' is a container whose element is a raw or managed pointer` |
| `MultiKey`（重复键容器） | 14 | `member 'm' is a duplicate-key container ('std::multimap<int, int>')` |
| `BitPacked`（`vector<bool>`） | 13 | `member 'v' is a bit-packed container ('std::vector<bool>')` |
| `NoDefaultElement`（元素不可默认构造） | 16 | `member 'v': the element type 'codegen_shapes::NoDefaultCtor' has no usable default constructor` |
| `NoOrderKeyMap`（无序容器排序侧没有全序） | 17 | `member 'm': an unordered container is sorted before it is written, but its sorting side has no ordering operator` |

对照（必须支持、必须 exit 0）：同文件的 `Supported`（标量 / 字符串 / 结构体 / 三种容器 / 定长数组）。

## 三、被撤除的那条拒绝（原退出码 12）

「容器的元素是内含无序容器的结构体」原来必须以 **exit 12** 拒绝并点名成员、元素类型与内层无序容器
（读数在归档的 `openspec/changes/archive/2026-10-02-codegen-container-semantics/` 里）。撤除后该形状
由递归读写接手：`Interface/RenderInterface/private/TestContainer.h` 的 `Inner` / `Outer` 就是那个夹具，
它在接入清单里，**构建期三个发射器都是 exit 0**、生成物逐字节与入库副本一致。

**还差一个读数**：撤除后要求"往返内容一致、且两个只有内层插入顺序不同的对象字节逐字节相同" ——
这条是运行期判据，由 `Test/CoreTests` 里那个夹具的用例给（对应任务 10.7 / 10.1）。本文件先记下
"出码成功"这半边，运行期读数在用例落地后补进同一节。

## 四、怎么复核

```
git grep -n -F "RawPointerMember"  -- Tools/CodeGen/TestShapes.h
python Tools/CodeGen/verify_readme.py        # README 里的命令原样跑一遍，0 处不符
```
