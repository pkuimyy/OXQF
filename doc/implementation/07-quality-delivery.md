# 质量、性能与发布实施方案

## 测试分层

```text
少量 E2E：浏览器真实用户闭环
集成测试：format ↔ editor ↔ WASM ↔ SDK
契约测试：命令、Worker、错误 schema
单元/生成式测试：模型、命令、历史、parser、缓存
静态与动态检查：warnings、sanitizers、fuzz
```

每个缺陷至少在能稳定复现它的最低层增加回归测试；不要只用脆弱的浏览器 E2E 覆盖领域错误。

## 测试目录建议

```text
test/
├── format/                 # 从 core 渐进更名
├── editor/
│   ├── command_test.cpp
│   ├── history_test.cpp
│   ├── session_test.cpp
│   ├── position_cache_test.cpp
│   └── generated_sequence_test.cpp
├── console/
├── wasm/
├── integration/
└── fixtures/editor/

apps/oxq-web/
├── src/**/*.test.ts
└── e2e/*.spec.ts
```

fixture 要小而可读，并注明坐标系。超大性能数据由确定性生成器产生，不提交重复的大型二进制。

## 核心性质测试

除示例测试外，编辑引擎必须验证以下性质：

- 任意成功命令后 `MoveTree::validateInvariants()` 为真；
- 失败命令前后 document、session state 和事件计数相等；
- `undo(execute(C, S)) == S`，可适用命令均成立；
- `redo(undo(S')) == S'`，若中间无新命令；
- Writer→Reader 保持文档语义，规范化 Writer 再写字节稳定；
- 缓存开启和关闭得到相同局面；
- 控制台命令与强类型命令等价；
- native 与 WASM fixture 输出等价。

随机测试失败时记录 seed 和最小化命令序列，作为永久回归 fixture。

## Fuzz 计划

现有 Reader fuzz 保持。新增目标：

| Target | 输入 | 检查 |
|---|---|---|
| console tokenizer/parser | 任意 bytes | 无崩溃、超限及时退出 |
| command decoder | versioned JSON/二进制 | 无异常逃逸、稳定错误 |
| edit sequence | 受限命令流 | 每步不变量、Undo 往返 |
| WASM ABI | handle/op/buffer 序列 | 无越界、double free、UAF |

CI smoke 使用有界时间，夜间或 release gate 延长 corpus 运行。崩溃 artifact 纳入回归目录前应最小化并去除敏感内容。

## 性能预算

以下是首轮工程预算，不是未经测量的产品承诺。基准机器和浏览器版本记录在结果中；若不达标，必须给出 profile 和处置，而不是静默放宽。

| 场景 | 数据规模 | 初始预算 |
|---|---:|---:|
| 打开规范化 OXQ | 100k nodes / 50 MiB | native < 1 s，桌面浏览器 < 2 s |
| 叶节点插入 | 100k-node session | p95 < 16 ms（Worker 时间） |
| checkout 已缓存邻近节点 | 100k nodes | p95 < 8 ms |
| Undo/Redo 单步 | 普通编辑 | p95 < 16 ms |
| 首屏变化视图 | 100k nodes | < 2,000 DOM elements |
| 导出 | 100k nodes / 50 MiB | 浏览器 < 2 s |

还需记录峰值 RSS/WASM memory、历史每项估算字节、打开/关闭后的 retained memory。CI 只对高波动基准做趋势记录，对确定性资源上限做硬门禁。

## CI 阶段

### 每个 PR

- Markdown 链接/格式检查；
- Node 脚本测试和环境检查；
- GCC Debug build + CTest；
- Clang sanitizer build + CTest；
- Web lint、typecheck、unit tests；
- WASM build 和 headless Chromium 契约测试；
- 安装后新旧 CMake consumer 测试；
- 有界 fuzz smoke。

### 主分支/夜间

- 大规模生成式测试；
- 延长 fuzz；
- 性能趋势；
- 浏览器矩阵；
- 依赖和许可证审计。

### 发布候选

- Linux GCC、Linux Clang、Windows MSVC native 包；
- WASM/npm 包 clean-consumer 测试；
- 全量 OXQ vectors 和 CBL gold baseline；
- 跨平台规范化 Writer 指纹；
- Web production build 和离线 smoke；
- manifest、SHA-256、CHANGELOG、迁移说明和 SBOM（引入 Web 依赖后）。

## 版本策略

- OXQ 文件格式版本、C++ 库 semver、WASM ABI version、Worker protocol version 和 Web 产品版本分别管理；
- 同一仓库发布可以共享 release train，但不能假定版本含义相同；
- C++ 公共 API 的不兼容变更升主版本；
- Worker 消息只新增可选字段通常保持协议版本，改变字段含义则升协议版本；
- Reader 保持既有前向兼容规则，Writer 默认只写当前稳定格式；
- 旧 `core` 兼容入口的移除必须在前一版本警告并在 CHANGELOG 明示。

## 可观测性与隐私

native 和 Web 均可记录耗时、节点数、错误 code、版本和内存统计，但默认不记录棋谱注释、棋手元数据、文件名或原始字节。诊断导出必须让用户预览并主动选择。

## 发布完成定义

- 所有门禁在干净 checkout 可复现；
- release artifacts 能由独立 consumer 安装使用；
- 新旧兼容矩阵结果归档；
- 已知限制（尤其完整棋规校验、DAG、浏览器范围）出现在 release note；
- 文档中的命令、包名和 API 与实际产物一致。
