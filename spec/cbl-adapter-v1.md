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
