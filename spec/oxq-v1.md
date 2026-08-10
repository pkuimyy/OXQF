# OXQ v1 单局棋谱二进制格式规范

状态：v1.0 Release Candidate 1（格式已冻结，待独立 Reader 验证后去除 RC 标记）  
格式族：OXQF（Open Xiangqi Format）  
格式名称：OXQ（Open Xiangqi Game）  
目标版本：1.0  
日期：2026-08-10

## 1. 文档约定

本文中的关键词“必须”“不得”“应该”“不应该”“可以”用于区分规范要求：

- **必须 / 不得**：实现互操作所需的强制要求；
- **应该 / 不应该**：除非有充分理由，否则应遵守；
- **可以**：可选行为。

本文所有偏移均从文件第一个字节开始，以字节为单位。区间使用左闭右开表示法 `[offset, offset + size)`。

除非另有说明：

- 无符号整数记为 `u8`、`u16`、`u32`、`u64`；
- 有符号整数记为 `i32`、`i64`，使用二进制补码；
- 多字节整数均使用小端序；
- “保留字段”写入时必须为零，读取时非零必须报格式错误；
- 所有长度和偏移均指字节数，不包含隐式结束符。

## 2. 设计目标

一个 OXQ 文件表示一盘完整、可独立存在的中国象棋棋局知识对象。

OXQ v1 的目标是：

- 保存棋局身份和基础元数据；
- 保存标准或自定义初始局面；
- 保存主线和任意数量的变化分支；
- 保存节点上的人类注释；
- 支持未知可选 Section 的向前兼容；
- 明确损坏文件检测和安全解析规则；
- 在不同平台上产生确定的规范化字节表示；
- 允许标准演进和第三方格式在不污染核心字段的前提下扩充元数据；
- 不依赖 C++ ABI、Qt、数据库或操作系统结构。

OXQF 所称的“语义兼容”不是逐字节还原来源格式。对于 CBL，参考转换器应能把 OXQ 中属于 CBL 表达域的棋局事实导出为当前受支持的 CBL，并由所声明的象棋桥兼容版本打开；无法由目标 CBL 表达的 OXQ 数据必须报告，严格模式下必须拒绝有损导出。

OXQ v1 不定义：

- 多棋局容器；
- 数据库索引；
- 压缩算法；
- 加密或数字签名；
- 引擎搜索树；
- 完整中国象棋规则裁决。

多棋局管理属于未来的 OXQP 规范。

## 3. 文件总体布局

OXQ v1 文件由以下区域组成：

```text
+-------------------------------+
| File Header（64 bytes）       |
+-------------------------------+
| Section Table                 |
+-------------------------------+
| Zero Padding / Alignment      |
+-------------------------------+
| Section Payload               |
+-------------------------------+
| ...                           |
+-------------------------------+
```

规范化写入器必须按以下顺序输出：

1. 64 字节 File Header；
2. Section Table；
3. 补零到 8 字节边界；
4. 按 `section_type` 数值升序排列的 Section Payload；
5. 每个 Section 后补零，使下一个 Section 从 8 字节边界开始。

读取器不得假设 Section Payload 一定紧邻或按类型排序，必须以 Section Table 中的偏移为准。

文件不得包含未被 Header、Section Table、Section Payload 或对齐填充覆盖的非零字节。规范化文件中的所有填充字节必须为零。

## 4. File Header

File Header 固定为 64 字节。

| 偏移 | 大小 | 类型 | 字段 | v1 要求 |
| ---: | ---: | --- | --- | --- |
| `0x00` | 8 | bytes | `magic` | 固定为 `89 4F 58 51 0D 0A 1A 0A` |
| `0x08` | 2 | u16 | `version_major` | `1` |
| `0x0A` | 2 | u16 | `version_minor` | `0` |
| `0x0C` | 4 | u32 | `header_size` | `64` |
| `0x10` | 8 | u64 | `file_size` | 必须等于实际文件长度 |
| `0x18` | 8 | u64 | `section_table_offset` | 规范化文件中为 `64` |
| `0x20` | 4 | u32 | `section_count` | v1 必须为 `5..1024`；五个标准 Section 各出现一次 |
| `0x24` | 4 | u32 | `file_flags` | v1 必须为 `0` |
| `0x28` | 16 | bytes | `game_uuid` | RFC 9562 UUID 的 16 字节网络序表示 |
| `0x38` | 4 | u32 | `section_table_crc32c` | Section Table 原始字节的 CRC-32C |
| `0x3C` | 4 | u32 | `header_crc32c` | Header `[0x00, 0x3C)` 的 CRC-32C |

### 4.1 Magic

OXQ v1 的 8 字节签名写作：

```text
十六进制：89 4F 58 51 0D 0A 1A 0A
字符示意：\x89 O  X  Q  \r \n \x1a \n
```

各字节的诊断目的为：

- `0x89` 设置最高位，用于尽早暴露不支持 8 位字节的传输或把二进制误认为纯文本的处理；
- `4F 58 51` 是 ASCII `OXQ`，便于人工识别格式；
- `0D 0A` 是 CR/LF，用于暴露换行符转换；
- `1A` 是传统文本 EOF 标记，用于暴露把文件当作旧式文本流的处理；
- 末尾 `0A` 用于暴露反向的 LF → CR/LF 转换。

Reader 必须逐字节精确匹配全部签名；任一字节不同都必须返回 `INVALID_MAGIC`。Magic 只用于格式识别和早期损坏诊断，不取代 Header、Section Table 和 Payload CRC。版本号仍以 Header 中 `version_major` 和 `version_minor` 为唯一事实来源。

### 4.2 版本兼容

- `version_major != 1`：v1 读取器必须拒绝；
- `version_major == 1` 且 `version_minor > 0`：v1.0 读取器可以尝试继续，但只能接受它已完整理解的 Header、标准 Section 版本和标志；遇到任何未知 critical 能力必须拒绝；
- `version_major == 1` 且 `version_minor == 0` 时，`header_size` 必须恰好为 `64`；
- v1.0 读取器遇到更高次版本且 `header_size != 64` 时必须返回 `UNSUPPORTED_VERSION`，不得猜测扩展 Header 的语义；
- `section_table_offset` 必须不小于 `header_size`；对 v1.0 规范化文件，它必须为 `64`。

