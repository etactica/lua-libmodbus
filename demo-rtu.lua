--[[
Very simple demo of using client/master APIs
Karl Palsson, April 2016
--]]

mb = require("libmodbus")
print("using libmodbus runtime version: ", mb.version())
print("using lua-libmodbus compiled against libmodbus: ", mb.VERSION_STRING)

-- everything but device will default to modbus recommendations.
local dev = mb.new_rtu("/dev/ttyUSB0", 19200, "even", 8, 1)
print("byte timeout:", dev:get_byte_timeout())
print("response timeout", dev:get_response_timeout())
dev:set_debug()
--dev:rtu_set_serial_mode(mb.RTU_RS232)
--dev:set_error_recovery(mb.ERROR_RECOVERY_LINK, mb.ERROR_RECOVERY_PROTOCOL)
--dev:set_error_recovery(mb.ERROR_RECOVERY_NONE)
ok, err = dev:connect()
if not ok then error("Couldn't connect: " .. err) end

function simple_read(dev)
	dev:set_slave(0x88)
	local base_address = 0x2000
	local regs, err
	regs, err = dev:read_registers(base_address, 10)
	if not regs then error("read failed: " .. err) end

	for r,v in ipairs(regs) do
		print(string.format("register (offset %d) %d: %d (%#x): %#x (%d)",
			r, r, r + base_address - 1, r + base_address -1, v, v))
	end

	--report = dev:report_slave_id()
	--print("report slaveid: ", report:byte(1), report:byte(2), report:sub(3))

end
	
simple_read(dev)

