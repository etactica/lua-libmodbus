package = "lua-libmodbus"
version = "0.6-1"
source = {
	url = "git://github.com/etactica/lua-libmodbus",
	tag = "v0.6"
}
description = {
	summary = "Lua bindings to libmodbus",
	detailed = [[
		Lua bindings to the libmodbus library.
		The parameters to all functions are as per libmodbus's api
		only with sensible defaults for optional values, and return
		values directly rather than via pointers.
	]],
	homepage = "https://github.com/etactica/lua-libmodbus",
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
			incdirs = { "$(LIBMODBUS_INCDIR)" },
			libdirs = { "$(LIBMODBUS_LIBDIR)" },
		}
	},
	platforms = {
		win32 = {
			modules = {
				libmodbus = {
					libraries = { "modbus", "ws2_32" },
				}
			}
		}
	}
}

