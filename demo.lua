--[[
Very simple demo of using client/master APIs
Karl Palsson, April 2016
--]]

mb = require("libmodbus")
print("using libmodbus runtime version: ", mb.version())
print("using lua-libmodbus compiled against libmodbus: ", mb.VERSION_STRING)

local dev = mb.new_tcp_pi("192.168.255.74", 1502)
print(dev:get_byte_timeout())
print(dev:get_response_timeout())
dev:set_debug()
--dev:set_error_recovery(mb.ERROR_RECOVERY_LINK, mb.ERROR_RECOVERY_PROTOCOL)
--dev:set_error_recovery(mb.ERROR_RECOVERY_NONE)
print(dev:connect())
dev:set_slave(0xb6)
local base_address = 0x2000
local regs, err = dev:read_registers(base_address, 10)
if not regs then error("read failed: " .. err) end

for r,v in ipairs(regs) do
	print(string.format("register (offset %d) %d: %d (%#x): %#x (%d)",
		r, r, r + base_address - 1, r + base_address -1, v, v))
end

-- will autoround 32.98 to 32.  no magic support for writing floats yet :|
-- numbers are cast to uint16_t internally.
local res, err = dev:write_registers(0x2000, { 0xabcd, 32.98, 0xfffe, 0xabcd, -1 })
if not res then print(err) end
regs, err = dev:read_registers(base_address, 5)
print("so, no magic casting 16bit tosigned!", regs[5], -1)

print(dev:write_register(0x2006, 2))

