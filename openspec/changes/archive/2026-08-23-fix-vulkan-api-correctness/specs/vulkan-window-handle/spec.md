## ADDED Requirements

### Requirement: swapchain 创建参数必须与 surface capability 一致

`VulkanWindowHandle` 创建 swapchain 时 SHALL 依据 `vkGetPhysicalDeviceSurfaceCapabilitiesKHR` 返回的 `supportedUsageFlags` 与 `supportedCompositeAlpha` 校验/约束 `VkSwapchainCreateInfoKHR.imageUsage` 与 `compositeAlpha`。请求的 imageUsage SHALL 是 `supportedUsageFlags` 的子集（VUID-VkSwapchainCreateInfoKHR-imageUsage-01275），compositeAlpha SHALL 是 `supportedCompositeAlpha` 中受支持的一位（VUID-VkSwapchainCreateInfoKHR-compositeAlpha-01281）；不支持时 SHALL 回退到受支持的取值。

#### Scenario: surface 不支持 eTransferDst

- **GIVEN** `supportedUsageFlags` 不含 `VK_IMAGE_USAGE_TRANSFER_DST_BIT`
- **WHEN** 创建 swapchain
- **THEN** imageUsage 中移除 `eTransferDst`（或选择与 capability 相交的子集）

#### Scenario: eOpaque compositeAlpha 不受支持

- **GIVEN** `supportedCompositeAlpha` 不含 `VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR`
- **WHEN** 创建 swapchain
- **THEN** compositeAlpha 回退为 `supportedCompositeAlpha` 中首个受支持位

### Requirement: acquire/present 错误码必须可被调用方处理

`vkAcquireNextImageKHR` 与 `vkQueuePresentKHR` 的调用 SHALL 使 `VK_ERROR_OUT_OF_DATE_KHR`、`VK_ERROR_SURFACE_LOST_KHR`、`VK_ERROR_DEVICE_LOST` 等错误码以返回值（或对应异常类型）对调用方可见，SHALL NOT 因 vk.hpp throwing 模式成功码白名单缺失而提前抛出导致现有错误处理分支成为死代码。调用方对这些错误码的处理分支 SHALL 实际可达（resize/重建路径）。

#### Scenario: 窗口 resize 触发 OUT_OF_DATE

- **GIVEN** 窗口尺寸变化导致 swapchain 过期
- **WHEN** `acquireNextImageKHR` 返回 `VK_ERROR_OUT_OF_DATE_KHR`
- **THEN** 代码执行 swapchain 重建路径（而非未捕获异常终止）

#### Scenario: 呈现返回 OUT_OF_DATE

- **GIVEN** present 时 swapchain 已过期
- **WHEN** `presentKHR` 返回 `VK_ERROR_OUT_OF_DATE_KHR`
- **THEN** 调用方触发重建且不崩溃

### Requirement: 窗口同步信号量索引基准统一

acquire 与 present 对同一窗口的同步信号量 SHALL 使用同一索引基准（窗口位置）访问 `GetWindowSync`，SHALL NOT 一侧用窗口位置、另一侧用 `GetCurrentImageIndex()` 索引同一数组。

#### Scenario: 多 image swapchain 下信号量配对正确

- **GIVEN** swapchain imageCount > 1 且存在多窗口
- **WHEN** 每帧执行 acquire → render → present
- **THEN** 每个窗口使用的 acquire/present semaphore 与该窗口的同步槽位一一对应，无错配

### Requirement: backbuffer 描述符格式与 swapchain 实际格式一致

backbuffer（后台缓冲）纹理描述符的格式 SHALL 来自 swapchain 实际选择的 `m_Format`（经格式映射），SHALL NOT 硬编码 `B8G8R8A8_UNORM`。当 surface 回退到其它格式时描述符与真实 swapchain 格式保持一致。

#### Scenario: surface 仅提供非 B8G8R8A8 格式

- **GIVEN** `ChooseSurfaceFormat` 因不支持 B8G8R8A8 回退到 `availableFormats[0]`（如 RGB 或 HDR 格式）
- **WHEN** 创建 backbuffer 纹理描述符
- **THEN** 描述符格式与 swapchain `m_Format` 一致
