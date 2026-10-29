# 测试与发布验证

本文说明现有测试的复现入口、覆盖边界及 1.0.0 发布结果记录要求。接口契约见 [API](API.md)。

## 当前状态与环境

本次文档整理只核对代码和测试定义，**未执行构建、QEMU 或实机测试**。以下命令不代表通过记录；尚未建立经验证的 Zephyr/SDK 兼容矩阵。

需要已初始化的 Zephyr workspace、Python、west、CMake（测试工程最低 3.20.0）、目标工具链和支持 qemu_cortex_m3 的 QEMU。激活环境并确保 `ZEPHYR_BASE` 指向对应 Zephyr 源码。当前 Zephyr 版本必须包含该板目标。

所有命令从模块根目录运行。输出使用临时目录，避免写入共享模块工作树。

## 自动化执行

入口是 `tests/testcase.yaml`，测试代码使用 Ztest：

| 场景 | 平台 | 配置与预期 |
|---|---|---|
| modbus_reg_service.core_names | qemu_cortex_m3 | tests/prj.conf，名称默认开启，包含 Shell attach 用例 |
| modbus_reg_service.core_without_names | qemu_cortex_m3 | 额外关闭名称；名称查找返回 ENOTSUP，Shell 因依赖不满足关闭 |

PowerShell 中运行两个场景：

```powershell
$testOutput = Join-Path ([System.IO.Path]::GetTempPath()) ("modbus-reg-twister-" + [guid]::NewGuid().ToString('N'))
python "$env:ZEPHYR_BASE/scripts/twister" -T tests -p qemu_cortex_m3 --inline-logs --outdir "$testOutput"
```

检查退出码、汇总报告及日志，应为两个场景实际执行成功；跳过、被过滤或仅构建不算运行通过。保存输出目录中的报告和失败日志，核对实际 `.config` 的服务、名称和 Shell 取值。

单独排查默认场景可执行：

```powershell
$buildOutput = Join-Path ([System.IO.Path]::GetTempPath()) ("modbus-reg-build-" + [guid]::NewGuid().ToString('N'))
west build -b qemu_cortex_m3 -s tests -d "$buildOutput"
west build -d "$buildOutput" -t run
```

关闭名称时使用现有配置片段：

```powershell
$noNamesOutput = Join-Path ([System.IO.Path]::GetTempPath()) ("modbus-reg-no-names-" + [guid]::NewGuid().ToString('N'))
west build -b qemu_cortex_m3 -s tests -d "$noNamesOutput" -- -DEXTRA_CONF_FILE=prj_no_names.conf
west build -d "$noNamesOutput" -t run
```

若换用其他 Zephyr 版本，先核对 Twister 参数和板名。构建失败先检查环境、工具链和配置，不能通过修改其他共享子模块绕过失败。

## 现有用例覆盖

源文件为 `tests/src/main.c`，每项测试前重新初始化。名称开启时有 10 项，关闭时移除条件编译的 Shell attach 测试，保留 9 项。

| 测试函数后缀 | 实际覆盖 |
|---|---|
| names_are_generated_from_ids | ID 生成名称，关闭名称返回 ENOTSUP |
| u32_is_one_atomic_logical_field | U32 默认值两字映射和有类型读写；未进行并发测试 |
| float_and_double_round_trip | F32/F64 正常值往返 |
| u64_round_trip_and_word_order | U64 高字优先映射和读写 |
| shell_can_attach_initialized_service | 已初始化实例 attach，不验证命令输出 |
| schema_rejects_overlapping_fields | 拒绝重叠字段 |
| typed_api_rejects_wrong_type | U32 经 U16 getter 访问返回 EINVAL |
| reset_restores_defaults | 修改 U32 后 reset 恢复默认值 |
| external_permissions_and_sparse_hole | 拒绝外部写 IR/DI，允许写 HR/Coil，HR U32 映射，未定义地址报错 |
| internal_bulk_write_can_publish_read_only_spaces | 内部批量写 DI/IR |

## 待补充验证

以下未被现有用例完整覆盖，发布时应补充证据或明确列为未验证项：

- 多线程竞争、批量快照、reset 并发及 ISR 限制。
- 低字优先、部分多字字段写入、跨相邻字段访问。
- 区间尾部遇空洞无部分写入、地址末端、越界、零数量和 NULL 参数。
- 重复 ID/名称、未排序、非法类型、空 bank、未初始化等异常路径。
- 多实例隔离、资源占用、最大产品表访问耗时。
- Shell 未 attach、未知名称和实际 list/get 输出。
- 目标 MCU 浮点表示、协议字节序、请求上限、异常映射和外部写后业务动作。

### 产品接入验收

1. 按 README 接入，确认 init 返回 0；定义 BIT、U16 和多字字段，核对默认值、ID 更新及地址读回。
2. 用主站读四个数据区，核对零基地址、宽度、字序；确认外部 DI/IR 写入被拒绝。
3. 请求未定义地址及跨空洞区间，确认异常映射正确，失败写入未改变已有值。
4. attach 后执行 `modbus_reg list` 和 `modbus_reg get <name>`，核对原始字。
5. 修改后 reset，确认恢复默认值；重启后按产品启动策略重新初始化，不能把 RAM 值视为持久化值。

## 发布结果记录

实际执行后填写，保留日志。未执行项不写通过，仅构建成功应标明“仅构建”；日期使用实际执行日期。

| 项目 | 记录 |
|---|---|
| 模块提交 / tag | 待填写 |
| 执行日期 / 执行者 | 待填写 |
| 主机、Python、west、CMake | 待填写 |
| Zephyr 版本 / 提交 | 待填写 |
| SDK/编译器 / QEMU 版本 | 待填写 |
| core_names | 未执行 |
| core_without_names | 未执行 |
| 产品板型号 / 产品提交 | 待填写 |
| 产品接入和主站验证 | 未执行 |
| 补充验证 / 未验证限制 | 待填写 |
| 报告与日志位置 | 待填写 |

发布前核对 README、API 和 CHANGELOG 与待发布提交一致，完成约定验证后再创建正式 tag。本文不表示已完成发布流程。
