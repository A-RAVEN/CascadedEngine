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

### Requirement: 深度-模板采样描述符布局合法

depth-stencil 格式纹理的 sampled 描述符，其 `VkDescriptorImageInfo.imageLayout` 与 view 的 `aspectMask` SHALL 合法：`VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` SHALL 仅用于非 depth 布局（该布局在 depth-stencil image 上对 sampled 描述符无效，见 VUID-VkDescriptorImageInfo-imageLayout-00344）；depth 采样 SHALL 使用 `VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL`（或 `GENERAL`），且 aspectMask 仅含实际采样的 aspect bit。

#### Scenario: 深度纹理采样绑定

- **GIVEN** 纹理为 depth（或 depth-stencil）格式且被 sampled 绑定
- **WHEN** `BuildDescriptors` 写入 `VkDescriptorImageInfo`
- **THEN** `imageLayout` 为 `VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL`（或 `GENERAL`）
- **AND** view 的 aspectMask 不含非法 bit 组合
- **AND** 验证层不报告 VUID-VkDescriptorImageInfo-imageLayout-00344