### 4.3 UUID

`game_uuid` 不得为全零 UUID。

- 原生新建棋局应该使用 UUIDv7；
- 从具有稳定原始身份的第三方格式导入时，可以使用 UUIDv5，以保证重复导入得到相同身份；
- 导入器不得使用导入时间生成所谓“稳定 UUID”；
- 同一逻辑棋局的 UUID 不因文件重新序列化而改变。

UUID 的文本显示采用小写、带连字符形式；磁盘中只保存 RFC 9562 定义的 16 字节表示。

## 5. CRC-32C

本规范中的 CRC 均为 CRC-32C（Castagnoli）：

- 多项式正规表示：`0x1EDC6F41`；
- 反射实现多项式：`0x82F63B78`；
- 初始值：`0xFFFFFFFF`；
- 输入和输出均反射；
- 最终异或：`0xFFFFFFFF`。

校验值按 `u32` 小端序写入。

读取器必须先完成安全的范围检查，再计算 CRC；不得先根据未校验偏移访问文件。

## 6. Section Table

每个 Section Entry 固定为 40 字节。Section Table 长度为：

```text
section_count * 40
```

计算乘法和加法时必须检测整数溢出。

### 6.1 Section Entry

| 相对偏移 | 大小 | 类型 | 字段 | 含义 |
| ---: | ---: | --- | --- | --- |
| `0x00` | 4 | u32 | `section_type` | Section ID |
| `0x04` | 4 | u32 | `section_flags` | Section 行为标志 |
| `0x08` | 8 | u64 | `offset` | Payload 文件偏移 |
| `0x10` | 8 | u64 | `stored_size` | 文件中保存的字节数 |
| `0x18` | 8 | u64 | `logical_size` | 解码后的字节数 |
| `0x20` | 4 | u32 | `payload_crc32c` | `stored_size` 个原始字节的 CRC-32C |
| `0x24` | 4 | u32 | `reserved` | 必须为 `0` |

### 6.2 Section Flags

| 位 | 名称 | 含义 |
| ---: | --- | --- |
| 0 | `CRITICAL` | 不理解该 Section 时必须拒绝整个文件 |
| 1 | `COMPRESSED` | Payload 已压缩；v1.0 不支持 |
| 2–31 | 保留 | v1.0 必须为零 |

v1.0 中所有已定义 Section 都必须设置 `CRITICAL`，且不得设置 `COMPRESSED`。因此 `section_flags` 必须为 `1`。

未来读取器遇到未知 Section 时：

- 若 `CRITICAL=1`，必须拒绝；
- 若 `CRITICAL=0`，必须校验其范围不与其他区域重叠，然后跳过；
- 不得猜测未知 Section 的内部结构。

### 6.3 Section ID

| ID | 名称 | v1.0 数量 |
| ---: | --- | ---: |
| `1` | `GAME_METADATA` | 恰好 1 |
| `2` | `POSITION` | 恰好 1 |
| `3` | `MOVE_TREE` | 恰好 1 |
| `4` | `ANNOTATION` | 恰好 1，可以为空 |
| `5` | `STRING_POOL` | 恰好 1，可以无用户字符串 |
| `0` | 无效 | 不得出现 |
| `0x80000000–0xFFFFFFFF` | 私有实验 | 不保证互操作 |

私有实验 Section 不得被标记为 critical，且不得承载读取标准棋局所必需的唯一信息。

### 6.4 范围和重叠规则

每个 Section 必须满足：

- `offset` 为 8 的倍数；
- `offset + stored_size` 不溢出且不大于 `file_size`；
- 不与 Header 或 Section Table 重叠；
- 不与其他 Section Payload 重叠；
- v1.0 中 `stored_size == logical_size`；
- 即使 Payload 为空，`offset` 也必须是有效的 8 字节对齐文件位置。

Section Table 的 Entry 应按 `section_type` 升序排列。读取器必须拒绝任何重复的 Section ID，包括未知和私有实验 Section。

五个标准 Section 的 `section_version`、`header_size`、记录大小和标志必须与本规范精确一致。由于它们都是 critical，v1.0 Reader 遇到未理解的标准 Section 版本必须返回 `UNSUPPORTED_VERSION`，不得按已知布局继续解析。

## 7. STRING_POOL Section

所有可变文本统一保存为 UTF-8 字符串。其他 Section 使用 `string_ref` 引用字符串。

### 7.1 string_ref

`string_ref` 是一个 `u32`，表示字符串记录相对于 STRING_POOL Payload 起点的字节偏移：

- `0` 表示字段不存在；
- 非零值必须指向某条 String Record 的 `byte_length` 字段；
- 不允许指向字符串内容中部或填充区；
- 引用偏移必须能由 `u32` 表达，因此 STRING_POOL 的逻辑大小不得超过 `0xFFFFFFFF`。

“字段不存在”与“存在但为空字符串”是不同状态。空字符串必须引用一条 `byte_length == 0` 的记录。

### 7.2 Header

| 偏移 | 大小 | 类型 | 字段 | v1 值 |
| ---: | ---: | --- | --- | --- |
| `0x00` | 2 | u16 | `section_version` | `1` |
| `0x02` | 2 | u16 | `header_size` | `8` |
| `0x04` | 4 | u32 | `string_count` | 字符串记录数 |

第一条 String Record 从偏移 `8` 开始。

### 7.3 String Record

```text
u32 byte_length
u8  utf8_bytes[byte_length]
u8  zero_padding[0..3]
```

每条记录补零到 4 字节边界。字符串不带 NUL 结束符；内容可以包含 U+0000，但应该避免。

字符串必须满足：

- 是最短形式、合法的 UTF-8；
- 不包含未配对代理项的编码；
- 文本应该规范化为 Unicode NFC；
- 换行规范化为 LF（U+000A）；
- 不得包含 CR（U+000D），除非它作为普通文本确有语义。

规范化写入器必须去重字符串，并按 UTF-8 字节的无符号字典序排列记录。所有引用在最终排序后计算。

