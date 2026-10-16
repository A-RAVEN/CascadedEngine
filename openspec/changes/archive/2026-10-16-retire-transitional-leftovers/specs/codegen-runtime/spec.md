## MODIFIED Requirements

### Requirement: 生成物的读写必须自带游标

CodeGen 生成的序列化代码 MUST NOT 依赖任何"把读位置藏在对象内部"的设施。读位置的 MUST 由调用者显式持有并在递归调用之间传递，使生成代码能在字节流的任意位置调用**另一个类型**的生成代码并继续读下去。

生成物 MUST NOT 引用 `cacore::serializer` / `cacore::deserializer`。这两个符号已随旧头删除（口径：除 `Tools/CodeGen/check_legacy_symbols.py` 的守卫符号表以外，源码树 0 处），所以这一条是**防回流**的守卫，不是"还有东西要迁"的待办。

#### Scenario: 容器里的结构体由它自己的生成代码读

- **WHEN** 一个容器的元素是另一个已用 CodeGen 标注的结构体，且该结构体内部含无序容器（`std::vector<Inner>`，`Inner` 内有 `unordered_map`）
- **THEN** 生成代码在读到该元素时把自己持有的读位置交给 `Inner` 的生成代码，读完后位置继续往后；同一内容、不同内层插入顺序的两个对象，写出的字节逐字节相同、读回的内容与写出内容一致，**不再以非零退出码拒绝**

#### Scenario: 落盘字节与冻结的期望字节逐字节一致

- **WHEN** 同一个对象用生成物（新读写）写一遍，再与测试里**冻结成字面量**的期望字节比对：`Test/CoreTests/CodeGenRuntimeTest.cpp` 的 `kOldSampleHex` 字面量与读它的那一行 `hexToBytes(kOldSampleHex)`
- **THEN** 两份字节逐字节相同 —— 旧运行时已删除，跑不出"与旧方案（`cacore::serializer` 逐成员写）对拍"，对照物因此改成入库的字面量；那些字面量是迁移期实测得到的（不是手算），改动它们等于改协议，MUST 单独说明理由

#### Scenario: 截断的载荷不会读越界

- **WHEN** 读一个被截断的载荷
- **THEN** 读对象在越界前失败（不读长度之外的内存），且失败可被调用方观察到
