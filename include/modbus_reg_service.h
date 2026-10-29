#ifndef MODBUS_REG_SERVICE_H_
#define MODBUS_REG_SERVICE_H_

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t modbus_reg_id_t;

#define MODBUS_REG_ID_INVALID ((modbus_reg_id_t)0U)

enum modbus_reg_space {
	MODBUS_REG_COIL,
	MODBUS_REG_DISCRETE_INPUT,
	MODBUS_REG_INPUT_REGISTER,
	MODBUS_REG_HOLDING_REGISTER,
	MODBUS_REG_SPACE_COUNT,
};

/* BIT is used by Coil/Discrete Input. The other types occupy one or more
 * consecutive 16-bit Input/Holding Registers.
 */
enum modbus_reg_type {
	MODBUS_REG_TYPE_BIT,
	MODBUS_REG_TYPE_U16,
	MODBUS_REG_TYPE_U32,
	MODBUS_REG_TYPE_U64,
	MODBUS_REG_TYPE_F32,
	MODBUS_REG_TYPE_F64,
};

/* Modbus fixes the byte order inside each 16-bit register. It does not define
 * the order of multiple registers, so the field definition makes that choice
 * explicit. The compact definition macro defaults to MSW first.
 */
enum modbus_reg_word_order {
	MODBUS_REG_WORD_ORDER_MSW_FIRST,
	MODBUS_REG_WORD_ORDER_LSW_FIRST,
};

union modbus_reg_default_value {
	bool bit;
	uint16_t u16;
	uint32_t u32;
	uint64_t u64;
	float f32;
	double f64;
};

struct modbus_reg_def {
	modbus_reg_id_t id;
	uint16_t address;
	enum modbus_reg_type type;
	enum modbus_reg_word_order word_order;
	union modbus_reg_default_value default_value;
#if defined(CONFIG_MODBUS_REG_SERVICE_NAMES)
	const char *name;
#endif
};

struct modbus_reg_runtime {
	uint64_t raw;
};

struct modbus_reg_bank_config {
	const struct modbus_reg_def *definitions;
	struct modbus_reg_runtime *runtime;
	size_t count;
};

struct modbus_reg_schema {
	struct modbus_reg_bank_config banks[MODBUS_REG_SPACE_COUNT];
};

struct modbus_reg_service {
	struct modbus_reg_bank_config banks[MODBUS_REG_SPACE_COUNT];
	struct k_mutex lock;
	bool initialized;
};

struct modbus_reg_info {
	modbus_reg_id_t id;
	enum modbus_reg_space space;
	uint16_t address;
	enum modbus_reg_type type;
	uint8_t register_count;
	enum modbus_reg_word_order word_order;
#if defined(CONFIG_MODBUS_REG_SERVICE_NAMES)
	const char *name;
#endif
};

/* X-list helpers. A product table has the form:
 *
 * #define IO_IR_REGS(X) \
 *     X(IR_DEVICE_TYPE, 0x000D, U16, 0x0012) \
 *     X(IR_TOTAL_COUNT, 0x0010, U32, 0)      \
 *     X(IR_TEMPERATURE, 0x0012, F32, 0.0f)
 *
 * enum io_reg_id {
 *     IO_REG_ID_INVALID = MODBUS_REG_ID_INVALID,
 *     IO_IR_REGS(MODBUS_REG_ENUM_ENTRY)
 * };
 *
 * The same enum token is stringified as the optional diagnostic name.
 */
#define MODBUS_REG_ENUM_ENTRY(reg_id_, address_, type_, default_) reg_id_,

#define MODBUS_REG_SERVICE_CAT_(a_, b_)       a_##b_
#define MODBUS_REG_SERVICE_CAT(a_, b_)        MODBUS_REG_SERVICE_CAT_(a_, b_)
#define MODBUS_REG_SERVICE_STRINGIFY_(value_) #value_
#define MODBUS_REG_SERVICE_STRINGIFY(value_)  MODBUS_REG_SERVICE_STRINGIFY_(value_)

#define MODBUS_REG_SERVICE_DEFAULT_BIT(value_) .default_value = {.bit = (value_)},
#define MODBUS_REG_SERVICE_DEFAULT_U16(value_) .default_value = {.u16 = (uint16_t)(value_)},
#define MODBUS_REG_SERVICE_DEFAULT_U32(value_) .default_value = {.u32 = (uint32_t)(value_)},
#define MODBUS_REG_SERVICE_DEFAULT_U64(value_) .default_value = {.u64 = (uint64_t)(value_)},
#define MODBUS_REG_SERVICE_DEFAULT_F32(value_) .default_value = {.f32 = (float)(value_)},
#define MODBUS_REG_SERVICE_DEFAULT_F64(value_) .default_value = {.f64 = (double)(value_)},

#if defined(CONFIG_MODBUS_REG_SERVICE_NAMES)
#define MODBUS_REG_SERVICE_NAME_INIT(reg_id_) .name = MODBUS_REG_SERVICE_STRINGIFY(reg_id_),
#else
#define MODBUS_REG_SERVICE_NAME_INIT(reg_id_)
#endif

