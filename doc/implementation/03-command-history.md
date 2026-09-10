# 命令与历史系统实施方案

## 设计目标

同一编辑意图无论来自 Web UI、调试控制台还是自动化，都经过统一的强类型命令。命令执行具备原子性、确定性和可撤销性；查询与修改严格分开。

## 命令模型

```cpp
using Command = std::variant<
    InsertMove,
    DeleteSubtree,
    ReplaceMove,
    ReorderVariation,
    SetAnnotations,
    SetMetadata,
    CheckoutNode,
    CompoundCommand>;

struct CommandEnvelope {
  CommandId id;                         // 宿主生成，用于追踪/去重
  std::optional<std::uint64_t> expected_revision;
  Command payload;
};
```

首版在单进程内同步执行。`expected_revision` 可选，但 Worker 和未来远程调用应始终提供，用于拒绝基于旧视图的操作。`CommandId` 不写入 OXQ 文件，也不能代替 `NodeId`。

### 修改命令

| 命令 | 必要输入 | 成功结果 |
|---|---|---|
| `InsertMove` | parent、move、可选 sibling index | 新 NodeId |
| `DeleteSubtree` | node | 删除节点清单、下一选择节点 |
| `ReplaceMove` | node、move | 更新节点清单 |
| `ReorderVariation` | node、目标 index | 新 sibling 顺序 |
| `SetAnnotations` | node、完整 annotations | 更新节点 |
| `SetMetadata` | 字段 path、类型化值 | metadata changed |
| `CheckoutNode` | node | selection changed，不置 dirty |
| `CompoundCommand` | 有序子命令 | 一个历史项和一个 ChangeSet |

公开 API 不提供含义重叠的 `createVariation` 数据操作；它是 `InsertMove` 指定 sibling index 的便利构造器。这样 UI 和控制台不会形成两套分支语义。

## 执行流水线

```text
Envelope
  → 检查 CommandId / expected_revision
  → 解析目标并做前置校验
  → 构造逆向数据（不是只存反向命令参数）
  → 在隔离工作副本或事务日志上 apply
  → 文档不变量 + 策略校验
  → commit，revision + 1
  → 更新 history / dirty / cache
  → 发布一个 ChangeSet
```

对于小命令，可使用操作日志回滚；对于复杂子树命令，可先构造 `SubtreeSnapshot`。不得在公共可见模型上逐步执行后靠“尽量回滚”维持原子性。

## Undo/Redo 语义

- 每个成功的持久化命令进入 Undo 栈；纯导航命令默认不进入持久化历史；
- `CompoundCommand` 无论包含多少子命令，都作为一个历史项；
- Undo 恢复执行前的文档及与该动作相关的选择；
- Redo 恢复 Undo 前的结果，包含原 NodeId；
- Undo 后执行新的持久化命令立即清空 Redo 栈；
- 失败、no-op 命令不增加 revision、不改变历史；
- Undo 和 Redo 本身各使 revision 增加 1，revision 永不倒退；
- dirty 由“当前文档状态是否等于已保存 checkpoint”决定，不能简单设置为 `undo_stack 非空`。

### 保存 checkpoint

历史条目维护单调的 document-state token。成功保存 revision `R` 后设置 checkpoint token；Undo 恰好回到该 token 时 dirty 为 false，离开时为 true。若历史裁剪丢掉 checkpoint，dirty 保持 true，直到下一次成功保存。

## 历史存储与限制

`HistoryOptions` 至少包含 `max_entries` 和 `max_bytes`。字节数按快照、字符串和容器容量的可解释估算统计，不要求精确到 allocator 元数据。

裁剪规则：

1. 新历史项大于单项上限时，命令在修改前返回 `resource_limit`；
2. 提交后从最旧项开始裁剪，保留刚提交项；
3. 被裁剪条目引用的节点 ID 仍不得在当前会话复用；
4. 清空历史不改变文档和 dirty；
5. 历史默认只在内存中存在，不保存到 OXQ v1。

## 复合命令

复合命令用于粘贴一段变化、批量注释、复制分支等用户级动作：

- 子命令按顺序看到前一个子命令的结果；
- 任一子命令失败则全部回滚；
- 禁止循环嵌套或超过配置深度；
- 返回一个聚合结果和合并去重后的 ChangeSet；
- 复合命令内部不得独立发布事件或写历史。

## 幂等与重放

同一会话对已经成功处理的 `CommandId` 重复提交时，返回原结果摘要，不再执行。缓存可有界；ID 淘汰后依赖 `expected_revision` 防止迟到重放。调试日志可以记录命令 envelope，但包含用户棋谱文本时默认不输出完整内容。

命令日志不是持久化格式。跨版本重放只有在命令 schema 明确版本化后才可承诺。

## C++ 组件建议

```text
libs/oxq-editor/
├── include/oxq/editor/
│   ├── command.hpp
│   ├── editor_session.hpp
│   ├── error.hpp
│   ├── snapshot.hpp
│   └── version.hpp
└── src/
    ├── command_bus.cpp
    ├── commands/
    ├── history.cpp
    ├── position_cache.cpp
    └── session.cpp
```

命令实现放在 `.cpp` 内，公共 header 只暴露稳定值类型，降低 WASM 和第三方 consumer 的编译耦合。

## 测试与验收

- 每种命令覆盖成功、边界、目标不存在、revision 冲突和资源上限；
- 对任意成功序列，连续 Undo 到起点后文档与初始文档相等；再 Redo 后与最终文档相等；
- 使用生成式测试随机组合插入、删除、替换和排序，每步检查树不变量；
- 对每个注入失败点验证原子性、revision、history、cache 和事件均未泄漏部分状态；
- 同一命令序列在 GCC、Clang、MSVC 得到相同文档和 ChangeSet；
- 复合命令只产生一个 history entry 和一次状态事件。
