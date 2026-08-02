# Vulkan API 本地文档库

本目录是 Vulkan 后端审查与开发用的**离线** API 文档库。审查 workflow 的 subagent 禁止联网（CLAUDE.md 工作规则），外部正确性验证一律参考本目录。

## 目录结构

```
docs/vulkan-api-docs/
├── README.md                 ← 本文件
├── spec/
│   ├── validusage.json       ← VUID 权威（15MB，官方 registry 生成，含全部 VUID 原文）
│   ├── vk.xml                ← API/枚举/结构体权威（官方 registry XML）
│   └── vkspec.pdf            ← 完整 Vulkan 规范正文（官方 PDF）
└── refpages/                 ← 常用 API man pages（markdown，docs.vulkan.org refpages 转换）
    ├── vkCreateDevice.md
    ├── vkCmdPipelineBarrier.md
    ├── VkImageMemoryBarrier.md
    └── ...（25 页）
```

## 引用约定（审查时）

- **VUID 语义与编号真实性**：`spec/validusage.json`（grep `"VUID-xxx"`）与 `spec/vk.xml`（grep 结构体/枚举/函数）
- **API 签名、参数、Valid Usage 章节**：`refpages/<ApiName>.md`（如 `refpages/vkCmdPipelineBarrier.md`）
- **规范章节正文**（layout 匹配规则、同步模型等）：`spec/vkspec.pdf`
- 审查结果中的引用格式：`[vkCmdPipelineBarrier](docs/vulkan-api-docs/refpages/vkCmdPipelineBarrier.md)`、`[VUID-xxx](docs/vulkan-api-docs/spec/validusage.json)`

## 来源与更新流程

| 文件 | 来源 | 更新方式 |
|------|------|----------|
| validusage.json | 本地 Vulkan SDK `share/vulkan/registry/validusage.json`（1.4.350.0） | 复制 SDK 新版本对应文件 |
| vk.xml | 本地 Vulkan SDK `share/vulkan/registry/vk.xml` | 复制 SDK 新版本对应文件 |
| vkspec.pdf | https://registry.khronos.org/vulkan/specs/latest/pdf/vkspec.pdf | curl 下载 |
| refpages/*.md | https://docs.vulkan.org/refpages/latest/refpages/source/<name>.html | WebFetch 抓取转 markdown（`convert_refpage.py` 辅助清理） |

**文档库维护流程**（由主会话执行，不在审查 workflow 内）：
1. 审查中 subagent 报告"文档库缺失: <API 名>"时，主会话用 WebFetch 抓取对应 refpage 加入 `refpages/`
2. 新 Vulkan 版本发布时按上表刷新
3. 改动后同步更新本 README

## 已收录 refpages（25 页）

vkCreateDevice, vkAcquireNextImageKHR, vkQueuePresentKHR, VkSwapchainCreateInfoKHR, vkGetDeviceQueue, vkCreateCommandPool, vkCmdCopyBufferToImage, VkImageMemoryBarrier, vkCmdPipelineBarrier, VkBufferImageCopy, VkImageViewCreateInfo, VkImageCreateInfo, VkDescriptorImageInfo, VkWriteDescriptorSet, vkBindBufferMemory, vkDestroyPipeline, VkGraphicsPipelineCreateInfo, VkPipelineTessellationStateCreateInfo, VkPipelineMultisampleStateCreateInfo, vkCmdBindVertexBuffers, vkCmdDraw, vkResetDescriptorPool, VkDescriptorPoolCreateInfo, vkQueueSubmit, VkMemoryRequirements
