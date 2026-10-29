# modbus_reg_service

`modbus_reg_service` 是一个无持久化、静态分配、线程安全的稀疏 Modbus
寄存器服务。产品代码通过稳定 ID 读写业务字段；Modbus 协议适配层才通过
“数据区 + 地址”批量访问；名称只用于 Shell 和诊断。

## 接入与依赖

本文面向计划发布的 1.0.0，当前不表示已发布。参见 [API](docs/API.md)、[测试与验证](docs/TESTING.md)、[变更记录](CHANGELOG.md)。

模块依赖 Zephyr 内核 mutex 和工具宏；可选诊断功能依赖 Zephyr Shell。无 devicetree、网络、UART 或 Flash 依赖，不提供 RTU/TCP 传输、报文解析、业务值校验或写入通知。要求 32 位 float 和 64 位 double，编译时检查宽度，浮点数据按本机位表示保存。

目前通过显式 CMake/Kconfig 接入，未提供 `zephyr/module.yml` 自动发现。已有 `qemu_cortex_m3` 测试配置，但尚未建立经验证的 Zephyr/SDK 版本兼容矩阵，发布前应记录实际环境和结果。

### 推荐接入方式

以下使用消费项目路径 `shared/modbus_reg_service`。独立仓库及 tag 发布后，在消费项目根目录执行：

```sh
git submodule add <模块仓库URL> shared/modbus_reg_service
git -C shared/modbus_reg_service checkout v1.0.0
git add .gitmodules shared/modbus_reg_service
```

URL 为待替换占位符，tag 必须实际存在。消费项目应提交 `.gitmodules` 和 gitlink，以锁定具体提交；克隆后的初始化使用 `git submodule update --init --recursive`。升级需评审并提交新的 gitlink。

在应用顶层 `CMakeLists.txt` 的 `find_package(Zephyr ...)`、`project(...)` 之后添加：

```cmake
add_subdirectory(shared/modbus_reg_service)
```

模块启用后自行创建静态库、公开头文件路径并链接到 `app`，无需重复链接。构建要求 Zephyr 提供 `zephyr_interface`、`zephyr_generated_headers` 和 `app` 目标。

在应用顶层 `Kconfig` 添加（已有 Zephyr 入口时保留一份）：

```kconfig
source "Kconfig.zephyr"
rsource "shared/modbus_reg_service/Kconfig"
```

在应用 `prj.conf` 或产品配置片段中启用：

```conf
CONFIG_MODBUS_REG_SERVICE=y
```

| 配置 | 默认值 | 说明 |
|---|---|---|
| `MODBUS_REG_SERVICE` | n | 编译模块 |
| `MODBUS_REG_SERVICE_NAMES` | y（服务开启时） | 保留诊断名称 |
| `MODBUS_REG_SERVICE_SHELL` | y（依赖满足时） | 依赖 SHELL 和名称功能 |

不需要诊断时可显式关闭后两项。需要 Shell 时启用 `CONFIG_SHELL=y` 并由产品配置控制台后端。模块配置放在应用配置中，产品寄存器表和默认值放在产品代码中，硬件设置放在产品 DTS/overlay 中。

## 字段模型

Coil 和 Discrete Input 的物理单元是 bit。Input Register 和 Holding Register
的物理单元始终是 `uint16_t`，但一个业务字段可以占用多个连续寄存器：

| 字段类型 | 寄存器数量 |
|---|---:|
| `BIT` | 1 bit |
| `U16` | 1 |
| `U32` | 2 |
| `U64` | 4 |
| `F32` | 2 |
| `F64` | 4 |

一次 ID 读写会在一个互斥锁临界区内提交整个字段。一次地址批量读写也只加锁
一次，因此同一次 FC03/FC04 请求不会读到线程更新到一半的数据。主站分成多个
请求读取同一字段时，协议本身无法保证跨请求快照一致。

默认字序为高字在前。例如 `0x12345678` 映射为：

```text
base + 0 = 0x1234
base + 1 = 0x5678
```

## 定义寄存器表

使用 X Macro 让枚举、定义表和可选名称只有一个来源：

