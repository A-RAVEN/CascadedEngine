# 本面未发现 blocking——⑤源增删事件

本面（验收可失败性 · ⑤源增删 assets-changed）本轮出了 2 条主张，最高 severity 为 `misleading`，没有 Blocking：

| 主张 | severity | 一句话 |
|---|---|---|
| 主张-验收-011 | misleading | 「源增删→assets-changed」没钉事件条数、fields、以及「无变化不广播」的负控；现有 TestEditorAssetBrowser 已有正确写法（恰好 1 条 + added=1/removed=0），新用例没沿用。 |
| 主张-验收-012 | misleading | 没有要求 .slang 与 .cs 两条源根各做增删；一个提供者缓存、另一个重扫时断言不会红。 |

为什么不构成 Blocking：只要真做一次源文件增删，「收不到 assets-changed」就是能红的（`BroadcastAssetsChangedIfNeeded` 只在路径集合变化时广播）；问题在于判据偏松（可被「每次枚举都广播」或「只覆盖一条源根」骗过），属于加严，不是「永远不可能红」。按主张-验收-011/012 的修订即可。

本轮禁跑 tester，未实测事件，只核工件与现有协议语义（证据见 `证据-验收-011.md`、`证据-验收-012.md`）。
