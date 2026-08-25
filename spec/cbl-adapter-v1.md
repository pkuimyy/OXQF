# OXQF CBL v3 Adapter 转换契约

状态：v1.0  
日期：2026-08-10

本文档冻结 `oxq-convert` 首个稳定 CBL v3 Adapter 中与 OXQ 互操作相关的转换决策。CBL 物理布局的证据与 Reader/Writer 细节仍以 `.codex/CBL-v3/` 的工作文档为准，不在此重复。

## 1. 适用范围

- 来源格式：已确认的 CBL v3 Library 和 CCB Record；
- 目标格式：OXQ v1.0；
- 参考兼容软件：象棋桥 3.0 beta4，程序版本 3.0.0.4；
- 一个有效 CBL 棋局 Record 映射为一个独立 OXQ 文件。

## 2. 棋局 UUID

Adapter 按以下优先级决定 OXQ Header 的 `game_uuid`：

1. Record GUID 与 Directory UUID 都能解析为非零 RFC 9562 UUID 且两者一致时，直接使用该 UUID；
2. 两者不一致，但 Record GUID 是可用非零 UUID 时，使用 Record GUID，并报告 `CBL_UUID_MISMATCH`；
3. Record GUID 不可用而 Directory UUID 可用时，使用 Directory UUID，并报告 `CBL_RECORD_UUID_INVALID`；
4. 两者都不可用时，按 2.1 节生成稳定 UUIDv5，并报告 `CBL_UUID_DERIVED`。

“可用”表示字节序转换后是 RFC 9562 variant 的非零 UUID。目录 UUID 使用有界 UTF-16LE 文本解码；Record GUID 按 Windows GUID 内存布局转为网络字节序。

### 2.1 UUIDv5 派生

命名空间 UUID 固定为：

```text
8e4fd752-6a26-5a9b-b295-0f2e0973c943
```

该值是以 RFC URL namespace 对 ASCII 名称 `https://openxiangqi.org/spec/cbl-adapter-v1` 计算的 UUIDv5，只作为稳定协议常量，不依赖该 URL 可访问。

UUIDv5 的 name 是以下文本的精确 UTF-8 字节，行之间使用 LF，末尾没有 LF：

```text
org.openxiangqi.cbl/v1
source-sha256=<source_sha256>
physical-slot=<physical_slot>
record-sha256=<record_sha256>
```

- `source_sha256`：整个原始 CBL 文件的 SHA-256，64 个小写十六进制字符；
- `physical_slot`：Directory 物理槽号的 ASCII 十进制表示，不带前导零；
- `record_sha256`：从计算得到的 Record 物理起点开始，长度为 Directory `used_size` 的原始字节 SHA-256，格式同上。

计算 UUIDv5 时命名空间使用 RFC 9562 的 16 字节网络序表示，不得使用 Windows GUID 内存字节序。同一原文件、物理槽和 Record 字节必须得到同一 UUID；导入时间、输出路径和本地时区不参与计算。

## 3. 缺失与扩展数据

- CBL 中缺失的可选文本映射为缺失 Metadata Field，不写空字符串；
- CBL 明确保存的空文本若能与缺失区分，必须保留为 OXQ 空字符串；
- 已有 OXQ 标准 Tag 的信息优先写入标准 Tag；
- CBL 专有且有意义的长尾字段写入 `org.openxiangqi.cbl` 扩展元数据；
- 无法表示的语义必须进入 `ConversionReport`，严格模式下拒绝有损输出。

## 4. 确定性

同一 Adapter 版本在相同选项下读取相同 CBL 字节时，必须生成相同的棋局枚举顺序、UUID、GameModel 语义和规范化 OXQ 字节。

## 5. 导出可表达性与 preflight

`GameModel → CBL v3` 必须先运行完整 preflight，再分配目标文件缓冲区。结构无效、固定槽溢出、注释或 Record 超限属于不可生成错误；其他目标格式损失在宽松模式中允许生成并逐项报告，在严格模式中拒绝整个棋库。

首版可表达性矩阵如下：

| GameModel 语义 | CBL v3 策略 | 分类 |
| --- | --- | --- |
| 非零 RFC 9562 游戏 UUID | 同时写 Directory UUID 和 Record GUID | 精确 |
| 初始局面、先行方、fullmove | 写 Board、SideToMove、FullmoveNumber | 精确 |
| 有序变化树 | 转为 DFS 前序 child/sibling 流 | 精确 |
| 每节点一条普通 comment，仅含文本 | 写节点或根注释块 | 精确 |
| 多条注释 | 按原顺序以空行连接文本 | 有损规范化 |
| source note、before-move、注释作者或语言 | 保留文本，丢弃 CBL 不支持的属性 | 有损规范化 |
| 标题、棋手/队伍/用时/等级分、赛事、地点、轮次、组别、台号、用时规则 | 写已确认固定槽 | 精确，受槽长限制 |
| DAY 精度的 `YYYY-MM-DD` 开始日期 | 写 Date | 精确 |
| 其他日期精度、结束时间 | 不写 | 丢失 |
| UNKNOWN、红胜、黑胜、和棋 | 写 Result 0..3 | 精确 |
| 缺失结果、UNFINISHED、ABORTED | 写 Result 0 | 有损规范化 |
| 1–3 字节 ASCII opening code | 写 ECCO | 精确 |
| player id/country/title、event id/organizer、opening name/id、tags | 不写 | 丢失 |
| 来源 URI、来源分类 | 写 From、URLOrCategory | 精确，受槽长限制 |
| 其他 provenance | 不写入单局字段 | 丢失 |
| `org.openxiangqi.cbl` 的 `record_type`、`result`、`source_fullmove_number`、`root_marker`、`source_controls` | 校验后恢复来源值；Control 结构位由树重新计算 | 精确或规范化 |
| 其他扩展属性 | 不写 | 丢失 |

固定 UTF-16LE 文本槽必须预留末尾 NUL；超过槽容量时宽松模式也不得截断生成。CBL 固定槽无法区分缺失字符串与存在但为空的字符串，因此后者属于已报告损失。

未显式提供库 UUID 时，Writer 使用本规范第 2.1 节 namespace，以以下 UTF-8 名称生成 UUIDv5：

```text
org.openxiangqi.cbl-writer/v1
library-name-sha256=<棋库名 UTF-8 的 SHA-256>
game-count=<十进制棋局数>
game-uuid=<第 1 局 UUID>
game-uuid=<第 2 局 UUID>
...
```

棋局顺序参与身份；作者、时间和输出路径不参与。默认目录容量为 `max(128, game_count)`，调用方可提高最小容量；Writer 不生成墓碑、非棋局资源或随机填充。

新建 Record 的未确认固定区和所有保留字节写零；版本写 `00 00 00 02`，没有可恢复来源值时 `RecordType=0`、`root_marker=0xffffffff`、Control 高位为零。Directory UUID 与 Record GUID 都从 `GameModel.uuid` 生成，任何固定 UTF-16LE 槽都使用最短合法编码、一个 NUL 和零填充。
