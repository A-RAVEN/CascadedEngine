## ADDED Requirements

### Requirement: InitializedState 拆分为 Image/Buffer 工厂
`VulkanResourceState` SHALL 提供 `InitializedImageState()` 和 `InitializedBufferState()` 两个独立工厂函数，各返回语义正确的 `isImage` 字段值。

#### Scenario: Buffer 初始状态的 isImage 为 false
- **WHEN** buffer 路径调用 `InitializedBufferState()` 作为缓存状态
- **THEN** 返回结构的 `isImage` 为 `false`，而非误导性的 `true`

#### Scenario: Image 初始状态的 isImage 为 true
- **WHEN** image 路径调用 `InitializedImageState()` 作为缓存状态
- **THEN** 返回结构的 `isImage` 为 `true`

### Requirement: TextureSamplerDescriptor::Create 正确设置 integerFormat
`TextureSamplerDescriptor::Create` 静态工厂 SHALL 将入参 `integerFormat` 的值赋给 `desc.integerFormat`，而非无条件设为 `false`。

#### Scenario: integerFormat=true 被正确传递
- **WHEN** 调用者传入 `integerFormat=true`
- **THEN** 返回的 descriptor 具有 `integerFormat=true`，对应 sampler 的 `unnormalizedCoordinates` 被正确设置
