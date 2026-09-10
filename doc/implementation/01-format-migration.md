# oxq-format 迁移实施方案

## 目标

在不改变 OXQ v1 字节语义、不打断现有 consumer 的前提下，把 `oxq-core` 收敛为含义明确的 `oxq-format`。迁移不是重写 Codec，也不同时重排文件格式。

## 当前基线

当前公共接口位于 `src/core/include/oxq/core/`，CMake 实体 target 为 `oxq_core`，导出名为 `core`，安装库名为 `oxq-core`。`oxq-convert` 公开依赖 `OXQF::core`，CLI、测试和独立 consumer 也使用旧入口。

现有 `GameModel` 包含 UUID、元数据、初始局面和 `MoveTree`；Reader 返回模型与规范化诊断，Writer 在写入前做模型校验。这些行为必须保持。

## 目标公共 API

```cpp
#include <oxq/format/document.hpp>
#include <oxq/format/reader.hpp>
#include <oxq/format/writer.hpp>
#include <oxq/format/validation.hpp>

namespace oxq::format {
using NodeId = std::uint64_t;

struct GameDocument; // 持久化模型
ReaderOutcome read_oxq(std::span<const std::byte>, const ReaderLimits& = {});
WriterOutcome write_oxq(const GameDocument&, const WriterLimits& = {});
std::vector<ValidationIssue> validate(const GameDocument&, const ValidationLimits& = {});
}
```

`GameModel` 到 `GameDocument` 的类型名迁移可以晚于库名迁移，但两者不能在同一版本中成为字段逐渐分叉的独立类型。推荐先 `using GameDocument = GameModel`，等调用点稳定后再完成主类型重命名。

## 分步变更

### F1：建立兼容外壳

- 将实体 target 改为 `oxq_format`，输出名设为 `oxq-format`；
- 增加 build-tree alias `OXQF::format`；
- 安装导出名为 `format`；
- 保留 `OXQF::core` 兼容入口；如果 CMake alias 不能随 install export，使用 `OXQFConfig.cmake.in` 创建兼容 imported interface target；
- 新增 `oxq/format/` 公共头；旧 `oxq/core/` 头只转发并标注弃用周期；
- `oxq-convert` 改为链接 `OXQF::format`。

验收：源码内不再新增 `OXQF::core` 调用；新旧 consumer 都能在隔离安装目录中编译、链接、运行。

### F2：命名空间迁移

- 实现进入 `oxq::format`；
- 在兼容头提供旧名称映射；
- 不使用全量 `namespace core = format`，若未来 `core` 有其他符号会造成不可控暴露；优先在兼容头按公共符号声明 `using`；
- 编译时弃用警告默认不破坏使用 `-Werror` 的第三方构建，弃用可先通过文档和 CMake message 提示。

验收：所有仓内产品仅 include `oxq/format/*`，旧 header smoke test 单独保留。

### F3：领域名收敛

- 引入 `GameDocument`；
- 保持字段和相等比较语义；
- Reader/Writer、CBL adapter、CLI 统一采用新名；
- 对 `MoveTree` 的直接 `nodes` 写访问建立审计清单。编辑器不得依赖 `rebuildIndex()` 修补任意外部修改，而通过自己的安全操作层维护不变量。

验收：Writer 对迁移前后构造的同一模型产生完全相同的规范化字节和 SHA-256。

### F4：目录迁移

- `src/core` 移至 `libs/oxq-format`；
- `src/convert`、`src/cli` 分别移至目标布局；
- 更新 CMake、脚本、fuzz corpus 路径、安装清单和文档链接；
- 一次只移动一个 target，移动提交不混入行为更改。

## 兼容性测试矩阵

| 场景 | include | link target | 预期 |
|---|---|---|---|
| 新 consumer | `oxq/format/*` | `OXQF::format` | 通过 |
| 旧 consumer | `oxq/core/*` | `OXQF::core` | 通过并有迁移说明 |
| 混合依赖 | 新旧均有 | 两入口间接出现 | 只有一个实体库，无重复符号 |
| convert | `oxq/format/*` | `OXQF::format` | CBL 金标不变 |
| CLI | 新入口 | 间接链接 | 输出协议和退出码不变 |

## 回滚与风险控制

- 每个阶段先比较全部 `test/vectors/oxq-v1` Writer 输出；任何字节变化都阻止合并；
- 保留上一发布包作为 external consumer 样本；
- 不在更名提交中修改默认限制、校验严重级别或错误码；
- 安装树和 build tree 都要测试，因为 CMake alias 在二者中的行为不同；
- 若兼容 target 无法可靠导出，宁可保留一个小型 `INTERFACE` target，也不要生成第二个静态库。

## 完成定义

- 仓库主文档只把 `oxq-format` 称为格式实现；
- 新 API、旧兼容 API、安装包 consumer 测试均通过；
- Linux/Windows release 包包含新头和兼容头；
- CHANGELOG 写明兼容周期及删除版本；
- 格式向量、CBL 语义基线和跨平台 Writer 指纹零变化。
