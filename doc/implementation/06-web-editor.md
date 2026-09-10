# Web 棋谱编辑器实施方案

## 产品闭环

首版 Web 编辑器必须完成一个本地、离线可用的闭环：

```text
打开 OXQ → 浏览变化 → 落子/建分支 → 注释 → Undo/Redo
         → 校验 → 下载保存 → 重新打开结果一致
```

文件默认只在浏览器内处理，不上传服务器。外部 CBL 转换不是首个 Web 里程碑的必需能力；需要时由独立 converter WASM 或服务适配，不能让 UI 依赖 editor 内部实现。

## 前端边界

框架可在实施前选择 Vue 或 React，但下列边界不随框架变化：

```text
UI components
     │ intents / view models
     ▼
Application store ─────► File service
     │                       │
     │ typed requests        └── open/download/recent handles
     ▼
Editor SDK client
     │ postMessage
     ▼
Web Worker → WASM editor → format
```

- Components 不读取 WASM 内存、不拼 JSON 命令；
- store 保存 UI 投影和 pending 状态，不复制权威文档；
- SDK client 是所有编辑/查询的唯一入口；
- File service 负责浏览器文件 API、下载和未保存保护；
- 棋盘组件接收 PositionView，发出 `moveIntent(from, to)`。

## 页面布局

桌面首版包含：

- 中央棋盘：局面、选择、上一步和候选落点；
- 变化面板：树/图投影、主线和旁支、折叠、当前节点；
- 信息面板：棋手、赛事、结果等元数据；
- 注释面板：当前节点注释；
- Debug Console：可折叠，复用控制台协议；
- 顶部操作：打开、保存、校验、Undo、Redo。

窄屏将面板变为 tabs/drawer，但不改变信息架构。首版图形化变化使用虚拟化树布局；只有可见节点进入 DOM，禁止一次渲染全部大文档节点。

## 状态划分

| 状态 | 权威位置 | 示例 |
|---|---|---|
| 文档事实 | WASM session | 着法、注释、元数据 |
| 编辑状态 | WASM session | current node、revision、dirty、history |
| 服务状态 | SDK/store | Worker ready、pending request、fatal error |
| 纯 UI 状态 | store/component | 面板尺寸、缩放、折叠、主题 |

UI 不能乐观提交会改变棋谱的操作。命令 pending 时可显示预览，但只有 Worker 成功响应后才提交权威投影。这样 validation 失败或 revision 冲突不会造成棋盘与引擎分叉。

## 关键交互序列

### 落子

1. 用户选择来源和目标格；
2. UI 基于当前投影做即时形态检查；
3. store 发送 `InsertMove`，携带 current node 和 expected revision；
4. Worker 校验并提交；
5. 响应返回新 NodeId、局面摘要和 ChangeSet；
6. store 更新 current node、变化面板、dirty 和 Undo 状态；
7. 失败则清理预览并在棋盘附近显示结构化错误。

若父节点已有相同着法，`InsertMove` 返回 `duplicate_move` 与
`existingNodeId`，应用服务随即 checkout 到已有节点，不创建重复节点，也不写
Undo 历史。

### 打开文件

1. 用户选择文件，先在 UI 检查配置的大小上限；
2. ArrayBuffer 转移给 Worker；
3. Reader 解析并返回 diagnostics；
4. 错误时保留原 session，展示 code/path；
5. 成功后原子切换 session，再释放旧 session；
6. 对非规范化但可读文件显示警告，保存将规范化重写。

### 保存文件

1. 记录待保存 revision；
2. Worker 导出规范化 OXQ 字节；
3. File service 完成写入或下载；
4. 只有宿主确认写入成功后，以该 revision 调用 `markSaved`；
5. 若期间又有编辑，界面仍保持 dirty。

## 变化视图

首版布局输入是有序树：节点、父子边、主变化标记、当前路径和折叠状态。布局可在主线程的独立 Worker 或 UI 层完成，但不能改变语义顺序。

- 当前根到节点路径高亮；
- 主变化使用视觉主轴，旁支使用次级轨道；
- 拖动分支只生成 `ReorderVariation`，不直接改数组；
- “复制到此分支”构造一个 `CompoundCommand`；
- 不展示跨父共享边，避免制造尚不存在的 DAG 语义；
- 超过阈值时按子树懒加载并显示截断提示。

## 可访问性与输入

- 棋盘每格可聚焦，提供棋子、坐标、行棋方的可读标签；
- 键盘可移动焦点、选择棋子、确认/取消着法；
- 不只依赖红/绿色表达阵营或错误；
- Undo/Redo 快捷键尊重操作系统约定，并避免在文本输入中截获普通编辑；
- 动画遵循 `prefers-reduced-motion`；
- Console 和错误提示使用可被辅助技术感知的 live region，但避免每次 hover 播报。

## 失败恢复

- 可恢复命令错误留在当前 session；
- Worker fatal 时冻结编辑，允许下载最近一次已导出的恢复副本；
- store 周期性请求规范化或内部恢复快照需由性能测试决定，不能把历史明文写入远程存储；
- `beforeunload` 仅作最后提醒，主要保护是显眼的 dirty 状态和明确保存反馈；
- 打开新文件、关闭标签页或重置文档前处理未保存状态。

## Web 验收场景

- 鼠标和纯键盘均可完成打开、落子、建分支、注释、撤销、保存；
- 10 万节点合成文档可打开，变化面板不创建同量级 DOM 节点；
- 连续快速操作通过 revision 串行化，无丢步或乱序 UI；
- 保存期间继续编辑不会错误清除 dirty；
- 非规范化文件警告、畸形文件路径诊断和 Worker fatal 有独立 UI；
- Chrome、Firefox、Edge 当前支持版本完成端到端测试；Safari 支持策略在发布前单独声明；
- 所有核心流程在无网络环境运行。
