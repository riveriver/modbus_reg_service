# API 参考

公开头文件：`#include <modbus_reg_service.h>`。本文面向计划中的 1.0.0；接入步骤见 [README](../README.md)。

## 通用约定

所有函数成功返回 0，失败返回负 errno。仅在成功时使用输出参数；参数同时违反多个条件时，不依赖错误码检查顺序。

运行值读写和 reset 使用实例 mutex，等待方式为 `K_FOREVER`，必须在线程上下文调用。有效调用进入锁函数后若发现 ISR，返回 `-EWOULDBLOCK`。初始化和 Shell attach 应在启动阶段串行完成，不能与查询、读写或命令执行并发。

元数据查询不加锁，依赖字段定义在初始化后不变。公开结构体中的 banks、lock、initialized 和运行数组由模块管理，调用方不得直接修改。初始化后的实例不能通过结构体复制产生新实例。

## 数据模型与宏

| 类型 | 含义 |
|---|---|
| `modbus_reg_id_t` | uint16_t，0 为 `MODBUS_REG_ID_INVALID`，有效 ID 为 1～65535，实例内全局唯一 |
| `enum modbus_reg_space` | `MODBUS_REG_COIL`、`MODBUS_REG_DISCRETE_INPUT`、`MODBUS_REG_INPUT_REGISTER`、`MODBUS_REG_HOLDING_REGISTER` |
| `enum modbus_reg_type` | `MODBUS_REG_TYPE_BIT/U16/U32/U64/F32/F64`（分别为完整枚举名） |
| `struct modbus_reg_def` | ID、起始地址、类型、字序、默认值、可选名称 |
| `struct modbus_reg_runtime` | 每个逻辑字段一个 uint64_t raw |
| `struct modbus_reg_bank_config` | definitions、runtime、count，count 是逻辑字段数 |
| `struct modbus_reg_schema` | 四个数据区的 bank 配置 |
| `struct modbus_reg_service` | bank 配置、mutex 和初始化状态 |
| `struct modbus_reg_info` | ID、space、address、type、register_count、word_order、可选 name |

BIT 只允许用于 Coil/DI，宽度为一个 bit；其余类型只用于 IR/HR，U16/U32/U64/F32/F64 宽度依次为 1/2/4/2/4 个 16 位寄存器。info 中 BIT 的 register_count 为 1。地址为零基 0x0000～0xFFFF，字段不能越界。

init 浅复制 bank 配置，不复制数组；schema 对象本身只在初始化时读取。定义表、可选名称字符串和运行数组必须保持有效。定义表只读，运行数组只能通过所属实例访问；不同独立实例不能共享运行数组，否则独立 mutex 无法保护共享数据。

| 宏 | 契约 |
|---|---|
| `MODBUS_REG_ENUM_ENTRY` | 展开四参数 X Macro 列表生成枚举 |
| `MODBUS_REG_DEF_ENTRY` | 展开 `(ID, address, type, default)`，默认 MSW_FIRST |
| `MODBUS_REG_DEF_ENTRY_ORDERED` | 展开 `(ID, address, type, default, order)` |
| `MODBUS_REG_BANK_DEFINE(name, list)` | 创建静态定义、运行数组和 bank，列表至少一项 |
| `MODBUS_REG_EMPTY_BANK` | 空 bank，两个指针 NULL、count 为 0 |

type 标记使用 BIT/U16/U32/U64/F32/F64，order 标记使用 MSW_FIRST/LSW_FIRST。低字优先的手工 bank 示例：

```c
enum { HR_LIMIT = 100 };
#define ORDERED_REGS(X) X(HR_LIMIT, 0x0010, U32, 0U, LSW_FIRST)
static const struct modbus_reg_def ordered_defs[] = {
    ORDERED_REGS(MODBUS_REG_DEF_ENTRY_ORDERED)
};
static struct modbus_reg_runtime ordered_runtime[ARRAY_SIZE(ordered_defs)];
static const struct modbus_reg_bank_config ordered_bank = {
    .definitions = ordered_defs,
    .runtime = ordered_runtime,
    .count = ARRAY_SIZE(ordered_defs),
};
```

将 bank 放入 schema 的 HR 项。五参数列表不能直接传给四参数的 BANK_DEFINE/ENUM_ENTRY 宏。

U32 的 0x12345678 在 MSW_FIRST 下映射为 1234 5678，LSW_FIRST 下为 5678 1234。API 使用本机 uint16_t 数组，字节流编解码由协议层负责。浮点数通过 memcpy 保存本机位表示，不转换编码，也不校验 NaN/Inf 或业务范围。

自动枚举 ID 随产品表调整可能变化；需要跨版本稳定数值时由产品显式分配并维护兼容约定。名称配置影响公开结构体布局，所有消费代码必须使用一致配置重新编译，不承诺跨配置二进制 ABI。

## 初始化和重置

```c
int modbus_reg_service_init(struct modbus_reg_service *service,
                            const struct modbus_reg_schema *schema);
int modbus_reg_service_reset(struct modbus_reg_service *service);
```

init 校验全部 schema，初始化 mutex，复制 bank 配置并载入默认值，最后设置 initialized。四个 bank 可以全为空。校验条件：非空 bank 两指针有效；空 bank 两指针均 NULL；字段类型匹配数据区，ID 非零，字序有效，范围不越界；每个 bank 地址递增且无重叠；ID 全局唯一；启用名称时字符串非空且全局唯一。

