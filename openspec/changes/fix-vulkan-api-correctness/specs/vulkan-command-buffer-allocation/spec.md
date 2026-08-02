## ADDED Requirements

### Requirement: 命令池队列族索引必须合法

`VulkanCommandListManager` 创建 graphics/compute/transfer 命令池时使用的 `queueFamilyIndex` SHALL 为合法索引（≥ 0），SHALL NOT 以 -1（或 0xFFFFFFFF）传入 `vkCreateCommandPool`（VUID-vkCreateCommandPool-queueFamilyIndex-00367 要求索引必须属于命令池可用的队列族）。`QueueContext` SHALL 保证单通用族设备上 compute/transfer 族索引回退到 graphics 族；仍无法取得合法索引时，对应池创建 SHALL 失败并输出诊断，而非携带非法索引继续。

#### Scenario: 单通用族设备初始化

- **GIVEN** 设备只有一个同时具备 graphics/compute/transfer 能力的通用队列族
- **WHEN** `InitQueueCreationInfo` 分类队列族
- **THEN** `m_ComputeQueueFamilyIndex` 与 `m_TransferQueueFamilyIndex` 回退为该通用族索引（≥ 0）
- **AND** `vkGetDeviceQueue` 与 `vkCreateCommandPool` 均使用合法索引

#### Scenario: 独立 compute/transfer 族存在

- **GIVEN** 设备存在独立 compute 族与独立 transfer 族
- **WHEN** `InitQueueCreationInfo` 分类队列族
- **THEN** 优先使用独立族索引，不与 graphics 族混淆

#### Scenario: 队列族索引仍无效时初始化失败

- **GIVEN** 任一队列族索引最终为 -1
- **WHEN** `VulkanCommandListManager::Init` 创建对应命令池
- **THEN** 该池创建返回失败并输出 `CA_LOG_ERR` 诊断
- **AND** 不产生携带 -1 索引的 Vulkan 调用