Reader 必须验证 UTF-8 合法性，但不得仅因文本未经 NFC 规范化而拒绝结构合法的文件。未经 NFC 规范化属于非规范化编码，Validator 可以报告非致命警告；规范化重写时必须转换为 NFC。

## 8. GAME_METADATA Section

GAME_METADATA 使用可扩展 TLV 记录。未知非 critical 元数据字段可以被读取器保留或跳过，但必须遵守本节的长度、填充和边界规则。

### 8.1 Header

| 偏移 | 大小 | 类型 | 字段 | v1 值 |
| ---: | ---: | --- | --- | --- |
| `0x00` | 2 | u16 | `section_version` | `1` |
| `0x02` | 2 | u16 | `header_size` | `8` |
| `0x04` | 4 | u32 | `field_count` | TLV Field 数量 |

### 8.2 Metadata Field

```text
u16 tag
u8  value_type
u8  field_flags
u32 value_length
u8  value[value_length]
u8  zero_padding[0..3]
```

Field 总长度补零到 4 字节边界。

`field_flags`：

| 位 | 名称 | 含义 |
| ---: | --- | --- |
| 0 | `CRITICAL` | 不理解该字段时必须拒绝 |
| 1 | `REPEATED` | 同一 tag 可以重复 |
| 2–7 | 保留 | 必须为零 |

标准字段除 `TAG` 外不得重复。`TAG` 必须设置 `REPEATED`；其他标准字段不得设置该位。v1 标准字段均不设置 `CRITICAL`。

### 8.3 Value Type

| 值 | 名称 | `value_length` | 编码 |
| ---: | --- | ---: | --- |
| `1` | `U32` | 4 | 小端 u32 |
| `2` | `I32` | 4 | 小端 i32 |
| `3` | `U64` | 8 | 小端 u64 |
| `4` | `I64` | 8 | 小端 i64 |
| `5` | `STRING_REF` | 4 | 小端 string_ref |
| `6` | `BYTES` | 任意 | 原始字节 |
| `7` | `BOOL` | 1 | 仅允许 `0` 或 `1` |

已知标准 tag 的 `value_type`、`value_length`、`field_flags` 和重复次数必须符合本规范，否则返回 `INVALID_METADATA`。未知 tag 的精确行为为：

- `CRITICAL=1`：必须拒绝整个文件；
- `CRITICAL=0` 且 `value_type` 已知：必须验证该类型的固定长度和取值；`STRING_REF` 还必须验证引用；之后可保留或跳过；
- `CRITICAL=0` 且 `value_type` 未知：只按 `value_length` 安全跳过，不得解释 `value`。

声称能够对未知 Metadata 做语义保留重写的工具，必须保留未知字段的 tag、类型、标志和值；对未知 `value_type` 不能识别其中潜在引用时，不得宣称重排 STRING_POOL 后仍能无损保留。

### 8.4 标准 Metadata Tag

| Tag | 名称 | 类型 | 含义 |
| ---: | --- | --- | --- |
| `0x0001` | `RED_PLAYER_NAME` | STRING_REF | 红方姓名 |
| `0x0002` | `BLACK_PLAYER_NAME` | STRING_REF | 黑方姓名 |
| `0x0003` | `RED_PLAYER_ID` | STRING_REF | 来源或权威系统中的红方 ID |
| `0x0004` | `BLACK_PLAYER_ID` | STRING_REF | 黑方 ID |
| `0x0005` | `RED_COUNTRY` | STRING_REF | 红方国家或地区 |
| `0x0006` | `BLACK_COUNTRY` | STRING_REF | 黑方国家或地区 |
| `0x0007` | `RED_RATING` | I32 | 红方等级分 |
| `0x0008` | `BLACK_RATING` | I32 | 黑方等级分 |
| `0x0009` | `RED_TITLE` | STRING_REF | 红方称号 |
| `0x000A` | `BLACK_TITLE` | STRING_REF | 黑方称号 |
| `0x000B` | `RED_TEAM` | STRING_REF | 红方代表队 |
| `0x000C` | `BLACK_TEAM` | STRING_REF | 黑方代表队 |
| `0x000D` | `RED_TIME_USED` | STRING_REF | 红方用时原文；未统一量纲时不得臆造数值 |
| `0x000E` | `BLACK_TIME_USED` | STRING_REF | 黑方用时原文 |
| `0x0010` | `EVENT_NAME` | STRING_REF | 比赛名称 |
| `0x0011` | `EVENT_ID` | STRING_REF | 比赛 ID |
| `0x0012` | `LOCATION` | STRING_REF | 地点 |
| `0x0013` | `ORGANIZER` | STRING_REF | 主办方 |
| `0x0014` | `ROUND` | STRING_REF | 轮次；保留“12”“12.3”等原始表达 |
| `0x0015` | `EVENT_TYPE` | STRING_REF | 比赛或活动类型原文 |
| `0x0016` | `GROUP` | STRING_REF | 组别 |
| `0x0017` | `BOARD_NUMBER` | STRING_REF | 台号或桌号；保留来源表达 |
| `0x0018` | `TIME_CONTROL` | STRING_REF | 用时规则原文 |
| `0x0020` | `START_TIME` | STRING_REF | ISO 8601 日期或日期时间 |
| `0x0021` | `END_TIME` | STRING_REF | ISO 8601 日期或日期时间 |
| `0x0022` | `DATE_PRECISION` | U32 | 日期精度枚举 |
| `0x0030` | `RESULT` | U32 | 结果枚举 |
| `0x0031` | `RESULT_TEXT` | STRING_REF | 来源中的赛果说明；不替代结构化 RESULT |
| `0x0040` | `OPENING_NAME` | STRING_REF | 开局名称 |
| `0x0041` | `OPENING_CODE` | STRING_REF | 开局编码 |
| `0x0042` | `OPENING_ID` | STRING_REF | 开局权威 ID |
| `0x0050` | `TITLE` | STRING_REF | 棋局标题 |
| `0x0051` | `TAG` | STRING_REF | 可重复的标签 |
| `0x0052` | `GAME_TYPE` | STRING_REF | 实战、摆谱、全局、中残局等棋局类型原文 |
| `0x0060` | `REFEREE` | STRING_REF | 裁判 |
| `0x0061` | `RECORDER` | STRING_REF | 记录者 |
| `0x0062` | `COMMENTATOR` | STRING_REF | 解说者 |
| `0x0063` | `COMMENTATOR_URI` | STRING_REF | 解说者主页或来源 URI |
| `0x0064` | `CREATOR` | STRING_REF | 棋谱数据创建者 |
| `0x0065` | `CREATOR_URI` | STRING_REF | 创建者主页或来源 URI |
| `0x0066` | `RECORD_CREATED_AT` | STRING_REF | 来源记录创建时间原文 |
| `0x0067` | `RECORD_MODIFIED_AT` | STRING_REF | 来源记录修改时间原文 |
| `0x0100` | `SOURCE_FORMAT` | STRING_REF | 例如 `CBL` |
| `0x0101` | `SOURCE_RECORD_ID` | STRING_REF | 来源内稳定记录标识 |
| `0x0102` | `SOURCE_URI` | STRING_REF | 来源 URI 或相对来源名 |
| `0x0103` | `IMPORT_NOTE` | STRING_REF | 导入告警或说明；不代替外部日志 |
| `0x0104` | `SOURCE_FORMAT_VERSION` | STRING_REF | 例如候选 CBL 版本 `3` |
| `0x0105` | `SOURCE_LIBRARY_ID` | STRING_REF | 来源棋库或容器的稳定标识 |
| `0x0106` | `SOURCE_LIBRARY_NAME` | STRING_REF | 来源棋库名称 |
| `0x0107` | `SOURCE_CATEGORY` | STRING_REF | 来源目录或分类路径原文 |
| `0x7FFF` | `EXTENDED_METADATA` | STRING_REF | 至多一个规范化扩展元数据文档，见 8.5 |

