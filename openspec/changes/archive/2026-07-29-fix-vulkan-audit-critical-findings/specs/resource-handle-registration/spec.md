## ADDED Requirements

### Requirement: RegisterTemporary* 调用点必须配对 Register*Handle
`CollectResources` 中每个 `RegisterTemporaryBuffer`/`RegisterTemporaryTexture` 调用点 SHALL 紧随其后调用对应的 `RegisterBufferHandle`/`RegisterTextureHandle`，利用已有的 `handleKey` 和返回的 `resourceId` 建立 Handle→ResourceID 映射。

#### Scenario: Render pass attachment texture 注册
- **WHEN** `CollectResources` 在 line 664 为 render pass attachment 调用 `RegisterTemporaryTexture`
- **THEN** 系统随之调用 `RegisterTextureHandle(ImageHandle(handleKey), resourceId)`，后续 `GetTextureView(attachment)` 返回有效 ImageView

#### Scenario: Index buffer 注册
- **WHEN** `CollectResources` 在 line 694 为 index buffer 调用 `RegisterTemporaryBuffer`
- **THEN** 系统随之调用 `RegisterBufferHandle(BufferHandle(handleKey), resourceId)`，后续 `RecordRenderPass` 中的 `bindIndexBuffer` 可获取有效 buffer

#### Scenario: Vertex buffer 注册
- **WHEN** `CollectResources` 在 line 705 的循环中为 vertex buffer 调用 `RegisterTemporaryBuffer`
- **THEN** 系统随之调用 `RegisterBufferHandle`，后续 `bindVertexBuffers` 可获取有效 buffer

#### Scenario: Transfer image 注册
- **WHEN** `CollectResources` 在 line 731 为 transfer target image 调用 `RegisterTemporaryTexture`
- **THEN** 系统随之调用 `RegisterTextureHandle`，后续 `RecordTransferPass` 可获取有效 VkImage

#### Scenario: Finalize pass image 注册
- **WHEN** `CollectResources` 在 line 745 为 finalize pass image 调用 `RegisterTemporaryTexture`
- **THEN** 系统随之调用 `RegisterTextureHandle`，后续该 image 可通过 Handle 查询

### Requirement: 已注册的 Internal 资源可通过 Handle 查询到有效句柄
`GetBuffer(BufferHandle)` 和 `GetTexture(TextureHandle)` SHALL 在 Handle 类型为 Internal 且已正确注册时，返回非空的 VkBuffer/VkImage 句柄。

#### Scenario: Internal buffer 查询成功
- **WHEN** `GetBuffer(BufferHandle)` 被调用，且该 Handle 已在 `CollectResources` 中注册
- **THEN** 返回对应的有效 VkBuffer（非 VK_NULL_HANDLE）

#### Scenario: Internal texture 查询成功
- **WHEN** `GetTexture(TextureHandle)` 被调用，且该 Handle 已在 `CollectResources` 中注册
- **THEN** 返回对应的有效 VkImage（非 VK_NULL_HANDLE）
