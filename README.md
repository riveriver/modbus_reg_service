# modbus_reg_service

`modbus_reg_service` 是一个无持久化、静态分配、线程安全的稀疏 Modbus
寄存器服务。产品代码通过稳定 ID 读写业务字段；Modbus 协议适配层才通过
“数据区 + 地址”批量访问；名称只用于 Shell 和诊断。

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

类型必须与定义一致，否则返回 `-EINVAL`。这些 API 使用 mutex，只允许线程
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