### 8.5 命名空间扩展元数据

`EXTENDED_METADATA` 用于尚未成为 OXQ 标准字段、格式专有或应用专有的长尾元数据。它不是把任意数据塞进标准字段的逃生口：具有跨格式普遍意义、需要检索或需要校验的字段应该登记为标准 Tag。

每盘棋至多有一个 `EXTENDED_METADATA` Field。它必须是非 critical、非 repeated 的 `STRING_REF`，引用一个采用下述受限 JSON 结构的 UTF-8 字符串：

```json
{
  "org.openxiangqi.cbl": {
    "directory_title": "示例标题",
    "display_index": "12",
    "raw_result": "4",
    "record_type_code": "3",
    "root_marker": "ffffffff"
  },
  "com.example.application": {
    "collection": ["研究", "待复核"]
  }
}
```

JSON 结构被严格限制为：

- 顶层必须是 object；键是 namespace；
- namespace 必须是小写 ASCII 反向域名：至少两个以 `.` 分隔的 segment，每个 segment 以 `a..z` 开头，后续只能是 `a..z`、`0..9` 或内部的 `-`，不得以 `-` 结尾；例如 `org.openxiangqi.cbl`；
- namespace 的值必须是 object；键必须以 `a..z` 开头，后续只能是 `a..z`、`0..9` 或 `_`；
- 属性值只能是 JSON string 或非空的 string array；
- 不允许 number、boolean、null、嵌套 object 或嵌套 array；
- object 中不允许重复键；
- 空字符串与字段不存在含义不同；
- 数值、日期、枚举和二进制文本的解释由 namespace/key 定义，通用核心不猜测类型。

这种“字符串属性包”有意不提供通用数值类型。可查询、可排序的公共数值应成为标准 Tag；来源专有数值可用规范定义的十进制或十六进制字符串保存，从而避免 JSON 浮点精度、整数范围和跨语言实现差异。少量二进制值可以由 namespace 约定为无填充 base64url 或小写十六进制。

规范化扩展 JSON 必须满足：

