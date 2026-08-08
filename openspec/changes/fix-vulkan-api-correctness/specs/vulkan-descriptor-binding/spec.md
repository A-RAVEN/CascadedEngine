## ADDED Requirements

### Requirement: 描述符写入数量与 layout 声明一致

`vkUpdateDescriptorSets` 的写入 SHALL 与 descriptor set layout 声明的 `descriptorCount` 一致：layout 声明 `descriptorCount = elementCount`（数组绑定）时，写入 SHALL 覆盖全部元素（`descriptorCount=elementCount` 的单个 write，或逐元素多个 write），SHALL NOT 只写 `bindings[0]` 且 `descriptorCount=1`。`VkWriteDescriptorSet` 的 `descriptorType` SHALL 与 layout binding 的类型一致（buffer → `pBufferInfo`，image → `pImageInfo`）。

#### Scenario: 数组绑定（elementCount > 1）

- **GIVEN** shader 反射声明某绑定 `elementCount > 1`，layout `descriptorCount = elementCount`
- **WHEN** `BuildDescriptors` 写入描述符
- **THEN** 写入覆盖全部 `elementCount` 个元素
- **AND** 验证层不报告 descriptor 数量不匹配

#### Scenario: 单元素绑定

- **GIVEN** 绑定 `elementCount == 1`
- **WHEN** `BuildDescriptors` 写入描述符
- **THEN** `descriptorCount=1` 的写入即可，不重复写入

### Requirement: descriptor 的 imageLayout 必须与图像实际布局一致

每个 `VkDescriptorImageInfo.imageLayout` SHALL 与图像在采样时刻的实际 `VkImageLayout` 一致（VUID-VkDescriptorImageInfo-imageLayout-00344："descriptor 的 imageLayout 必须匹配采样时图像各子资源的实际布局"）。当前 barrier/布局跟踪链对采样图像统一转换到 `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`（compute/transfer/finalize 路径），因此 `BuildDescriptors` SHALL 对所有非 UAV sampled 绑定统一写 `SHADER_READ_ONLY_OPTIMAL`（R5-4 最终方案，与实际布局一致）。depth-stencil 格式的 sampled 绑定：view 的 aspectMask SHALL 只含单个 aspect bit（depth 或 stencil 之一，VUID-VkDescriptorImageInfo-imageView-01976），SHALL NOT 同时含两个 bit。

#### Scenario: 深度纹理采样绑定

- **GIVEN** 纹理为 depth（或 depth-stencil）格式，被 sampled 绑定，barrier 链将其转换到 `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`
- **WHEN** `BuildDescriptors` 写入 `VkDescriptorImageInfo`
- **THEN** `imageLayout` 为 `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`（与实际布局一致）
- **AND** view 的 aspectMask 为 depth 单 bit（不含 stencil）
- **AND** 验证层不报告 VUID-VkDescriptorImageInfo-imageLayout-00344 / imageView-01976

#### Scenario: 布局转换目标与描述符同步

- **GIVEN** 采样图像经 barrier 转换后的实际布局
- **WHEN** `BuildDescriptors` 写入该图像的描述符
- **THEN** `imageLayout` 与转换后的实际布局一致（当前统一为 `SHADER_READ_ONLY_OPTIMAL`）
- **AND** 若未来布局跟踪链改变转换目标，描述符写入必须同步更新
