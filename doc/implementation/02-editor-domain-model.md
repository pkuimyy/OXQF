# oxq-editor 领域模型实施方案

## 目标与边界

`oxq-editor` 是无 UI 的 C++20 编辑引擎。它接收 `format::GameDocument`，提供安全修改、导航、历史和查询，并导出可由 `oxq-format` 保存的文档。

首版不实现完整象棋规则裁决。当前格式层状态校验只检查来源有子、行棋方和同色占位等基本状态，不验证车马炮走法、将军或困毙。编辑器必须显式暴露校验级别，不得把“结构可保存”描述成“棋规合法”。

## 核心类型

```cpp
namespace oxq::editor {

enum class ValidationPolicy {
  structural,       // 可形成有效 OXQ 文档
  state_consistent  // 加上现有状态重放检查
};

struct Selection {
  format::NodeId current{0};
  std::optional<std::uint8_t> board_square;
};

struct SessionState {
  Selection selection;
  bool dirty{false};
  std::uint64_t revision{0};
  bool can_undo{false};
  bool can_redo{false};
};

class EditorSession {
 public:
  static Result<EditorSession> open(format::GameDocument document,
                                    SessionOptions options = {});
  CommandResult execute(Command command);
  Result<void> undo();
  Result<void> redo();
  Result<void> checkout(format::NodeId node);
  Result<void> mark_saved(std::uint64_t revision);
  SessionSnapshot snapshot() const;
  const format::GameDocument& document() const noexcept;
  format::GameDocument export_document() const;
};
}
```

具体返回容器可用项目统一的 `std::variant` 风格实现；公共接口不得靠异常表达用户输入错误。资源耗尽或内部不变量破坏可作为单独的 fatal 类错误。

## 会话所有权

- 一个 `EditorSession` 独占一个可变文档；
- `open` 验证文档，失败时不创建半有效会话；
- `document()` 只读，所有写操作走 `execute`；
- `snapshot()` 是 UI 读取模型，不能反向转换为可变 C++ 引用；
- `export_document()` 不清除 dirty；只有宿主确认保存成功后调用 `mark_saved(revision)`；
- `mark_saved` 只在 revision 仍匹配时清除 dirty，防止异步保存覆盖后续编辑。

## 节点与变化语义

持久化的 `children` 顺序定义变化优先级：

- `children[0]` 是主变化；
- 其余是旁支，顺序即显示顺序；
- `insert_move(parent, move, index)` 在指定位置创建子节点；
- `create_variation(parent, first_move)` 本质是插入非首位子节点；
- `promote_variation(node)` 只调整父节点的 children 顺序；
- `delete_subtree(node)` 删除节点及后代，根节点不可删除；
- `replace_move(node, move)` 默认只替换该节点的着法，并对整棵后代做状态一致性校验。

同一父节点下不允许出现完全相同的 `from_square/to_square` 子着法。原始
`InsertMove` 返回 `duplicate_move` 和已有 NodeId，调用方可以随后 checkout；
面向用户的“落子”应用服务可把“发现已有着法并导航”封装成一次意图，但不能
伪造一次文档修改或历史项。

若替换导致后代失效，命令整体失败。首版不提供“替换并自动截断后代”这种隐式数据丢失行为；需要时必须是名称明确的独立命令并返回被删除节点清单。

## NodeId 生命周期

`NodeId` 是文档内身份，不是数组下标。

- 根固定为 `0`；
- 会话中单调分配且永不复用；
- 删除命令把完整子树保存在历史条目中；
- Undo 恢复原 ID、原 sibling index、注释和后代；
- 从文件重新打开后，只保证该文件内 ID 的含义；
- Web 层把 64 位 ID 作为十进制字符串传输，避免 JavaScript Number 精度损失。

当前 `MoveTree::restoreNode` 只能恢复无子节点节点，不能单独满足子树 Undo。编辑器实现应提供内部 `SubtreeSnapshot`，按父先子后恢复，并在提交前一次性校验；不要把半恢复状态暴露给观察者。

## 局面缓存

查询任意节点局面会成为高频操作。首版采用按 revision 失效的惰性缓存：

```text
initial_position --apply path--> PositionAt(node)
                         │
                         └── cache[node] = {revision, position}
```

- 插入叶子只新增相关缓存；
- 替换或删除使目标子树缓存失效；
- Undo/Redo 后按受影响子树失效；
- 缓存不序列化、不进入命令历史、不影响相等性；
- 先保证正确性，再依据基准决定是否加入祖先跳表或增量棋盘。

当前 native API 通过 `EditorSession::position_at(NodeId)` 查询任意节点，并以
`PositionCacheStats` 暴露命中、未命中和当前条目数，便于基准与诊断。缓存默认最多
4096 项，可通过 `SessionOptions::max_position_cache_entries` 调整或设为 0 禁用；
达到上限时整批淘汰，以保持实现确定且有界。删除和替换只失效受影响子树，插入、
注释、元数据、分支排序和 checkout 保留仍有效条目，复合命令使用保守的整体失效。

## 快照与事件

`SessionSnapshot` 至少包含：revision、current node、dirty、undo/redo 能力、当前局面、当前节点摘要和树投影。大文档不应在每次事件复制全部文档；事件包含变化集合：

```cpp
struct ChangeSet {
  std::uint64_t before_revision;
  std::uint64_t after_revision;
  std::vector<NodeId> inserted;
  std::vector<NodeId> removed;
  std::vector<NodeId> updated;
  std::vector<NodeId> reordered_parents;
  bool metadata_changed;
  bool selection_changed;
};
```

事件在命令成功提交后同步产生。宿主可随后投递到 Worker/UI 消息队列，领域层本身不依赖浏览器事件循环。

首版 `VariationGraphProjection` 严格保持 OXQ 有序树语义，不创建共享子图。投影以
preorder 返回节点和父子边，携带相对深度、原 sibling index、主变化标记和当前路径
标记。`VariationGraphQuery` 支持子树起点、最大深度和最大节点数；会话级
`max_projection_nodes` 默认 10,000，达到任一边界时返回 `truncated=true`，而不是
构造无界快照。`SessionSnapshot` 包含当前节点摘要及默认树投影；更大的 UI 视口应直接
使用分页/截断查询。

## 错误分类

| 类别 | 示例 | 是否改变状态 |
|---|---|---|
| `invalid_argument` | square 超界、根节点有着法 | 否 |
| `not_found` | NodeId 不存在 | 否 |
| `conflict` | 预期 revision 已过期 | 否 |
| `validation_failed` | 替换后后代状态不一致 | 否 |
| `history_empty` | 无可 Undo/Redo 项 | 否 |
| `resource_limit` | 节点数或历史字节上限 | 否 |
| `internal_invariant` | 提交后树不变量失败 | 回滚并进入不可继续状态 |

错误携带稳定 code、用户可读 message、可选 path/node/revision；UI 不解析英文 message 做分支判断。

## 最小测试清单

- 空文档打开与根节点选择；
- 插入主线、并列分支、嵌套分支及稳定顺序；
- 删除/Undo 恢复完整子树和原 ID；
- 替换使后代失效时原子失败；
- checkout 不修改 dirty，持久化命令修改 dirty；
- 新命令在 Undo 后执行会清空 Redo；
- `mark_saved` 的 revision 竞争；
- 缓存命中、子树失效和 Undo 后一致性；
- 1,000,000 层深树操作不依赖递归调用栈；
- 导出后经 Writer→Reader 往返保持语义等价。