- 整个字符串遵守 STRING_POOL 的 UTF-8、NFC 和 LF 规则；
- 不含 JSON 语法之外的空白；
- namespace 和属性键分别按 UTF-8 字节无符号字典序排列；
- string array 保留业务顺序和重复项，不擅自排序或去重；
- `"` 必须写为 `\"`，`\` 必须写为 `\\`；U+0008、U+0009、U+000A、U+000C 分别写为 `\b`、`\t`、`\n`、`\f`，其他 U+0000..U+001F 写为小写 `\u00xx`；
- 其他 Unicode 字符直接写为 UTF-8，不使用可选的 `\uXXXX` 转义；
- solidus `/` 不转义；十六进制转义使用小写；
- 同一语义文档必须得到唯一字符串表示。

没有扩展属性时必须省略 `EXTENDED_METADATA` Field，不写空 object。存在的 namespace object 不得为空。

通用 Reader 必须验证这一受限结构，并向上层暴露 namespace 属性包。一个声称“保留未知元数据”的读写器必须保留不理解的 namespace 及其值；只读展示工具可以忽略其语义，但不得把它误报为损坏。转换器若不能把扩展属性写入目标格式，必须报告；严格模式下拒绝转换。

`org.openxiangqi.cbl` 保留给参考 CBL Adapter。建议属性包括 `raw_result`、`record_type_code`、`record_kind_text`、`root_marker`、`root_control`、`directory_title`、`physical_slot` 和 `display_index`。已经有标准 Tag 的文本不得重复保存，除非确实需要区分来源原文与规范化派生值。

逐节点标志、分析数组、图片或大型原始 payload 不属于元数据，不得为了省事放入该 JSON。此类高基数数据应进入相应标准结构或未来的非 critical 专用 Section。CBL Reader 在相关 Section 冻结前可在内存来源视图中保留完整 node control，但转换器必须明确报告 OXQ 尚未承载的部分。

### 8.6 枚举

`DATE_PRECISION`：

| 值 | 含义 |
| ---: | --- |
| `0` | UNKNOWN |
| `1` | YEAR |
| `2` | MONTH |
| `3` | DAY |
| `4` | MINUTE |
| `5` | SECOND |
| `6` | SUBSECOND |

`RESULT`：

| 值 | 含义 |
| ---: | --- |
| `0` | UNKNOWN |
| `1` | RED_WIN |
| `2` | BLACK_WIN |
| `3` | DRAW |
| `4` | UNFINISHED |
| `5` | ABORTED |

START_TIME 和 END_TIME 采用 ISO 8601 扩展形式的下列规范子集，并必须与 DATE_PRECISION 相符：

| DATE_PRECISION | 文本形式 |
| --- | --- |
| `YEAR` | `YYYY` |
| `MONTH` | `YYYY-MM` |
| `DAY` | `YYYY-MM-DD` |
| `MINUTE` | `YYYY-MM-DDThh:mm`，可带时区 |
| `SECOND` | `YYYY-MM-DDThh:mm:ss`，可带时区 |
| `SUBSECOND` | `YYYY-MM-DDThh:mm:ss.fraction`，至少一位小数，可带时区 |
| `UNKNOWN` | 上述任一合法形式 |

时区若存在，只能是 `Z` 或 `±hh:mm`；日期必须是有效的 Gregorian 日期，时、分、秒分别限于 `00..23`、`00..59`、`00..59`。v1.0 不接受基本形式、空格代替 `T`、逗号小数或闰秒 `60`。存在 START_TIME 或 END_TIME 时必须存在 DATE_PRECISION；两个时间都缺失时必须省略 DATE_PRECISION。没有时区信息时不得臆造 `Z` 或本地偏移。

### 8.7 规范化顺序

规范化写入器按 `tag` 升序输出 Field。重复 `TAG` 按其引用字符串的 UTF-8 字节顺序排列并去重；唯一的 `EXTENDED_METADATA` 按 8.5 的规范化 JSON 规则生成。

Metadata 不重复保存 `game_uuid`；Header 中的 UUID 是唯一事实来源。

### 8.8 缺失、空值和未知值

v1.0 中这三种状态不得混同：

- 缺失：不写对应 Metadata Field，或者可选 `string_ref` 为 `0`；
- 存在但为空文本：写入 Field，并引用 STRING_POOL 中 `byte_length == 0` 的记录；
- 已知枚举的未知值：写入该枚举明确定义的 `UNKNOWN` 数值，如 `RESULT=0`。

不存在 `UNKNOWN` 枚举值的标准字段不得臆造零值。转换器不得用空字符串代替缺失字段，也不得用缺失代替来源中明确存在的空文本。未知非 critical 扩展的存在本身不会使文件无效；工具若跳过它，必须降低自己的“无损往返”声明。

## 9. POSITION Section

POSITION 保存棋局根节点对应的完整初始局面，不依赖“默认开局”的隐式假设。

### 9.1 Header

| 偏移 | 大小 | 类型 | 字段 | 含义 |
| ---: | ---: | --- | --- | --- |
| `0x00` | 2 | u16 | `section_version` | `1` |
| `0x02` | 2 | u16 | `header_size` | `16` |
| `0x04` | 2 | u16 | `variant` | `1` 表示中国象棋 |
| `0x06` | 1 | u8 | `side_to_move` | `0` 红方，`1` 黑方 |
| `0x07` | 1 | u8 | `position_flags` | v1 必须为 `0` |
| `0x08` | 2 | u16 | `fullmove_number` | 从 `1` 开始；未知时也写 `1` |
| `0x0A` | 2 | u16 | `piece_count` | `0..32` |
| `0x0C` | 4 | u32 | `reserved` | 必须为 `0` |

Header 后紧跟 `piece_count` 条 Piece Record，不得有额外尾部数据。

### 9.2 坐标系统

棋盘为 9 列 × 10 行。

- 从红方视角观察棋盘；
- `file` 从左向右为 `0..8`；
- `rank` 从红方底线向黑方底线为 `0..9`；
- `square = rank * 9 + file`，有效范围为 `0..89`。

从红方视角观看时，坐标如下。图的上方是黑方底线，下方是红方底线：

```text
                    file
          0   1   2   3   4   5   6   7   8
        +---+---+---+---+---+---+---+---+---+
rank 9  |81 |82 |83 |84 |85 |86 |87 |88 |89 |  黑方底线
rank 8  |72 |73 |74 |75 |76 |77 |78 |79 |80 |
rank 7  |63 |64 |65 |66 |67 |68 |69 |70 |71 |
rank 6  |54 |55 |56 |57 |58 |59 |60 |61 |62 |
rank 5  |45 |46 |47 |48 |49 |50 |51 |52 |53 |
rank 4  |36 |37 |38 |39 |40 |41 |42 |43 |44 |
rank 3  |27 |28 |29 |30 |31 |32 |33 |34 |35 |
rank 2  |18 |19 |20 |21 |22 |23 |24 |25 |26 |
rank 1  | 9 |10 |11 |12 |13 |14 |15 |16 |17 |
rank 0  | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |  红方底线
        +---+---+---+---+---+---+---+---+---+
                         红方
