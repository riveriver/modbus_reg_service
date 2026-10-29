#include <zephyr/ztest.h>

#include <modbus_reg_service.h>

#include <errno.h>

#define TEST_CO_REGS(X) X(CO_OUTPUT, 0x0000, BIT, false)

#define TEST_DI_REGS(X) X(DI_STATUS, 0x0020, BIT, true)

#define TEST_IR_REGS(X)                                                                            \
	X(IR_COUNT, 0x1000, U32, 0x12345678U)                                                      \
	X(IR_VALUE, 0x1002, F32, 1.5f)                                                             \
	X(IR_TOTAL, 0x1004, F64, 2.5)                                                              \
	X(IR_UPTIME, 0x1008, U64, 0x0123456789ABCDEFULL)

#define TEST_HR_REGS(X)                                                                            \
	X(HR_PERIOD, 0x0000, U16, 10U)                                                             \
	X(HR_LIMIT, 0x0010, U32, 0U)

enum test_reg_id {
	TEST_REG_ID_INVALID = MODBUS_REG_ID_INVALID,
	TEST_CO_REGS(MODBUS_REG_ENUM_ENTRY)
	TEST_DI_REGS(MODBUS_REG_ENUM_ENTRY) TEST_IR_REGS(MODBUS_REG_ENUM_ENTRY)
		TEST_HR_REGS(MODBUS_REG_ENUM_ENTRY)
};

MODBUS_REG_BANK_DEFINE(test_co_bank, TEST_CO_REGS);
MODBUS_REG_BANK_DEFINE(test_di_bank, TEST_DI_REGS);
MODBUS_REG_BANK_DEFINE(test_ir_bank, TEST_IR_REGS);
MODBUS_REG_BANK_DEFINE(test_hr_bank, TEST_HR_REGS);

static const struct modbus_reg_schema test_schema = {
	.banks =
		{
			[MODBUS_REG_COIL] = test_co_bank,
			[MODBUS_REG_DISCRETE_INPUT] = test_di_bank,
			[MODBUS_REG_INPUT_REGISTER] = test_ir_bank,
			[MODBUS_REG_HOLDING_REGISTER] = test_hr_bank,
		},
};

static struct modbus_reg_service service;

static void before_each(void *fixture)
{
	ARG_UNUSED(fixture);
	zassert_ok(modbus_reg_service_init(&service, &test_schema));
}

ZTEST(modbus_reg_service, test_names_are_generated_from_ids)
{
	modbus_reg_id_t id;

#if defined(CONFIG_MODBUS_REG_SERVICE_NAMES)
	zassert_ok(modbus_reg_find_id_by_name(&service, "IR_COUNT", &id));
	zassert_equal(id, IR_COUNT);
#else
	zassert_equal(modbus_reg_find_id_by_name(&service, "IR_COUNT", &id), -ENOTSUP);
#endif
}

ZTEST(modbus_reg_service, test_u32_is_one_atomic_logical_field)
{
	uint16_t words[2];
	uint32_t value;

	zassert_ok(modbus_reg_read_words(&service, MODBUS_REG_INPUT_REGISTER, 0x1000, words,
					 ARRAY_SIZE(words)));
	zassert_equal(words[0], 0x1234);
	zassert_equal(words[1], 0x5678);

	zassert_ok(modbus_reg_set_u32(&service, IR_COUNT, 0xAABBCCDDU));
	zassert_ok(modbus_reg_get_u32(&service, IR_COUNT, &value));
	zassert_equal(value, 0xAABBCCDDU);
}

ZTEST(modbus_reg_service, test_float_and_double_round_trip)
{
	float f32;
	double f64;

	zassert_ok(modbus_reg_set_f32(&service, IR_VALUE, -12.25f));
	zassert_ok(modbus_reg_get_f32(&service, IR_VALUE, &f32));
	zassert_equal(f32, -12.25f);

	zassert_ok(modbus_reg_set_f64(&service, IR_TOTAL, 1234.5));
	zassert_ok(modbus_reg_get_f64(&service, IR_TOTAL, &f64));
	zassert_equal(f64, 1234.5);
}

ZTEST(modbus_reg_service, test_u64_round_trip_and_word_order)
{
	uint16_t words[4];
	uint64_t value;

	zassert_ok(modbus_reg_read_words(&service, MODBUS_REG_INPUT_REGISTER, 0x1008, words,
					 ARRAY_SIZE(words)));
	zassert_equal(words[0], 0x0123);
	zassert_equal(words[1], 0x4567);
	zassert_equal(words[2], 0x89AB);
	zassert_equal(words[3], 0xCDEF);

	zassert_ok(modbus_reg_set_u64(&service, IR_UPTIME, 0xFEDCBA9876543210ULL));
	zassert_ok(modbus_reg_get_u64(&service, IR_UPTIME, &value));
	zassert_equal(value, 0xFEDCBA9876543210ULL);
}

