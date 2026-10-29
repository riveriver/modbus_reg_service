if(CONFIG_MODBUS_REG_SERVICE)
	add_library(modbus_reg_service STATIC src/modbus_reg_service.c)
	target_sources_ifdef(CONFIG_MODBUS_REG_SERVICE_SHELL
		modbus_reg_service PRIVATE src/modbus_reg_service_shell.c)
	target_include_directories(modbus_reg_service PUBLIC include)
	target_link_libraries(modbus_reg_service PUBLIC zephyr_interface)
	add_dependencies(modbus_reg_service zephyr_generated_headers)
	target_link_libraries(app PRIVATE modbus_reg_service)
endif()
