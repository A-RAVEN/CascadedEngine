# VkPipelineMultisampleStateCreateInfo(3)

## Name

VkPipelineMultisampleStateCreateInfo — 描述新建管线多重采样（multisample）状态的参数结构。

## C Specification

```c
// Provided by VK_VERSION_1_0
typedef struct VkPipelineMultisampleStateCreateInfo {
    VkStructureType                          sType;
    const void*                              pNext;
    VkPipelineMultisampleStateCreateFlags    flags;
    VkSampleCountFlagBits                    rasterizationSamples;
    VkBool32                                 sampleShadingEnable;
    float                                    minSampleShading;
    const VkSampleMask*                      pSampleMask;
    VkBool32                                 alphaToCoverageEnable;
    VkBool32                                 alphaToOneEnable;
} VkPipelineMultisampleStateCreateInfo;
```

## Members

| 成员 | 说明 |
|------|------|
| `sType` | 标识该结构的 `VkStructureType` 值。 |
| `pNext` | `NULL` 或指向扩展该结构的结构体指针。 |
| `flags` | 保留供将来使用。 |
| `rasterizationSamples` | 指定光栅化所用采样数的 `VkSampleCountFlagBits` 值。若管线以 `VK_DYNAMIC_STATE_RASTERIZATION_SAMPLES_EXT` 动态状态创建，该值在设置光栅化采样数时被忽略；但若未设置 `VK_DYNAMIC_STATE_SAMPLE_MASK_EXT`，它仍用于定义 `pSampleMask` 数组的大小（见下文）。 |
| `sampleShadingEnable` | 可用于启用 Sample Shading。 |
| `minSampleShading` | 当 `sampleShadingEnable` 为 `VK_TRUE` 时，指定采样着色（sample shading）的最小比例。 |
| `pSampleMask` | 指向用于采样掩码测试（sample mask test）的 `VkSampleMask` 值数组的指针。 |
| `alphaToCoverageEnable` | 控制是否根据片元第一个颜色输出的 alpha 分量生成临时覆盖率值。 |
| `alphaToOneEnable` | 控制是否将片元第一个颜色输出的 alpha 分量替换为 1。 |

## Description

采样掩码中的每个位与唯一的采样索引（sample index）相关联。掩码字 `w` 中的位 `b` 对应采样索引 `i = 32 × w + b`。`pSampleMask` 的长度为 ⌈ `rasterizationSamples` / 32 ⌉ 个字。

若 `pSampleMask` 为 `NULL`，则视为掩码所有位均为 `1`。

## Valid Usage

以下 VUID 条目均未省略：

1. `VUID-VkPipelineMultisampleStateCreateInfo-sampleShadingEnable-00784` — 若未启用 `sampleRateShading` 特性，`sampleShadingEnable` 必须为 `VK_FALSE`。
2. `VUID-VkPipelineMultisampleStateCreateInfo-alphaToOneEnable-00785` — 若未启用 `alphaToOne` 特性，`alphaToOneEnable` 必须为 `VK_FALSE`。
3. `VUID-VkPipelineMultisampleStateCreateInfo-minSampleShading-00786` — `minSampleShading` 必须在 `[0,1]` 范围内。
4. `VUID-VkPipelineMultisampleStateCreateInfo-rasterizationSamples-01415` — 若启用 `VK_NV_framebuffer_mixed_samples` 扩展，且未启用 `coverageReductionMode` 特性，或 `pNext` 链不含 `VkPipelineCoverageReductionStateCreateInfoNV`，或其 `coverageReductionMode` 不是 `VK_COVERAGE_REDUCTION_MODE_TRUNCATE_NV`，且子通道有颜色附件、`rasterizationSamples` 大于颜色采样数，则不得启用采样着色（sample shading）。
5. `VUID-VkPipelineMultisampleStateCreateInfo-sType-sType` — `sType` 必须为 `VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO`。
6. `VUID-VkPipelineMultisampleStateCreateInfo-pNext-pNext` — `pNext` 链中每个结构的 `pNext` 成员必须为 `NULL` 或指向以下任一有效实例：`VkPipelineCoverageModulationStateCreateInfoNV`、`VkPipelineCoverageReductionStateCreateInfoNV`、`VkPipelineCoverageToColorStateCreateInfoNV`、`VkPipelineSampleLocationsStateCreateInfoEXT`。
7. `VUID-VkPipelineMultisampleStateCreateInfo-sType-unique` — `pNext` 链中每个结构的 `sType` 值必须唯一。
8. `VUID-VkPipelineMultisampleStateCreateInfo-flags-zerobitmask` — `flags` 必须为 `0`。
9. `VUID-VkPipelineMultisampleStateCreateInfo-rasterizationSamples-parameter` — `rasterizationSamples` 必须是有效的 `VkSampleCountFlagBits` 值。

## See Also

- `VK_VERSION_1_0`
- `VkBool32`
- `VkGraphicsPipelineCreateInfo`
- `VkPipelineMultisampleStateCreateFlags`
- `VkSampleCountFlagBits`
- `VkSampleMask`
- `VkStructureType`

## Document Notes

更多信息参见 Vulkan Specification 中的 `VkPipelineMultisampleStateCreateInfo` 章节。本页摘自 Vulkan Specification，修改应提交至 Specification 而非此处。

---
*Source: https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineMultisampleStateCreateInfo.html (crawled via WebFetch, 2026-07-19)*

该页面为结构体文档，不涉及函数调用，因此没有返回值（Return Values）或错误码（Error Codes）章节。所有 9 条 VUID 条目均已列出，未作省略。
