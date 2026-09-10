# 架构决策与边界

状态：实施基线。若要改变本文件中的“决定”，应新增 ADR，而不是直接让代码偏离文档。

## AD-001：`oxq-core` 更名为 `oxq-format`

### 决定

`oxq-format` 是 OXQ 文件格式的参考实现，只包含：

- 持久化文档模型；
- OXQ Reader、Writer 和 Codec；
- 结构、资源限制和可选状态重放校验；
- 与格式版本直接相关的产品信息。

编辑会话、历史、选择、临时变化、UI 状态、外部格式适配和 AI 分析不进入该库。

### 兼容策略

首个迁移版本同时导出新旧入口：

| 类型 | 新入口 | 兼容入口 | 退出时机 |
|---|---|---|---|
| CMake target | `OXQF::format` | `OXQF::core` | 下一个主版本 |
| 库文件 | `oxq-format` | 不再生成第二份二进制 | 立即 |
| Header | `oxq/format/*.hpp` | `oxq/core/*.hpp` 转发头 | 下一个主版本 |
| Namespace | `oxq::format` | `oxq::core` 别名/兼容声明 | 下一个主版本 |

不得复制实现形成两个库。旧入口必须解析到同一个实现，弃用信息写入 release note。当前版本已经是 `1.0.0`，因此删除旧入口只能发生在明确发布的下一个主版本。

## AD-002：持久化模型与编辑模型分离

### 决定

`format::GameDocument`（迁移期间可仍由 `GameModel` 兼容）是可序列化事实；`editor::EditorSession` 是运行时状态。打开文档时复制或移动到会话，保存时由会话导出文档快照。格式库不持有回调、历史栈或缓存。

### 不变量

- 根节点 ID 为 `0`，无父节点、无着法；
- 非根节点恰有一个父节点；
- 子节点顺序具有语义：第一个是当前主变化，其余为旁支；
- 已分配的 `NodeId` 在同一会话中不复用，包括 Undo 后；
- 每个成功命令结束后，文档树和编辑状态都满足不变量；
- 失败命令不产生可观察的部分修改。

## AD-003：首版 VariationGraph 是树视图

OXQ v1 的持久化结构是有序树。首版 `VariationGraph` 是该树的布局/查询投影，节点仍只有一个父节点。“合并分析分支”在首版指比较后把一段着法复制到目标分支，不建立共享子图。

真正的多父节点 DAG 会引入序列化、注释归属、删除语义和循环检测问题，必须通过新的格式扩展与 ADR 决定。编辑器 API 不使用暗示多父关系的字段，以免形成错误兼容承诺。

## AD-004：所有持久化修改走命令总线

UI、控制台和未来自动化都创建同一种强类型命令，经 `CommandBus` 执行。查询不进入 Undo 历史。命令成功后统一产生 `StateChanged` 事件；命令失败时不发布该事件。

这条规则不要求把鼠标悬停、面板宽度等瞬时 UI 状态也命令化。只有会影响保存结果或编辑导航一致性的状态由编辑引擎管理。

## AD-005：WASM 是发布目标

WASM 绑定是 `oxq-format` 与 `oxq-editor` 的适配层，不拥有领域规则。浏览器通过 Web Worker 调用单线程 WASM 实例；UI 与 Worker 只交换结构化消息和文件字节，不共享 C++ 指针。

## AD-006：Monorepo 渐进迁移

目标布局为 `libs/`、`apps/`、`bindings/`，但不进行一次性目录大搬迁。顺序是：

1. 先完成 target、namespace 和 include 的兼容更名；
2. 新模块直接建立在目标目录；
3. 旧模块在兼容窗口内移动，使用 `git mv` 保留历史；
4. 每一步都保持主分支可构建、可安装。

建议稳定后的布局：

```text
libs/oxq-format/{include,src}
libs/oxq-editor/{include,src}
libs/oxq-convert/{include,src}
apps/oxq-cli/
apps/oxq-web/
bindings/wasm/
test/{format,editor,convert,wasm,integration}/
```

## 决策检查

代码评审时应回答：

- 新类型是文件事实还是会话状态？
- 修改能否从 UI 绕过命令总线？
- 新依赖是否违反有向层次？
- 新能力能否无损投影到 OXQ v1？若不能，保存时如何明确报告？
- API 是否在 native 与 WASM 中给出一致结果和稳定错误码？
