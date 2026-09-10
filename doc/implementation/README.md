# OXQ 生态演进实施文档

本目录把《OXQ 生态架构演进与现代棋谱编辑器设计》转换为可排期、可测试、可验收的工程实施基线。本文档集以仓库当前 `1.0.0` 实现为起点：C++20、CMake 3.23、`oxq-core`、`oxq-convert`、`oxq-cli`，以及冻结的 OXQ v1 文件格式。

## 阅读顺序

1. [架构决策与边界](00-architecture-decisions.md)：固定命名、依赖、兼容和首版范围。
2. [oxq-format 迁移](01-format-migration.md)：把 `oxq-core` 安全演进为 `oxq-format`。
3. [编辑领域模型](02-editor-domain-model.md)：定义 `EditorSession`、快照、选择和变化树。
4. [命令与历史系统](03-command-history.md)：定义原子命令、事务、Undo/Redo 和错误契约。
5. [调试控制台](04-debug-console.md)：定义语法、命令映射和机器可读输出。
6. [WASM 与 TypeScript SDK](05-wasm-typescript.md)：定义跨语言句柄、内存和异步边界。
7. [Web 编辑器](06-web-editor.md)：定义前端分层、Worker 协议和交互闭环。
8. [质量、性能与发布](07-quality-delivery.md)：定义测试矩阵、基准、CI 和发布门禁。
9. [路线图与工作包](08-roadmap.md)：给出依赖有序的里程碑和完成定义。

## 总体交付链

```text
OXQ v1 specification
        │
        ▼
oxq-format ───────► oxq-convert ───────► oxq-cli
        │                                  │
        ▼                                  │
oxq-editor                                 │
        │                                  │
        ▼                                  │
WASM + TypeScript SDK ◄────────────────────┘（仅复用格式能力）
        │
        ▼
oxq-web
```

依赖只能向下：`format` 不知道编辑器、转换器、WASM 或 UI；`convert` 不依赖 `editor`；UI 不直接修改文档模型。

## 共同完成定义

每个工作包只有同时满足以下条件才算完成：

- 公共 API、错误语义、所有权、生命周期、资源限制、线程安全和异常契约在面向使用者的 API 文档中有记录；
- `oxq-format` 和 `oxq-editor` 分别有完整 API 指南，并有一份覆盖 Reader→Editor→Writer 的端到端集成指南；
- 公共头文件包含可生成 API Reference 的文档注释，文档示例由 CI clean-consumer 编译并运行；
- Linux GCC、Linux Clang 和 Windows MSVC 构建通过；
- 新行为有单元测试，跨模块路径有集成测试；
- 不降低现有 OXQ v1 测试向量、CBL 金标和 Reader fuzz 门禁；
- 公共 API 变更包含迁移说明，破坏性变更经过 ADR；
- 安装后的独立 consumer 能通过 `find_package(OXQF CONFIG REQUIRED)` 使用交付目标。

## 非目标

首轮实施不包含多人实时协作、云端账户、插件市场、AI 引擎协议、任意 DAG 棋谱格式和完整赛事裁决。它们可以建立扩展点，但不得阻塞本轮编辑闭环。