```c
#include <modbus_reg_service.h>

#define IO_COIL_REGS(X) \
	X(CO_OUTR0_COMMAND, 0x0000, BIT, false) \
	X(CO_OUTR1_COMMAND, 0x0001, BIT, false)

#define IO_DI_REGS(X) \
	X(DI_DIR0_STATUS,    0x0000, BIT, false) \
	X(DI_OUTR0_STATUS, 0x0020, BIT, false)

#define IO_IR_REGS(X) \
	X(IR_DEVICE_TYPE,  0x000D, U16, 0x0012) \
	X(IR_TOTAL_COUNT,  0x0010, U32, 0)      \
	X(IR_TEMPERATURE,  0x0012, F32, 0.0f)   \
	X(IR_TOTAL_ENERGY, 0x0014, F64, 0.0)     \
	X(IR_UPTIME_US,    0x0018, U64, 0)

#define IO_HR_REGS(X) \
	X(HR_SAMPLE_PERIOD_MS, 0x0000, U16, 10)

enum io_reg_id {
	IO_REG_ID_INVALID = MODBUS_REG_ID_INVALID,
	IO_COIL_REGS(MODBUS_REG_ENUM_ENTRY)
	IO_DI_REGS(MODBUS_REG_ENUM_ENTRY)
	IO_IR_REGS(MODBUS_REG_ENUM_ENTRY)
	IO_HR_REGS(MODBUS_REG_ENUM_ENTRY)
};

MODBUS_REG_BANK_DEFINE(io_coil_bank, IO_COIL_REGS);
MODBUS_REG_BANK_DEFINE(io_di_bank, IO_DI_REGS);
MODBUS_REG_BANK_DEFINE(io_ir_bank, IO_IR_REGS);
MODBUS_REG_BANK_DEFINE(io_hr_bank, IO_HR_REGS);

static const struct modbus_reg_schema io_schema = {
	.banks = {
		[MODBUS_REG_COIL] = io_coil_bank,
		[MODBUS_REG_DISCRETE_INPUT] = io_di_bank,
		[MODBUS_REG_INPUT_REGISTER] = io_ir_bank,
		[MODBUS_REG_HOLDING_REGISTER] = io_hr_bank,
	},
};

static struct modbus_reg_service io_regs;

int io_regs_init(void)
{
	return modbus_reg_service_init(&io_regs, &io_schema);
}
```

模块无自动初始化钩子。应用在线程上下文调用 `io_regs_init()`，检查返回值为 0 后，再启动业务线程、协议服务和 Shell 使用。定义表和运行数组必须在整个服务使用期间有效且不得直接修改；独立实例不能共用运行数组。

示例中的枚举 ID 是业务标识，和 Modbus 地址分离。自动枚举会随条目插入或重排改变数值，不保证跨产品字段表版本稳定，不应直接用作跨固件版本的持久化键或线协议标识。

寄存器列表必须按地址递增排列，同一数据区内的字段地址范围不能重叠。初始化
还会检查 ID、可选名称、字段类型和地址范围。

若某个数据区没有任何字段，在 schema 中使用：

```c
[MODBUS_REG_HOLDING_REGISTER] = MODBUS_REG_EMPTY_BANK,
```

## 程序通过 ID 访问

```c
modbus_reg_set_bit(&io_regs, DI_OUTR0_STATUS, gpio_level);
modbus_reg_set_u32(&io_regs, IR_TOTAL_COUNT, total_count);
modbus_reg_set_u64(&io_regs, IR_UPTIME_US, uptime_us);
modbus_reg_set_f32(&io_regs, IR_TEMPERATURE, temperature);
modbus_reg_set_f64(&io_regs, IR_TOTAL_ENERGY, total_energy);
```

同一数据区需要一次发布多个连续物理单元时，内部线程可使用
`modbus_reg_write_bits()` 或 `modbus_reg_write_words()`。这两个接口允许更新四个
数据区；带 `_external` 后缀的接口专供协议层使用，并额外执行 Modbus 外部写权限检查。

所有函数成功返回 0，失败返回负 errno，调用方应检查结果。可使用 `modbus_reg_get_u32(&io_regs, IR_TOTAL_COUNT, &count)` 读取字段（`count` 为 `uint32_t`）。类型必须与定义一致，否则返回 `-EINVAL`。这些 API 使用 mutex，只允许线程
上下文调用；ISR 应先通过原子变量、队列或事件转交线程。