#if defined(CONFIG_MODBUS_REG_SERVICE_SHELL)
ZTEST(modbus_reg_service, test_shell_can_attach_initialized_service)
{
	zassert_ok(modbus_reg_shell_attach(&service));
}
#endif

ZTEST(modbus_reg_service, test_schema_rejects_overlapping_fields)
{
	static const struct modbus_reg_def definitions[] = {
		{
			.id = 100U,
			.address = 0x0010,
			.type = MODBUS_REG_TYPE_U32,
			.word_order = MODBUS_REG_WORD_ORDER_MSW_FIRST,
			.default_value.u32 = 0U,
#if defined(CONFIG_MODBUS_REG_SERVICE_NAMES)
			.name = "IR_FIRST",
#endif
		},
		{
			.id = 101U,
			.address = 0x0011,
			.type = MODBUS_REG_TYPE_U16,
			.word_order = MODBUS_REG_WORD_ORDER_MSW_FIRST,
			.default_value.u16 = 0U,
#if defined(CONFIG_MODBUS_REG_SERVICE_NAMES)
			.name = "IR_OVERLAP",
#endif
		},
	};
	static struct modbus_reg_runtime runtime[ARRAY_SIZE(definitions)];
	static const struct modbus_reg_schema invalid_schema = {
		.banks =
			{
				[MODBUS_REG_INPUT_REGISTER] =
					{
						.definitions = definitions,
						.runtime = runtime,
						.count = ARRAY_SIZE(definitions),
					},
			},
	};
	struct modbus_reg_service invalid_service;

	zassert_equal(modbus_reg_service_init(&invalid_service, &invalid_schema), -EINVAL);
}

ZTEST(modbus_reg_service, test_typed_api_rejects_wrong_type)
{
	uint16_t value;

	zassert_equal(modbus_reg_get_u16(&service, IR_COUNT, &value), -EINVAL);
}

ZTEST(modbus_reg_service, test_reset_restores_defaults)
{
	uint32_t value;

	zassert_ok(modbus_reg_set_u32(&service, IR_COUNT, 1U));
	zassert_ok(modbus_reg_service_reset(&service));
	zassert_ok(modbus_reg_get_u32(&service, IR_COUNT, &value));
	zassert_equal(value, 0x12345678U);
}

ZTEST(modbus_reg_service, test_external_permissions_and_sparse_hole)
{
	uint16_t value = 25U;
	uint16_t readback;
	uint16_t words[] = {0xAABBU, 0xCCDDU};
	uint32_t wide_readback;
	bool bit = true;
	bool bit_readback;

	zassert_equal(modbus_reg_write_words_external(&service, MODBUS_REG_INPUT_REGISTER, 0x1000,
						      &value, 1U),
		      -EACCES);
	zassert_ok(modbus_reg_write_words_external(&service, MODBUS_REG_HOLDING_REGISTER, 0x0000,
						   &value, 1U));
	zassert_ok(modbus_reg_get_u16(&service, HR_PERIOD, &readback));
	zassert_equal(readback, value);
	zassert_ok(modbus_reg_write_words_external(&service, MODBUS_REG_HOLDING_REGISTER, 0x0010,
						   words, ARRAY_SIZE(words)));
	zassert_ok(modbus_reg_get_u32(&service, HR_LIMIT, &wide_readback));
	zassert_equal(wide_readback, 0xAABBCCDDU);
	zassert_ok(modbus_reg_write_bits_external(&service, MODBUS_REG_COIL, 0x0000, &bit, 1U));
	zassert_ok(modbus_reg_get_bit(&service, CO_OUTPUT, &bit_readback));
	zassert_true(bit_readback);
	zassert_equal(modbus_reg_write_bits_external(&service, MODBUS_REG_DISCRETE_INPUT, 0x0020,
						     &bit, 1U),
		      -EACCES);
	zassert_equal(
		modbus_reg_read_words(&service, MODBUS_REG_INPUT_REGISTER, 0x0000, &readback, 1U),
		-ENOENT);
}

ZTEST(modbus_reg_service, test_internal_bulk_write_can_publish_read_only_spaces)
{
	bool discrete = false;
	uint16_t words[] = {0x1357U, 0x2468U};
	uint32_t input;

	zassert_ok(
		modbus_reg_write_bits(&service, MODBUS_REG_DISCRETE_INPUT, 0x0020, &discrete, 1U));
	zassert_ok(modbus_reg_get_bit(&service, DI_STATUS, &discrete));
	zassert_false(discrete);

	zassert_ok(modbus_reg_write_words(&service, MODBUS_REG_INPUT_REGISTER, 0x1000, words,
					  ARRAY_SIZE(words)));
	zassert_ok(modbus_reg_get_u32(&service, IR_COUNT, &input));
	zassert_equal(input, 0x13572468U);
}

ZTEST_SUITE(modbus_reg_service, NULL, NULL, before_each, NULL, NULL);
