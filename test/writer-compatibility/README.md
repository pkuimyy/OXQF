# CBL Writer 象棋桥兼容性核对

本目录用于 M5 的 Windows 实机打开验证。生成物不进入版本库；生成器固定库 UUID、游戏 UUID、时间、目录顺序和内容，因此同一提交重复生成应逐字节一致。

从 WSL2 生成到 Windows 文件系统：

```bash
cmake --preset dev
cmake --build --preset dev --target oxq_convert_cbl_writer_compatibility_vectors
./build/dev/test/oxq_convert_cbl_writer_compatibility_vectors \
  /mnt/c/Temp/oxqf-m5-writer
sha256sum /mnt/c/Temp/oxqf-m5-writer/*.CBL
```

分别使用象棋桥 3.0 beta4 和 3.0.0.4 打开四个文件。不要通过另存为覆盖原测试文件；若软件会自动修改棋库，先复制一份再打开。

| 文件 | 预期局数 | 核对重点 | beta4 | 3.0.0.4 |
| --- | ---: | --- | --- | --- |
| `m5_writer_00_empty.CBL` | 0 | 空库正常打开 | 待验证 | 待验证 |
| `m5_writer_01_metadata_mainline.CBL` | 1 | 中英文元数据、标准局面、两步主线、着法注释 | 待验证 | 待验证 |
| `m5_writer_02_custom_variations.CBL` | 1 | 自定义局面、23 回合、根注释、两个有序变化、emoji | 待验证 | 待验证 |
| `m5_writer_03_two_games.CBL` | 2 | 局数、目录顺序、两局标题及各自内容 | 待验证 | 待验证 |

验证后记录软件显示的局数、标题、初始局面、主线/变化顺序和注释；若 emoji 显示为替代字符，需区分是象棋桥 UI 字体/UTF-16 显示问题，还是重新读取文件后字符已被修改。

当前生成器的预期 SHA-256（文件内容变化时必须审查并同步更新）：

```text
f719d8496113a222a7fd113a927076624b1d03b9b6b498f3503c551fe45758aa  m5_writer_00_empty.CBL
729bcdd6fc87a628baaa963f2f9fd60e5604a01e8eed9b08963718b03e56c4a9  m5_writer_01_metadata_mainline.CBL
6dfb580fa445f86b8fb385d9fb7adfeb17fb6542595136d2de36b3ba018bc8b1  m5_writer_02_custom_variations.CBL
aeaa59f1981204a98b65589ab2e3ed602a5117e33993daea815afd61748d0627  m5_writer_03_two_games.CBL
```