## Shell 按名称访问

开启：

```conf
CONFIG_MODBUS_REG_SERVICE_NAMES=y
```

宏会把枚举标识直接字符串化，因此 `IR_TEMPERATURE` 的名称就是
`"IR_TEMPERATURE"`，无需重复书写：

```c
modbus_reg_id_t id;

if (modbus_reg_find_id_by_name(&io_regs, "IR_TEMPERATURE", &id) == 0) {
	float value;
	modbus_reg_get_f32(&io_regs, id, &value);
}
```

关闭该配置后，名称字符串和结构体中的名称指针都不会进入固件，名称查找返回
`-ENOTSUP`。业务代码不得依赖名称。

开启 `CONFIG_MODBUS_REG_SERVICE_SHELL` 后，产品在寄存器服务初始化成功后显式
选择 Shell 要查看的实例：

```c
modbus_reg_shell_attach(&io_regs);
```

然后可使用只读诊断命令：

```text
modbus_reg list
modbus_reg get IR_TEMPERATURE
```

Shell 只显示逻辑字段的名称、数据区、起始地址、类型和原始 16 位字，不提供
字符串写入命令。业务线程仍只通过 ID 和带类型 API 修改值。

全局仅选择一个诊断实例，应在启动阶段 attach，避免与命令并发切换。未 attach 时提示 `No register service attached`。例如把 `IR_TOTAL_COUNT` 写为 42 后，get 应显示地址 `0x0010`、类型 U32 和原始字 `0000 002a`。

## Modbus 协议适配

协议层使用地址 API：

```c
modbus_reg_read_bits(&io_regs, MODBUS_REG_DISCRETE_INPUT,
			     start, values, count);
modbus_reg_read_words(&io_regs, MODBUS_REG_INPUT_REGISTER,
			      start, values, count);
modbus_reg_write_words_external(&io_regs, MODBUS_REG_HOLDING_REGISTER,
					start, values, count);
```

外部写权限由数据区决定：仅 Coil 和 Holding Register 可写。设备内部线程可以
用 ID 更新四个数据区。地址 API 遇到未定义的空洞返回 `-ENOENT`，由适配层映射
为 Modbus `Illegal Data Address`。

本模块只维护 RAM 运行值，不读写 Flash，也不包含任何持久化策略。

地址 API 使用零基物理地址，不使用 30001/40001 显示编号；参数是本机 bool/uint16_t 数组，网络字节序和协议功能码数量限制由适配层处理。

## 实现原理与使用边界

```text
产品 X Macro → ID 枚举 / 定义表 / 静态运行数组
                       ↓ 初始化校验并载入默认值
业务 ID API → 实例 mutex + RAM 值 ← 协议地址 API
                       ↑
                  只读 Shell
```

每个逻辑字段用一个 `uint64_t raw` 保存。ID/名称查找遍历字段，地址查找在已排序 bank 中二分搜索；不按最高地址分配稠密存储。需要低字优先时可手工定义字段，见 [API](docs/API.md)。

批量访问先检查完整区间，再在单个锁内执行，遇到空洞不会提交前半段写入。地址写允许仅更新多字字段的一部分，其他字保留；需要完整字段提交时使用 ID API 或一次覆盖全字段的批量写。

多个 API 调用、多个协议请求和不同实例之间没有共同事务；get 后 set 不是原子计数操作。`modbus_reg_service_reset()` 在一个锁内恢复全部字段默认值，不改变 schema，也不写 Flash。重启后需重新初始化，产品负责参数加载和保存。

| 现象 | 排查方向 |
|---|---|
| 找不到头文件或配置符号 | CMake/Kconfig 是否接入，服务是否开启 |
| init 返回 EINVAL/EEXIST | 字段范围、排序、类型、空 bank、ID/名称唯一性 |
| 地址访问返回 ENOENT | 零基地址是否正确，请求是否跨空洞 |
| 有类型访问返回 EINVAL | 初始化、指针、字段类型是否正确 |
| 名称查找返回 ENOTSUP | 是否关闭名称配置 |
| 外部写返回 EACCES | 是否写入了 DI/IR 或错误的数据区 |