```

四角因此分别为：红方视角左下 `0`、右下 `8`、左上 `81`、右上 `89`。

该坐标是存储坐标，与中文纵线记谱中的“一至九”以及某些 FEN 字符串方向无直接等同关系。适配器必须显式转换，不能直接复制外部坐标值。

### 9.3 Piece Record

每条记录固定为 4 字节：

| 偏移 | 大小 | 类型 | 字段 | 含义 |
| ---: | ---: | --- | --- | --- |
| `0x00` | 1 | u8 | `piece_code` | 颜色和棋子种类 |
| `0x01` | 1 | u8 | `square` | `0..89` |
| `0x02` | 2 | u16 | `piece_flags` | v1 必须为 `0` |

`piece_code`：

- bit 7：颜色，`0` 红方，`1` 黑方；
- bit 6..4：必须为 `0`；
- bit 3..0：棋子种类。

| 种类值 | 棋子 |
| ---: | --- |
| `1` | KING（帅/将） |
| `2` | ADVISOR（仕/士） |
| `3` | ELEPHANT（相/象） |
| `4` | HORSE（马） |
| `5` | ROOK（车） |
| `6` | CANNON（炮） |
| `7` | PAWN（兵/卒） |

种类 `0` 和 `8..15` 在 v1 中无效。

同一 square 不得出现多枚棋子。规范化写入器按 `square` 升序输出 Piece Record。

格式层允许少于 32 枚棋子和非常规组成，以支持残局、排局以及来源不完整的数据。完整规则合法性不属于二进制结构解析的前置条件。

## 10. Move 编码

Move 固定为 4 字节：

| 偏移 | 大小 | 类型 | 字段 | 含义 |
| ---: | ---: | --- | --- | --- |
| `0x00` | 1 | u8 | `from_square` | `0..89` |
| `0x01` | 1 | u8 | `to_square` | `0..89` |
| `0x02` | 1 | u8 | `move_kind` | v1 正常着法为 `0` |
| `0x03` | 1 | u8 | `move_flags` | v1 必须为 `0` |

根节点没有着法，其 4 个 Move 字节必须全部为 `0xFF`。

OXQ 不保存“吃子”“将军”“绝杀”等可从局面推导的缓存标志，以避免缓存与棋局事实不一致。外部格式中的此类信息如具有不可替代的说明价值，应转成注释或未来扩展数据。

## 11. MOVE_TREE Section

MOVE_TREE 使用 First Child / Next Sibling 表示有序变化树。

### 11.1 Header

| 偏移 | 大小 | 类型 | 字段 | v1 值 |
| ---: | ---: | --- | --- | --- |
| `0x00` | 2 | u16 | `section_version` | `1` |
| `0x02` | 2 | u16 | `header_size` | `16` |
| `0x04` | 2 | u16 | `node_record_size` | `32` |
| `0x06` | 2 | u16 | `tree_flags` | `0` |
| `0x08` | 4 | u32 | `node_count` | 至少为 `1` |
| `0x0C` | 4 | u32 | `root_index` | v1 必须为 `0` |

Header 后紧跟 `node_count` 条 Node Record。

### 11.2 Node Record

每条记录固定为 32 字节：

| 相对偏移 | 大小 | 类型 | 字段 | 含义 |
| ---: | ---: | --- | --- | --- |
| `0x00` | 4 | u32 | `parent_index` | 父节点 |
| `0x04` | 4 | u32 | `first_child_index` | 第一个子变化 |
| `0x08` | 4 | u32 | `next_sibling_index` | 下一个同级变化 |
| `0x0C` | 4 | u32 | `first_annotation_id` | 首条注释 ID，`0` 表示无 |
| `0x10` | 4 | bytes | `move` | 第 10 节定义的 Move |
| `0x14` | 4 | u32 | `ply` | 根为 0，子节点为父节点加 1 |
| `0x18` | 4 | u32 | `node_flags` | v1 必须为 `0` |
| `0x1C` | 4 | u32 | `reserved` | 必须为 `0` |

不存在的节点索引统一写为 `0xFFFFFFFF`。

### 11.3 树约束

- 节点 0 是唯一根节点；
- 根节点 `parent_index == 0xFFFFFFFF`、`ply == 0`，Move 全为 `0xFF`；
- 非根节点必须有有效父节点和正常 Move；
- 每个非根节点必须且只能出现在一个父节点的 child/sibling 链中；
- 所有节点必须从根可达；
- child/sibling 链不得成环；
- `parent_index` 必须与 child/sibling 关系一致；
- 第一子节点表示首选变化或主线，其余兄弟按展示顺序排列；
- 节点索引必须小于 `node_count`，或等于不存在值。

规范化写入器按深度优先先序分配节点索引：根节点、第一子树、第二子树，依此类推。这使同一有序树得到确定的索引布局。

## 12. ANNOTATION Section

ANNOTATION 保存与节点关联的注释。一个节点可以通过单向链关联多条注释。

### 12.1 Header

| 偏移 | 大小 | 类型 | 字段 | v1 值 |
| ---: | ---: | --- | --- | --- |
| `0x00` | 2 | u16 | `section_version` | `1` |
| `0x02` | 2 | u16 | `header_size` | `16` |
| `0x04` | 2 | u16 | `record_size` | `24` |
| `0x06` | 2 | u16 | `annotation_flags` | `0` |
| `0x08` | 4 | u32 | `annotation_count` | 可以为 `0` |
| `0x0C` | 4 | u32 | `reserved` | `0` |

### 12.2 Annotation Record

每条记录固定为 24 字节：

| 相对偏移 | 大小 | 类型 | 字段 | 含义 |
| ---: | ---: | --- | --- | --- |
| `0x00` | 4 | u32 | `annotation_id` | 从 `1` 开始的唯一 ID |
| `0x04` | 4 | u32 | `next_annotation_id` | 下一条 ID，`0` 表示无 |
| `0x08` | 2 | u16 | `kind` | 注释类型 |
| `0x0A` | 2 | u16 | `flags` | 注释位置等标志 |
| `0x0C` | 4 | u32 | `text_ref` | 注释正文，必须非零 |
| `0x10` | 4 | u32 | `author_ref` | 作者；可以为 `0` |
| `0x14` | 4 | u32 | `language_ref` | BCP 47 语言标签；可以为 `0` |

`kind`：

| 值 | 名称 | 含义 |
| ---: | --- | --- |
| `1` | `COMMENT` | 普通人类注释 |
| `2` | `SOURCE_NOTE` | 来源格式中的说明或无法结构化的文本 |

`flags`：

| 位 | 名称 | 含义 |
| ---: | --- | --- |
| 0 | `BEFORE_MOVE` | 文本位于该节点着法之前 |
| 1–15 | 保留 | 必须为零 |

根节点的注释默认描述初始局面；根节点不得设置 `BEFORE_MOVE`。

注释约束：

- `annotation_id` 必须等于记录索引加 1；
- `next_annotation_id` 必须为 0 或有效 ID；
- 每条注释最多属于一个节点的注释链；
- 注释链不得成环；
- 所有注释都必须被某个节点引用，不得存在不可达记录；
- `text_ref` 必须引用 STRING_POOL 中的有效记录。

AI 分析不使用 `COMMENT` 冒充结构化结果。未来 AI_RESULT Section 将单独定义；在此之前只能将来源中的纯文本引擎说明保存为 `SOURCE_NOTE`。

## 13. 引用和跨 Section 校验

解析器在访问引用前必须完成以下顺序：

1. 校验 Header 和 Header CRC；
2. 校验 Section Table 范围和 CRC；
3. 校验所有 Section 的范围、重叠、标志和 Payload CRC；
4. 解析 STRING_POOL 并建立合法记录起点集合；
5. 解析其他 Section 的固定结构；
6. 校验所有 string_ref；
7. 校验 Node 和 Annotation 图；
8. 可选地重放着法进行棋局语义校验。

任何引用不得导致解析器读取目标 Section 之外的字节。

## 14. 校验层级

OXQ 工具应区分三个校验层级。

### 14.1 结构校验 `structural`

必须实现，包括：

- Magic、版本、长度和 CRC；
- Section 数量、范围、对齐和重叠；
- 固定字段、保留位和枚举值；
- UTF-8、TLV 和 string_ref；
- Node/Annotation 索引、可达性和无环性。

结构校验失败时文件不是有效 OXQ v1。

### 14.2 状态一致性校验 `state`

MVP 应实现，包括沿每条变化重放：

- from square 存在棋子；
- from square 棋子颜色与当前行棋方一致；
- to square 不包含同色棋子；
- 每步之后正确切换行棋方；
- 同一父局面的每个子变化独立重放。

### 14.3 完整规则校验 `rules`

可以实现，包括棋子走法、蹩马腿、塞象眼、九宫、过河、炮架、将帅照面、将军应对等完整规则。

结构解析不得依赖完整规则引擎。对于历史资料、排局或来源异常数据，工具可以允许结构有效但规则校验失败，同时必须明确报告。

## 15. 错误模型

参考实现的解析错误至少应提供：

- 稳定错误代码；
- 文件绝对偏移；
- 所属 Section（若已知）；
- 简短可读信息；
- 相关的期望值和实际值（适用时）。

建议错误类别：

- `INVALID_MAGIC`
- `UNSUPPORTED_VERSION`
- `INVALID_HEADER`
- `SIZE_MISMATCH`
- `INTEGER_OVERFLOW`
- `CRC_MISMATCH`
- `INVALID_SECTION_TABLE`
- `SECTION_OUT_OF_RANGE`
- `SECTION_OVERLAP`
- `UNKNOWN_CRITICAL_SECTION`
- `INVALID_UTF8`
- `INVALID_STRING_REF`
- `INVALID_METADATA`
- `INVALID_POSITION`
- `INVALID_MOVE`
- `INVALID_TREE`
- `INVALID_ANNOTATION`
- `RESOURCE_LIMIT`

解析器不得在格式错误上崩溃、无限循环或进行由输入值直接决定的无上限内存分配。

## 16. 资源限制与安全要求

编码字段允许的理论上限不等于实现必须分配的资源。实现可以配置更低限制，但必须以 `RESOURCE_LIMIT` 明确报告，不能把它误报为格式损坏。

参考实现默认限制为：

- 文件大小：1 GiB；
- Section 数量：1024；
- 节点数量：10,000,000；
- 注释数量：10,000,000；
- 单字符串长度：16 MiB；
- `EXTENDED_METADATA` JSON：1 MiB；超过通常说明数据应迁移到专用 Section；
- 字符串总大小：512 MiB；
- Metadata Field 数量：65,536；
- 树深度：1,000,000，遍历应使用显式栈而非程序递归栈。

实现必须：

- 对所有 `offset + length`、`count * record_size` 使用安全算术；
- 在分配前校验数量和长度；
- 防止循环引用造成死循环；
- 不执行注释或元数据中的内容；
- 不将文件内字符串当作本地文件路径自动访问。

## 17. 规范化和确定性序列化

规范化 OXQ 必须满足：

- Header 和 Section Table 使用本规范规定的布局；
- Section Entry 按 Section ID 升序；
- Section Payload 按 Section ID 升序；
- 所有填充和保留字节为零；
- Piece Record 按 square 升序；
- Node 按深度优先先序编号；
- Annotation ID 连续，并按节点先序和链内顺序分配；
- Metadata 按 tag 排序，重复标签按值排序；
- 字符串 NFC、LF 换行、去重并按 UTF-8 字节排序；
- 不写入未定义的缓存或随机填充；
- UUID 已确定后不因重新序列化改变。

规范化 Writer 必须写入 `version_major=1`、`version_minor=0` 和 `header_size=64`。如果它选择保留未知非 critical Section，必须原样保留该 Section 的 type、flags 和 Payload，按 Section ID 与标准 Section 一起排序并重新计算偏移与 CRC。不理解某扩展的 Writer 可以删除该非 critical Section，但必须向调用者报告语义可能丢失，严格无损模式下必须拒绝重写。

两个语义相同且 UUID 相同的 Game Model，经同版本规范化写入器处理后应产生完全相同的字节序列。

文件时间、导入时间等非棋局事实不得隐式写入 OXQ，否则会破坏确定性；如业务确需保存，必须作为明确元数据并参与语义比较。

## 18. CBL 导入映射要求

CBL 是 MVP 的首个输入格式，但 CBL 的内部布局不属于 OXQ 规范。

CBL 导入器必须遵循：

- 一个 CBL 棋库转换为零到多个 OXQ；
- 每个 CBL 棋局记录对应一个独立 OXQ；
- CBL 库名不是单局标题，除非来源明确如此表达；
- CBL 库级作者信息不得无条件冒充每盘棋的注释作者；
- 坐标必须转换为第 9.2 节坐标系；
- 来源文本必须转换为合法 UTF-8；
- 主线和分支顺序必须保留；
- 无法映射的数据必须告警，不得静默丢弃；
- `SOURCE_FORMAT` 写为 `CBL`；
- 已确认版本应写入 `SOURCE_FORMAT_VERSION`；
- 如 CBL 有稳定记录 ID，应写入 `SOURCE_RECORD_ID`；
- UUIDv5 的命名空间和名称构造算法必须由转换器规范另行固定。

CBL 中已有标准 Tag 的字段必须优先写入标准 Tag。例如红黑队、用时规则、组别、台号、裁判、记录者、解说者、创建者及记录时间不得仅塞入 `IMPORT_NOTE`。无法标准化但必须保留的 CBL 原值使用 `org.openxiangqi.cbl` 扩展属性。

CBL `Result == 4` 表示“多重结果”，不得错误映射成 OXQ `UNFINISHED`；应将标准 `RESULT` 写为 `UNKNOWN`，并把原始数值及 `ResultType` 保存在扩展元数据中。CBL 的 `FullmoveNumber == 0` 等超出 OXQ 标准域的值也必须保留来源原值并产生告警。

### 18.1 导出到象棋桥兼容 CBL

OXQ 文件本身不要求被象棋桥直接打开。兼容保证由 OXQ → CBL Adapter 提供：

- 输出当前明确支持的 CBL v3 容器和 Record；
- 输出文件必须能被转换器声明的象棋桥版本打开；
- 初始局面、先行方、着法顺序、变化顺序、注释和 CBL 可表达的标准元数据必须保持语义等价；
- `org.openxiangqi.cbl` 中已理解且可写回的字段应该恢复到对应 CBL 字段；
- OXQ 独有或 CBL 无法表达的数据必须列入转换报告；
- 严格模式只要存在未表示的语义字段就必须拒绝生成 CBL；宽松模式可以生成，但必须返回非静默告警。

“象棋桥最新版”必须在实现和测试报告中落实为明确版本号与可执行文件哈希，不能作为随时间变化而无测试依据的泛称。第一份兼容基线是本项目用于逆向的象棋桥 3.0 beta4 / 程序版本 3.0.0.4；新增版本应单独回归。

仅识别出 `CCBridgeLibrary` 文件头不足以证明文件中的棋局记录已被正确解析。

## 19. 最小合法 OXQ

最小合法文件仍然必须包含五个标准 Section：

- GAME_METADATA：零个 Field；
- POSITION：至少包含一种结构有效的初始局面，棋子数可以为零；
- MOVE_TREE：只有根节点；
- ANNOTATION：零条记录；
- STRING_POOL：零条字符串记录。

但“零棋子棋局”只用于格式测试，不代表规则合法的中国象棋棋局。

## 20. 实现检查清单

写入器：

- [ ] 生成非零 RFC UUID；
- [ ] 规范化所有字符串；
- [ ] 规范化 Metadata、棋子、节点和注释顺序；
- [ ] 检查所有引用和树约束；
- [ ] 写入零填充和零保留字段；
- [ ] 计算 Payload、Section Table 和 Header CRC；
- [ ] 回填准确 `file_size`；
- [ ] 输出后可以用同一读取器重新解析并比较语义。

读取器：

- [ ] 在任何内存分配和数据访问前检查长度与溢出；
- [ ] 按正确顺序验证三级 CRC；
- [ ] 拒绝未知 critical 能力；
- [ ] 校验 UTF-8 和 string_ref 起点；
- [ ] 校验树、注释链和跨 Section 引用；
- [ ] 区分格式错误、资源限制和规则错误；
- [ ] 不依赖宿主结构体布局或未对齐读取；
- [ ] 对畸形输入返回错误而不是崩溃。

## 21. v1.0 已冻结决策

本节记录 v1.0 评审期间的已决事项。这些选择在 v1.0 参考实现和测试向量中均按当前结论执行；如需改变磁盘语义，必须按版本兼容规则演进，不得在同一 v1.0 声明下静默修改。

### 21.1 Section ID 登记

v1.0 不建立第三方公开 Section ID 登记表。未来标准 Section ID 由后续 OXQ 规范分配；实验性非元数据能力使用 `0x80000000..0xFFFFFFFF` 私有实验范围，且必须为 non-critical。元数据扩展继续使用第 8.5 节命名空间机制。

### 21.2 Metadata Summary

v1.0 不增加独立 Summary 固定头。摘要信息从 `GAME_METADATA` 及其他标准 Section 读取；快速索引属于外部容器或未来 OXQP 的职责。

### 21.3 POSITION 计数器

v1.0 不增加 half-move clock 或可逆着法计数器。`fullmove_number` 按第 9.1 节保留。来源格式中的额外计数器若不能无损映射，转换器必须使用带命名空间的扩展元数据保留或明确报告；该计数器不被声明为完整象棋规则裁决状态。

### 21.4 Annotation 表达

v1.0 只支持第 12 节定义的纯文本 `COMMENT` 和 `SOURCE_NOTE`，不定义富文本、图形标记或更复杂的结构化来源字段。

### 21.5 AI_RESULT

v1.0 不定义、不保留公共 `AI_RESULT` Section ID。引擎评分、深度、PV、MultiPV、引擎身份和分析节点等结构在未来规范中一并定义。实验实现只能使用私有非 critical Section；纯文本引擎说明可按第 12 节使用 `SOURCE_NOTE`。

### 21.6 UUIDv5 命名空间

OXQ v1.0 磁盘格式不定义全局官方 UUIDv5 命名空间，也不把某个来源格式的身份构造规则写入通用格式。需要稳定导入身份的 Adapter 必须在自身转换契约中冻结 namespace UUID、名称字节序列和规范化方法。参考 CBL Adapter 必须在首个稳定版本和黄金向量生成前完成该决策；同一 Adapter 版本对同一来源记录必须生成相同 UUID。

### 21.7 全文件尾部校验

v1.0 不增加全文件尾部校验或 Footer。Reader 仍必须验证 `file_size`、Header CRC、Section Table CRC、每个 Payload CRC、所有区间与填充规则。未登记的非零尾部或其他规范区域之外附加数据不因本决策而变得合法；扩展必须使用 Section 机制。

### 21.8 默认资源限制

v1.0 参考实现保留第 16 节的默认限制。它们是参考实现策略，不是磁盘字段的新上限；其他实现可使用更低限制，但必须返回 `RESOURCE_LIMIT` 而非把文件误报为格式损坏。超大型研究棋谱的性能调整不改变 v1.0 磁盘布局。

### 21.9 Unicode NFC

v1.0 要求规范化 Writer 将所有文本转换为 NFC，Reader 必须接受合法 UTF-8 但非 NFC 的非规范化文件，具体行为见第 7 节。确定性保证适用于规范化写入结果，不要求 Reader 把所有可读文件都误报为规范化编码。

至此，v1.0 磁盘格式不再保留开放设计问题。手工测试向量位于 [`test/vectors/oxq-v1`](../test/vectors/oxq-v1/README.md)；参考实现和独立 Reader 可读性验证完成后，可以去除 RC 标记并发布最终 v1.0，不再修改同版本磁盘语义。
