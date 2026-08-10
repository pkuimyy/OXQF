# OXQ v1 手工测试向量

本目录的二进制文件直接按 [`spec/oxq-v1.md`](../../../spec/oxq-v1.md) 的字段布局构造，不由 `oxq-core` Reader/Writer 生成。它们用于防止参考实现的偶然行为取代规范。本目录随项目按根目录 MIT License 分发。

`manifest.json` 是机器可读真值，记录每个向量的 SHA-256、Section 偏移、Payload CRC-32C 和预期语义或错误。

## 向量

| 文件 | 用途 |
| --- | --- |
| `minimal.oxq` | 360 字节的最小合法文件；五个标准 Section，零棋子，只有根节点 |
| `variation-zh.oxq` | 中文标题、多行根注释、BCP 47 语言标签和有序变化树 |
| `unknown-noncritical.oxq` | 额外的 `0x80000001` non-critical Section，Payload 为 `DE AD BE EF` |
| `invalid-magic-high-bit.oxq` | 只破坏 Magic 偏移 `0` 的高位字节 |
| `invalid-magic-crlf.oxq` | 只破坏 Magic 偏移 `4` 的 CR/LF 标记 |
| `invalid-magic-eof.oxq` | 只破坏 Magic 偏移 `6` 的 EOF 标记 |

三个损坏向量都从 `minimal.oxq` 派生，仅修改指定 Magic 字节，然后重算 Header CRC。因此它们只有一个格式故障，预期错误均为 `INVALID_MAGIC`。

## 最小向量字节布局

| 区间 | 大小 | 内容 |
| --- | ---: | --- |
| `0x000..0x03F` | 64 | File Header |
| `0x040..0x107` | 200 | 5 条 Section Entry |
| `0x108..0x10F` | 8 | GAME_METADATA |
| `0x110..0x11F` | 16 | POSITION |
| `0x120..0x14F` | 48 | MOVE_TREE |
| `0x150..0x15F` | 16 | ANNOTATION |
| `0x160..0x167` | 8 | STRING_POOL |

## 维护

修改向量定义后显式执行：

```bash
npm run vectors:write
```

CI 只执行检查模式，不会自动覆盖已提交向量。更改二进制文件时必须同时人工审查字段定义、`manifest.json` 语义真值和本文档。