无效参数/定义返回 `-EINVAL`，重复 ID/名称返回 `-EEXIST`。校验失败不建立新实例，调用方必须检查返回值，不能依赖旧状态。init 不保护并发调用，运行中不要再次 init 重建 mutex，应使用 reset。

reset 在单个锁内恢复全部字段默认值，不改变定义、不写 Flash、不触发业务回调。无效实例返回 `-EINVAL`，ISR 限制见通用约定。

## 元数据和名称

```c
int modbus_reg_get_info(const struct modbus_reg_service *service,
                        modbus_reg_id_t id, struct modbus_reg_info *info);
int modbus_reg_find_id_by_name(const struct modbus_reg_service *service,
                              const char *name, modbus_reg_id_t *id);
```

get_info 不读取运行值。启用名称时 info->name 引用原字符串。无效实例、ID 0 或 NULL 输出返回 `-EINVAL`；未找到非零 ID 返回 `-ENOENT`。

名称查找区分大小写且完整匹配。默认宏直接把 ID 标识字符串化。无效实例或 NULL 参数返回 `-EINVAL`；未找到返回 `-ENOENT`；名称关闭时有效调用返回 `-ENOTSUP`。关闭名称也会移除定义和查询结构体的 name 成员。

## 有类型 ID 访问

每种类型提供 get/set 一对函数，以 U32 为例：

```c
int modbus_reg_get_u32(struct modbus_reg_service *service,
                       modbus_reg_id_t id, uint32_t *value);
int modbus_reg_set_u32(struct modbus_reg_service *service,
                       modbus_reg_id_t id, uint32_t value);
```

| 函数后缀 | C 类型 | 必须匹配的字段类型 |
|---|---|---|
| bit | bool | BIT |
| u16 | uint16_t | U16 |
| u32 | uint32_t | U32 |
| u64 | uint64_t | U64 |
| f32 | float | F32 |
| f64 | double | F64 |

每行均有 `modbus_reg_get_<后缀>` 和 `modbus_reg_set_<后缀>`；getter 接收值指针，setter 接收值。NULL 输出、无效实例、ID 0、类型不匹配返回 `-EINVAL`；未找到非零 ID 返回 `-ENOENT`。不进行隐式类型转换。

setter 可更新四个数据区，不执行外部写权限或业务范围校验。一次调用完整访问一个字段；多个调用之间可被其他线程插入，get 后 set 不是原子加法。

## 地址批量访问

六个接口的完整名称和参数如下：

```c
int modbus_reg_read_bits(struct modbus_reg_service *service,
    enum modbus_reg_space space, uint16_t start_address, bool *values, size_t count);
int modbus_reg_write_bits(struct modbus_reg_service *service,
    enum modbus_reg_space space, uint16_t start_address, const bool *values, size_t count);
int modbus_reg_write_bits_external(struct modbus_reg_service *service,
    enum modbus_reg_space space, uint16_t start_address, const bool *values, size_t count);
int modbus_reg_read_words(struct modbus_reg_service *service,
    enum modbus_reg_space space, uint16_t start_address, uint16_t *values, size_t count);
int modbus_reg_write_words(struct modbus_reg_service *service,
    enum modbus_reg_space space, uint16_t start_address, const uint16_t *values, size_t count);
int modbus_reg_write_words_external(struct modbus_reg_service *service,
    enum modbus_reg_space space, uint16_t start_address, const uint16_t *values, size_t count);
```

bits 接口仅接受 Coil/DI，数组每项一个 bool，不是打包位图；words 接口仅接受 IR/HR，每项一个 uint16_t。无 external 后缀的写接口供内部线程使用。协议层必须使用 external：bits 仅可写 Coil，words 仅可写 HR。

count 表示物理单元数；调用方保证数组容量足够且调用期间不被并发修改，不与内部存储重叠。区间 `[start_address, start_address + count)` 不得越过 65536，count 也不得大于 65536。地址必须全部存在，可跨相邻字段，不能跨空洞。

先检查完整区间，随后单次加锁执行；范围校验失败不会部分写入或先填充部分输出。允许访问多字字段的中间部分，部分写保留其余字，不做业务值校验。一次请求内快照/提交一致，多个请求、多个实例没有共同事务。

count 为 0 时允许 values 为 NULL，仍检查数据区、实例并经过锁；外部写仍检查权限。协议层仍需按功能码规则拒绝非法数量并限制报文长度，本模块不实施协议请求上限。

| 返回值 | 含义 |
|---|---|
| `-EINVAL` | 无效实例、错误数据区类型、非零 count 对应 NULL、数量或范围越界 |
| `-ENOENT` | 至少一个地址未定义 |
| `-EACCES` | external 数据区不是其唯一允许写入的数据区，该检查在内部访问前执行 |
| `-EWOULDBLOCK` | 进入锁函数时发现 ISR |

适配层负责 errno 到协议异常的映射，例如未定义地址映射为非法数据地址。本模块不生成异常报文。

## Shell

仅在 `CONFIG_MODBUS_REG_SERVICE_SHELL=y` 时声明和编译：

```c
int modbus_reg_shell_attach(struct modbus_reg_service *service);
```

选择全局唯一诊断实例。NULL/未初始化返回 `-EINVAL`，成功返回 0。再次 attach 替换原指针，无 detach 接口，切换不加锁，应在启动阶段完成。实例和底层存储必须持续有效。

命令为 `modbus_reg list`、`modbus_reg get <name>`；BIT 显示 0/1，其余显示按字段字序排列的原始字，无写命令。未 attach 时命令返回 `-ENODEV`。list 逐字段读取，不提供全表共同快照。
