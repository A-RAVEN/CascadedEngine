# 面 05：像素读回正控 `(255,0,0,255)` 与 `ReadCaptureCenterRed` —— 未发现阻断项

## 一、函数真实存在

`Test\GPUBackendTester\Main.cpp`（匿名命名空间内）：

```cpp
4575: 	// 渲染证明（⑨/⑪）：按读数行的 graphSelf 解析出离屏捕获纹理，Readback 它的中心像素并判红。
4576: 	// 容差与 D12 的屏幕捕获一致（R>200、G<60、B<60），背景是 (0,128,0) 的清除色，不会误判。
4577: 	bool ReadCaptureCenterRed(TestContext& ctx, HostProbeReadings const& readings, std::string& detail)
...
4596: 		auto token = ctx.pGPUBackend->Readback(objects.captureImage, pixels);
...
4614: 		// 判据（工件修订：与既有探针一致的精确像素，不放宽）：离屏捕获中心必须精确是三角形红。
4615: 		return r == 255 && g == 0 && b == 0 && a == 255;
```

判据是逐字节精确比较，不是“看起来红”。

## 二、被 `TestEditorWindowPanel` 真实调用

```cpp
4855: 	// ⑨ 渲染证明（首帧）：读数行 exec>0 且离屏捕获中心像素是三角形红。
4856: 	{
4857: 		std::string pixelDetail;
4858: 		const bool red = ReadCaptureCenterRed(ctx, readings, pixelDetail);
...
4863: 			fail("E_EDITOR_CAPTURE_PIXEL", "离屏捕获中心不是三角形红：" + pixelDetail);
```

`design.md` 说“像素读回惯例 (255,0,0,255) 精确匹配（editor-window-panel scene view 证明）”，指的就是这条：`TestEditorWindowPanel` 从托管侧读数行解析 `graphSelf`/`capture`，通过 `ctx.pGPUBackend->Readback` 读离屏捕获纹理中心，再与 `(255,0,0,255)` 精确比较。

## 三、`(255,0,0,255)` 在既有用例里多处出现

```text
Main.cpp
2340: 		const std::array<uint8_t, 4> pureRed{ 255, 0, 0, 255 };
2348: 	const std::array<uint8_t, 4> expectedCenter{ 255, 0, 0, 255 };
5534: 	// 合成/虚拟显示，headless 也可跑。判据：精确 (255,0,0,255)（按 backbuffer 格式判色序）。
7288: 		const std::array<uint8_t, 4> expectedCenter{ 255, 0, 0, 255 };
7340: 		const std::array<uint8_t, 4> expectedCenter{ 255, 0, 0, 255 };
```

## 四、注记（不构成阻断）

- `ReadCaptureCenterRed` 读的是“图对象 `captureImage`”的中心，走的是后端 `Readback`；spike 的渲染目标是新加的共享纹理，若它不由后端的 `GPUTexture` 句柄管理，这个函数不能原样复用。`tasks.md` 2.1 写“沿用现有探针像素惯例”，颜色惯例是真实的；函数是否可原样复用取决于共享纹理怎么建，这是实现细节，不是符号缺失。
- `tasks.md` 2.1 还要求“离屏读回中心 == (255,0,0,255)（原生侧正控）”，与 `ReadCaptureCenterRed` 的精确判据一致。

## 五、结论

本面未发现阻断项。`ReadCaptureCenterRed`、`(255,0,0,255)` 精确判据、`TestEditorWindowPanel` 的调用都真实存在。
