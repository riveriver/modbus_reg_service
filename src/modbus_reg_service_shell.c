#include "modbus_reg_service.h"

#include <zephyr/shell/shell.h>

#include <errno.h>

static struct modbus_reg_service *diagnostic_service;

static const char *space_name(enum modbus_reg_space space)
{
	switch (space) {
	case MODBUS_REG_COIL:
		return "CO";
	case MODBUS_REG_DISCRETE_INPUT:
		return "DI";
	case MODBUS_REG_INPUT_REGISTER:
		return "IR";
	case MODBUS_REG_HOLDING_REGISTER:
		return "HR";
	default:
		return "?";
	}
}

static const char *type_name(enum modbus_reg_type type)
{
	switch (type) {
	case MODBUS_REG_TYPE_BIT:
		return "BIT";
	case MODBUS_REG_TYPE_U16:
		return "U16";
	case MODBUS_REG_TYPE_U32:
		return "U32";
	case MODBUS_REG_TYPE_U64:
		return "U64";
	case MODBUS_REG_TYPE_F32:
		return "F32";
	case MODBUS_REG_TYPE_F64:
		return "F64";
	default:
		return "?";
	}
}

int modbus_reg_shell_attach(struct modbus_reg_service *service)
{
	if (service == NULL || !service->initialized) {
		return -EINVAL;
	}

	diagnostic_service = service;
	return 0;
}

static int require_service(const struct shell *shell)
{
	if (diagnostic_service != NULL && diagnostic_service->initialized) {
		return 0;
	}

	shell_error(shell, "No register service attached");
	return -ENODEV;
}

static int print_register(const struct shell *shell, const struct modbus_reg_info *info)
{
	bool bit;
	uint16_t words[4];
	int error;

	if (info->type == MODBUS_REG_TYPE_BIT) {
		error = modbus_reg_read_bits(diagnostic_service, info->space, info->address, &bit,
					     1U);
		if (error == 0) {
			shell_print(shell, "%s %-24s 0x%04x BIT = %u", space_name(info->space),
				    info->name, info->address, bit ? 1U : 0U);
		}
		return error;
	}

	error = modbus_reg_read_words(diagnostic_service, info->space, info->address, words,
				      info->register_count);
	if (error != 0) {
		return error;
	}

	shell_fprintf(shell, SHELL_NORMAL, "%s %-24s 0x%04x %-3s =", space_name(info->space),
		      info->name, info->address, type_name(info->type));
	for (uint8_t index = 0U; index < info->register_count; index++) {
		shell_fprintf(shell, SHELL_NORMAL, " %04x", words[index]);
	}
	shell_fprintf(shell, SHELL_NORMAL, "\n");
	return 0;
}

static int command_list(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int error = require_service(shell);

	if (error != 0) {
		return error;
	}

	for (enum modbus_reg_space space = MODBUS_REG_COIL; space < MODBUS_REG_SPACE_COUNT;
	     space++) {
		const struct modbus_reg_bank_config *bank = &diagnostic_service->banks[space];

		for (size_t index = 0U; index < bank->count; index++) {
			struct modbus_reg_info info;

			error = modbus_reg_get_info(diagnostic_service, bank->definitions[index].id,
						    &info);
			if (error == 0) {
				error = print_register(shell, &info);
			}
			if (error != 0) {
				shell_error(shell, "Read failed: %d", error);
				return error;
			}
		}
	}

	return 0;
}

static int command_get(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	int error = require_service(shell);
	modbus_reg_id_t id;
	struct modbus_reg_info info;

	if (error != 0) {
		return error;
	}

	error = modbus_reg_find_id_by_name(diagnostic_service, argv[1], &id);
	if (error != 0) {
		shell_error(shell, "Unknown register: %s", argv[1]);
		return error;
	}

	error = modbus_reg_get_info(diagnostic_service, id, &info);
	if (error == 0) {
		error = print_register(shell, &info);
	}
	if (error != 0) {
		shell_error(shell, "Read failed: %d", error);
	}

	return error;
}

SHELL_STATIC_SUBCMD_SET_CREATE(modbus_reg_commands,
			       SHELL_CMD_ARG(list, NULL, "List all defined registers", command_list,
					     1, 0),
			       SHELL_CMD_ARG(get, NULL, "Read one register by name", command_get, 2,
					     0),
			       SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(modbus_reg, &modbus_reg_commands, "Modbus register diagnostics", NULL);
