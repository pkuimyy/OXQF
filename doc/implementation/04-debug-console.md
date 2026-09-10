# Debug Console 实施方案

## 定位

Debug Console 是命令系统的文本适配器和查询入口，不是第二套编辑 API。解析器只负责把文本变为类型化请求；状态修改仍由 `CommandBus` 完成。

## 分层

```text
input line
  → tokenizer
  → parser（ConsoleRequest）
  → dispatcher
       ├── QueryService（只读）
       └── CommandBus（修改）
  → ConsoleResponse
  → text/json renderer
```

Tokenizer、parser、renderer 可在 native 单元测试中独立运行，不依赖 DOM 或 WASM。

当前 native 实现公开 `tokenize_console`、`parse_console`、`DebugConsole` 和
`render_console_response`。`DebugConsole` 只持有宿主提供的 `EditorSession&`，不拥有
文档，也不提供文件系统或网络能力；修改、导航和历史请求分别转发到会话现有 API。

## 语法约定

- UTF-8 输入，一行一个请求；
- 空白分隔 token；双引号包裹含空白字符串；
- 支持 `\"`、`\\`、`\n`、`\t`，未知转义报错；
- `#` 仅在 token 起始位置引入行尾注释；
- 命令和选项使用 ASCII 小写，用户内容保持原样；
- square 首版采用 `0..89`，并可额外接受 `a0..i9`；输出统一同时给出数值和坐标；
- NodeId 输入输出使用无符号十进制字符串；
- 不做 shell expansion、文件重定向、环境变量替换或任意代码执行。

建议使用明确语法，而非原稿中的 `move h2 e2`（它看起来像移动两个棋盘坐标，却未固定坐标系）：

```text
move add --parent 42 --from h2 --to e2 [--index 0]
move replace --node 51 --from h7 --to h0
node delete --node 51
branch promote --node 51 --index 0
checkout --node 51
annotation set --node 51 --kind comment --text "中炮"
undo
redo
```

## 首版命令表

| 请求 | 类型 | 引擎映射 |
|---|---|---|
| `status` | 查询 | SessionState |
| `tree [--from ID] [--depth N]` | 查询 | VariationTree projection |
| `node show --node ID` | 查询 | 节点、局面、注释 |
| `validate [--state]` | 查询 | format validation |
| `move add ...` | 修改 | InsertMove |
| `move replace ...` | 修改 | ReplaceMove |
| `node delete ...` | 修改 | DeleteSubtree |
| `branch promote ...` | 修改 | ReorderVariation |
| `checkout --node ID` | 导航 | CheckoutNode |
| `annotation set ...` | 修改 | SetAnnotations |
| `undo` / `redo` | 修改 | HistoryManager |
| `help [command]` | 查询 | CommandRegistry |

`branch create "名称"` 暂不进入首版，因为 OXQ v1 的分支没有名称字段。若需要命名，应先决定它是注释约定、编辑器临时标签还是格式扩展，不能悄悄塞入普通 comment。

## 响应协议

控制台内部返回结构化结果，再渲染为文本。WASM/Web 默认使用 JSON 形态：

```json
{
  "schemaVersion": 1,
  "requestId": "c-104",
  "ok": true,
  "revision": "28",
  "result": {
    "kind": "moveInserted",
    "nodeId": "51"
  },
  "diagnostics": []
}
```

失败响应：

```json
{
  "schemaVersion": 1,
  "requestId": "c-105",
  "ok": false,
  "revision": "28",
  "error": {
    "code": "editor.node_not_found",
    "message": "node 99 does not exist",
    "span": { "start": 19, "length": 2 }
  }
}
```

JSON 中所有可能超过 53 位精度的整数均编码为十进制字符串。`message` 面向人，程序只依赖 `code` 和结构化字段。

`ConsoleResponse` 保留结构化的 session state、树投影、节点/局面/注释、校验问题或
`CommandResult`，渲染器只负责 text/JSON 表达。JSON 的 revision 和全部 NodeId 均为
字符串；棋盘格同时输出 `index` 和 `coord`。console/editor 错误分别使用
`console.*`、`editor.*` 稳定 code，解析错误附字节 span。

## 安全与资源限制

- 单行默认最多 64 KiB、token 最多 256 个、嵌套 JSON 值不在首版语法中；
- `tree` 必须支持 depth/node limit，默认不打印整棵千万节点树；
- 控制台无文件系统和网络命令；文件打开/保存由宿主应用提供；
- 日志对棋谱注释和元数据按隐私数据处理，生产构建默认不记录原文；
- parser 错误必须带字节 span，且不能改变 session。
- fuzz preset 包含 `editor.console-parser-fuzz-smoke`，在 ASan/UBSan 下持续覆盖任意字节输入。

## 测试与完成定义

- 引号、转义、Unicode、非法 UTF-8、超长输入和边界数字测试；
- 每个修改语句与直接构造的强类型 Command 得到相同结果；
- 文本/JSON renderer 使用快照测试并固定 schemaVersion；
- fuzz tokenizer/parser，任意输入不得崩溃或卡死；
- 查询不会修改 revision、dirty 或 history；
- 浏览器 Console 面板不包含任何直接文档写路径。
