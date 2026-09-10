# 路线图与工作包

## 排期原则

路线图按依赖和可验收增量组织，不绑定具体人日。每个工作包应在任务系统中继续拆成不超过一个评审单元的 issue；编号用于保持依赖关系。

## 里程碑总览

| 里程碑 | 结果 | 前置 |
|---|---|---|
| M0 基线冻结 | 可比较、可回滚的 1.0.0 基线 | 无 |
| M1 oxq-format | 新格式命名与旧 API 兼容 | M0 |
| M2 editor alpha | native 编辑、历史、控制台闭环 | M1 |
| M3 WASM SDK | 浏览器 Worker 中的同等能力 | M2 |
| M4 web MVP | 本地打开—编辑—保存闭环 | M3 |
| M5 稳定化 | 性能、兼容、发布与文档完成 | M4 |

## M0：基线冻结

### WP-001 记录公共面

- 归档当前 CMake exports、安装树和 public headers；
- 保存规范化 Writer 的跨平台指纹；
- 确认 OXQ vectors、CBL gold baseline、CLI JSON 合约均为绿色；
- 建立旧版本独立 consumer fixture。

验收：能明确判断后续变更是否改变 API、ABI、文件字节或 CLI 行为。

### WP-002 建立 ADR 流程

- 创建 ADR 模板，字段含状态、背景、决定、后果、兼容和替代方案；
- 把本实施基线的六项架构决定登记为 accepted；
- 约定格式变更、依赖反转和破坏性 API 变更必须有 ADR。

## M1：oxq-format

### WP-101 新 target 与安装导出

实现 `oxq_format` / `OXQF::format`，保留旧 target 兼容，新增新旧 clean-consumer 测试。

### WP-102 include 与 namespace 迁移

建立 `oxq/format` 公共头；仓内调用迁移；旧头转发。更名过程不改变 Reader/Writer 行为。

### WP-103 `GameDocument` 命名

先用兼容别名落地新名，再逐步更新签名和文档，禁止长期存在两份可分叉模型。

### WP-104 目录迁移

使用独立移动提交迁入 `libs/oxq-format`，修复构建、脚本、install 和 package 清单。

M1 出口：新 consumer 只见 `format`，旧 consumer 仍通过，Writer 指纹与 1.0.0 相同。

## M2：editor alpha

状态：native `oxq-editor` 工作包 WP-201 至 WP-206 已于 2026-09-10 完成实现；
真实 OXQ 的 Reader → Editor → Undo/Redo → Writer → Reader 路径、安装后 consumer、
GCC Debug/Release、Clang ASan/UBSan 与 Console parser fuzz 均已有自动化门禁。
Windows MSVC 由现有 CI matrix 持续验证。

### WP-201 库骨架与错误模型

- 新建 `oxq-editor` target、public headers、版本接口；
- 定义 Result、错误 domain/code、SessionOptions；
- 增加 header smoke 和安装 consumer。

### WP-202 EditorSession 与只读查询

- open/export/document/snapshot；
- current node checkout；
- 节点、路径和局面查询；
- revision、dirty/checkpoint 基础状态。

### WP-203 基础命令

按顺序实现 InsertMove、DeleteSubtree、ReplaceMove、ReorderVariation、SetAnnotations、SetMetadata，每个命令先完成原子性测试。

### WP-204 历史与复合命令

实现 Undo/Redo、子树快照、保存 checkpoint、历史限制和 CompoundCommand；增加随机序列性质测试。

### WP-205 局面缓存与 ChangeSet

先建立无缓存基准真值，再实现按 revision/子树失效缓存；提供增量 ChangeSet。

### WP-206 Debug Console

实现 tokenizer、parser、registry、查询/命令 dispatch 和 text/JSON renderer；增加 fuzz target。

M2 出口：native 测试程序可以打开真实 OXQ、完成所有首版编辑、任意撤销重做并写回可读的规范化文件。

## M3：WASM SDK

### WP-301 工具链与 C ABI

锁定 Emscripten，建立句柄 registry、buffer ownership、错误转换和 ABI version。

### WP-302 Worker 协议

实现 versioned request/response、按 session 串行化、transferable 文件和 fatal recovery。

### WP-303 TypeScript SDK

交付 ESM、类型声明、branded NodeId/Revision、生命周期和 clean-consumer example。

### WP-304 一致性与内存测试

同 fixture 对比 native/WASM；覆盖大 NodeId、反复 open/close、Worker terminate、过期 revision 和导出回读。

M3 出口：无 UI 的浏览器测试页能通过 SDK 完成 M2 的全部编辑闭环，且没有持续内存增长。

## M4：Web MVP

### WP-401 应用壳与状态层

选定框架，建立路由、store、Worker client、错误边界、主题和基础无障碍设施。

### WP-402 文件闭环

实现本地打开、诊断、下载保存、异步 `markSaved` 和未保存保护。

### WP-403 棋盘与导航

实现局面显示、键鼠选择、落子命令、当前路径和错误反馈。

### WP-404 变化视图

实现虚拟化有序树、主/旁支视觉、checkout、promote、删除和复制分支复合命令。

### WP-405 元数据与注释

使用类型化表单，处理 optional/null、Unicode、字节限制和并发 revision 冲突。

### WP-406 历史、校验和 Console

接入 Undo/Redo、结构/状态校验面板和 Debug Console；确保三种入口共享命令结果。

M4 出口：用户仅用浏览器即可完成产品闭环，重新打开保存文件后文档事实一致。

## M5：稳定化与发布

### WP-501 大文档性能

建立 100k 节点基准，profile 打开、局面查询、树渲染、导出和历史内存，达到预算或形成有证据的调整 ADR。

### WP-502 跨平台与浏览器矩阵

完成 native 三编译器、WASM headless 和 Web 目标浏览器测试；修复 worker/文件 API 差异。

### WP-503 安全和恢复

完成资源限制、parser/ABI fuzz、Worker fatal、恢复副本和隐私日志审计。

### WP-504 发布工程

完成 native archive、npm/WASM 包、Web production artifact、SBOM、checksums、迁移指南和 release notes。

M5 出口：满足 [质量、性能与发布](07-quality-delivery.md) 的发布完成定义。

## 明确延后项

以下事项不应夹带进入上述关键路径：

- 完整中国象棋走法与胜负裁决引擎；
- DAG 多父节点和真正的分支合并；
- 多人协作/CRDT；
- 云同步、账户和权限；
- AI 分析协议与引擎调度；
- 插件系统和第三方执行环境。

它们可以在 M2/M4 预留窄接口，但实现前分别需要需求文档、威胁模型和 ADR。

## 首批 issue 建议

可以直接从以下 10 个任务开始：

1. 为当前安装包增加旧 consumer 快照测试；
2. 记录现有 Writer vectors 的 SHA-256 基线；
3. 新增 `OXQF::format` build-tree target；
4. 验证并实现 install-tree `OXQF::core` 兼容 target；
5. 新增 `oxq/format` 转发/主公共头；
6. 将 `oxq-convert` 依赖迁到 `OXQF::format`；
7. 创建 `oxq-editor` target 与错误值类型；
8. 实现只读 `EditorSession::open/snapshot/export_document`；
9. 实现 InsertMove 的原子命令与测试；
10. 建立随机编辑序列测试 harness。

前 6 项构成可独立发布的 M1，后 4 项开始 M2，避免先搭 Web 壳却没有稳定引擎协议。
