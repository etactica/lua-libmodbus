package = "lua-libmodbus"
version = "0.3-1"
source = {
	url = "git://github.com/remakeelectric/lua-libmodbus",
	tag = "v0.3"
}
description = {
	summary = "Lua bindings to libmodbus",
	detailed = [[
		Lua bindings to the libmodbus library.
		The parameters to all functions are as per libmodbus's api
		only with sensible defaults for optional values, and return
		values directly rather than via pointers.
	]],
	homepage = "https://github.com/remakeelectric/lua-libmodbus",
	license = "MIT"
}
dependencies = {
	"lua >= 5.1"
}
external_dependencies = {
	LIBMODBUS = {
		header = "modbus/modbus.h"
	}
}
build = {
	type = "builtin",
	modules = {

		libmodbus = {
			sources = { "lua-libmodbus.c" },
			defines = {},
			libraries = { "modbus" },
			incdirs = { "$LIBMODBUS_INCDIR" },
			libdirs = { "$LIBMODBUS_LIBDIR" },
		}
	}
}

