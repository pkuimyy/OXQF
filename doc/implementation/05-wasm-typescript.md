# WASM 与 TypeScript SDK 实施方案

## 目标

把同一份 `oxq-format` 和 `oxq-editor` C++ 实现编译到浏览器，提供稳定、类型安全、可测试的 TypeScript API。WASM 层只做 ABI 转换，不复制业务规则。

## 构建产物

```text
bindings/wasm/
├── CMakeLists.txt
├── src/abi.cpp
├── js/worker.ts
├── js/client.ts
├── js/protocol.ts
└── package/
    ├── oxq_editor.js
    ├── oxq_editor.wasm
    ├── index.js
    ├── index.d.ts
    └── package.json
```

建议以 Emscripten 为首个工具链，输出 ESM。构建必须固定工具链版本，并在 release manifest 中记录 Emscripten、CMake、编译器 flags 和 WASM SHA-256。

## ABI 原则

C++ STL 类型、异常和对象地址不跨边界。C ABI 仅暴露整数句柄、字节缓冲和 UTF-8 JSON 控制消息：

```c
uint32_t oxq_session_open(const uint8_t* bytes, size_t length,
                          oxq_buffer_t* error);
int32_t oxq_session_dispatch(uint32_t handle,
                             const uint8_t* request, size_t request_length,
                             oxq_buffer_t* response);
int32_t oxq_session_export(uint32_t handle, oxq_buffer_t* output);
void oxq_buffer_free(oxq_buffer_t buffer);
void oxq_session_close(uint32_t handle);
const char* oxq_abi_version(void);
```

函数名仅表示形态，最终声明需包含明确 visibility。每一个由 WASM 分配的 buffer 都必须由对应 free 释放；JS 不缓存指向线性内存的 view 跨越下一次可能增长内存的调用。

## 句柄生命周期

- handle `0` 无效；
- `open` 成功后由 registry 独占 session；
- `close` 幂等，释放 history、cache 和文档；
- 对过期 handle 返回稳定错误，不崩溃；
- 页面卸载、Worker terminate 和 SDK `dispose()` 都应有释放路径；
- FinalizationRegistry 只能作兜底，不能代替显式 `dispose()`。

## Worker 协议

主线程不直接调用 WASM：

```ts
type WorkerRequest =
  | { v: 1; id: string; type: "open"; bytes: ArrayBuffer }
  | { v: 1; id: string; type: "command"; session: string; expectedRevision: string; command: EditorCommand }
  | { v: 1; id: string; type: "query"; session: string; query: EditorQuery }
  | { v: 1; id: string; type: "export"; session: string }
  | { v: 1; id: string; type: "markSaved"; session: string; revision: string }
  | { v: 1; id: string; type: "close"; session: string };
```

- `ArrayBuffer` 用 transferable 传输，避免文件字节复制；
- 请求按 session 串行处理，不允许同一 session 并发修改；
- 所有响应回显 `id`，修改响应包含 revision 与 ChangeSet；
- Worker 崩溃时 client 拒绝全部 pending Promise，并把 session 标为不可用；
- 协议 `v` 与 npm 包 semver 分开管理。

## TypeScript API

```ts
interface OxqEditorClient {
  open(bytes: ArrayBuffer, options?: OpenOptions): Promise<EditorSessionHandle>;
}

interface EditorSessionHandle {
  readonly id: string;
  command(command: EditorCommand, expectedRevision: string): Promise<CommandResult>;
  query(query: EditorQuery): Promise<QueryResult>;
  undo(expectedRevision: string): Promise<CommandResult>;
  redo(expectedRevision: string): Promise<CommandResult>;
  export(): Promise<ArrayBuffer>;
  markSaved(revision: Revision): Promise<SessionState>;
  dispose(): Promise<void>;
}
```

NodeId 和 revision 在 TypeScript 中均使用 branded string：

```ts
type NodeId = string & { readonly __nodeId: unique symbol };
type Revision = string & { readonly __revision: unique symbol };
```

不要把它们暴露为 `number`。坐标使用受限整数类型或代数坐标解析函数，并在进入 Worker 前做一次轻量校验，C++ 仍是最终校验权威。

## 数据传输策略

- 打开/保存传二进制；命令和小查询传结构化对象；
- 初次打开返回有限摘要，变化树通过分页或 `from/depth/limit` 查询；
- ChangeSet 驱动 UI 增量更新，避免每步复制完整文档；
- 大注释和大树响应应有明确字节/节点上限；
- 第一版保持单线程 WASM，只有基准证明必要时再评估 pthread、SharedArrayBuffer 和 COOP/COEP 部署要求。

## 错误映射

C++ 错误转换为：

```ts
interface OxqErrorData {
  domain: "format" | "editor" | "abi" | "worker";
  code: string;
  message: string;
  path?: string;
  nodeId?: NodeId;
  expectedRevision?: Revision;
  actualRevision?: Revision;
}
```

SDK 抛出带 `data` 的 `OxqError`，但 Worker wire response 本身始终是可判别 union。不得把 C++ `what()` 当稳定 code。

## 测试与发布门禁

- native 与 WASM 对同一命令 fixture 产生等价结果；
- OXQ 全部有效/畸形向量在 Chromium Worker 中运行；
- open→edit→undo→redo→export→native read 端到端通过；
- 反复 open/close 和 buffer 分配在固定轮数后内存不持续增长；
- NodeId 大于 `2^53` 的协议测试；
- Worker 并发请求、过期 revision、terminate 和 dispose 竞态测试；
- npm 包在干净示例项目中类型检查并运行；
- release 同时发布 `.wasm`、loader、类型声明、source map 策略和许可证。