#define MODBUS_REG_DEF_ENTRY(reg_id_, address_, type_, default_)                                   \
	{.id = (reg_id_),                                                                          \
	 .address = (uint16_t)(address_),                                                          \
	 .type = MODBUS_REG_TYPE_##type_,                                                          \
	 .word_order = MODBUS_REG_WORD_ORDER_MSW_FIRST,                                            \
	 MODBUS_REG_SERVICE_CAT(MODBUS_REG_SERVICE_DEFAULT_, type_)(default_)                      \
		 MODBUS_REG_SERVICE_NAME_INIT(reg_id_)},

#define MODBUS_REG_DEF_ENTRY_ORDERED(reg_id_, address_, type_, default_, order_)                   \
	{.id = (reg_id_),                                                                          \
	 .address = (uint16_t)(address_),                                                          \
	 .type = MODBUS_REG_TYPE_##type_,                                                          \
	 .word_order = MODBUS_REG_WORD_ORDER_##order_,                                             \
	 MODBUS_REG_SERVICE_CAT(MODBUS_REG_SERVICE_DEFAULT_, type_)(default_)                      \
		 MODBUS_REG_SERVICE_NAME_INIT(reg_id_)},

/* The list must contain at least one entry. Use MODBUS_REG_EMPTY_BANK for an
 * unused data space.
 */
#define MODBUS_REG_BANK_DEFINE(bank_name_, register_list_)                                         \
	static const struct modbus_reg_def bank_name_##_definitions[] = {                          \
		register_list_(MODBUS_REG_DEF_ENTRY)};                                             \
	static struct modbus_reg_runtime                                                           \
		bank_name_##_runtime[ARRAY_SIZE(bank_name_##_definitions)];                        \
	static const struct modbus_reg_bank_config bank_name_ = {                                  \
		.definitions = bank_name_##_definitions,                                           \
		.runtime = bank_name_##_runtime,                                                   \
		.count = ARRAY_SIZE(bank_name_##_definitions),                                     \
	}

#define MODBUS_REG_EMPTY_BANK ((struct modbus_reg_bank_config){0})

int modbus_reg_service_init(struct modbus_reg_service *service,
			    const struct modbus_reg_schema *schema);
int modbus_reg_service_reset(struct modbus_reg_service *service);

int modbus_reg_get_info(const struct modbus_reg_service *service, modbus_reg_id_t id,
			struct modbus_reg_info *info);
int modbus_reg_find_id_by_name(const struct modbus_reg_service *service, const char *name,
			       modbus_reg_id_t *id);

int modbus_reg_get_bit(struct modbus_reg_service *service, modbus_reg_id_t id, bool *value);
int modbus_reg_set_bit(struct modbus_reg_service *service, modbus_reg_id_t id, bool value);
int modbus_reg_get_u16(struct modbus_reg_service *service, modbus_reg_id_t id, uint16_t *value);
int modbus_reg_set_u16(struct modbus_reg_service *service, modbus_reg_id_t id, uint16_t value);
int modbus_reg_get_u32(struct modbus_reg_service *service, modbus_reg_id_t id, uint32_t *value);
int modbus_reg_set_u32(struct modbus_reg_service *service, modbus_reg_id_t id, uint32_t value);
int modbus_reg_get_u64(struct modbus_reg_service *service, modbus_reg_id_t id, uint64_t *value);
int modbus_reg_set_u64(struct modbus_reg_service *service, modbus_reg_id_t id, uint64_t value);
int modbus_reg_get_f32(struct modbus_reg_service *service, modbus_reg_id_t id, float *value);
int modbus_reg_set_f32(struct modbus_reg_service *service, modbus_reg_id_t id, float value);
int modbus_reg_get_f64(struct modbus_reg_service *service, modbus_reg_id_t id, double *value);
int modbus_reg_set_f64(struct modbus_reg_service *service, modbus_reg_id_t id, double value);

/* Address-based APIs are intended for the Modbus protocol adapter only.
 * Each bulk operation takes one mutex and therefore returns/commits one
 * coherent snapshot. External writes follow Modbus permissions: Coil and
 * Holding Register are writable; Discrete Input and Input Register are not.
 */
int modbus_reg_read_bits(struct modbus_reg_service *service, enum modbus_reg_space space,
			 uint16_t start_address, bool *values, size_t count);
int modbus_reg_write_bits(struct modbus_reg_service *service, enum modbus_reg_space space,
			  uint16_t start_address, const bool *values, size_t count);
int modbus_reg_write_bits_external(struct modbus_reg_service *service, enum modbus_reg_space space,
				   uint16_t start_address, const bool *values, size_t count);
int modbus_reg_read_words(struct modbus_reg_service *service, enum modbus_reg_space space,
			  uint16_t start_address, uint16_t *values, size_t count);
int modbus_reg_write_words(struct modbus_reg_service *service, enum modbus_reg_space space,
			   uint16_t start_address, const uint16_t *values, size_t count);
int modbus_reg_write_words_external(struct modbus_reg_service *service, enum modbus_reg_space space,
				    uint16_t start_address, const uint16_t *values, size_t count);

#if defined(CONFIG_MODBUS_REG_SERVICE_SHELL)
/* Select the single service instance exposed by the diagnostic shell. Call
 * this after modbus_reg_service_init() and before using modbus-reg commands.
 */
int modbus_reg_shell_attach(struct modbus_reg_service *service);
#endif

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_REG_SERVICE_H_ */
