--[[
Very simple demo of using client/master APIs
Karl Palsson, April 2016
--]]

mb = require("libmodbus")
print("using libmodbus runtime version: ", mb.version())
print("using lua-libmodbus compiled against libmodbus: ", mb.VERSION_STRING)

local dev = mb.new_tcp_pi("localhost", 1502)
print(dev:get_byte_timeout())
print(dev:get_response_timeout())
dev:set_debug()
--dev:set_error_recovery(mb.ERROR_RECOVERY_LINK, mb.ERROR_RECOVERY_PROTOCOL)
--dev:set_error_recovery(mb.ERROR_RECOVERY_NONE)
ok, err = dev:connect()
if not ok then error("Couldn't connect: " .. err) end

function lmb_unit_test(dev)
	dev:set_slave(17)
	local base_address = 0x16b
	local regs, err
	regs, err = dev:read_registers(base_address, 3)
	if not regs then error("read failed: " .. err) end

	print("read 3 from 0x16b: ")
	for r,v in ipairs(regs) do
		print(string.format("register (offset %d) %d: %d (%#x): %#x (%d)",
			r, r, r + base_address - 1, r + base_address -1, v, v))
	end

	print("write single 0x16c -> 0xabcd ", dev:write_register(0x16c, 0xabcd))
	regs, err = dev:read_registers(0x16c, 1)
	if not regs then error("read1 failed: ", err) end
	print("reading back:  single 0x16c: ", string.format("%x", regs[1]))

	regs, err = dev:read_input_registers(0x108, 1)
	if not regs then print("read input regs failed", err) end
	print("should be 0xa: ", string.format("%x", regs[1]))

	bits, err = dev:read_bits(0x130, 0x25)
	if not bits then print("read bits failed", err) end
	for r,v in ipairs(bits) do
		print(string.format("bit (offset %d) %d: %d (%#x): %#x (%d)",
			r, r, r + 0x130 - 1, r + 0x130 -1, v, v))
	end

	bits, err = dev:write_bits(0x130, { 0xffff,0, true,0,false,1,0,99 })
	if not bits then print("write bits failed", err) end
	bits, err = dev:read_bits(0x130, 8)
	print("reading back 8 bits, should be 0xa5")
	for r,v in ipairs(bits) do
		print(string.format("bit (offset %d) %d: %d (%#x): %#x (%d)",
			r, r, r + 0x130 - 1, r + 0x130 -1, v, v))
	end


	bits, err = dev:read_input_bits(0x1c4, 8)
	if not bits then print("read bits failed", err) end
	for r,v in ipairs(bits) do
		print(string.format("bit (offset %d) %d: %d (%#x): %#x (%d)",
			r, r, r + 0x1c4 - 1, r + 0x1c4 -1, v, v))
	end

	report = dev:report_slave_id()
	print("report slaveid: ", report:byte(1), report:byte(2), report:sub(3))

end
	

function write_test(dev)
-- will autoround 32.98 to 32.  no magic support for writing floats yet :|
-- numbers are cast to uint16_t internally.
	local res, err = dev:write_registers(0x2000, { 0xabcd, 32.98, 0xfffe, 0xabcd, -1 })
	if not res then print(err) end
	regs, err = dev:read_registers(base_address, 5)
	print("so, no magic casting 16bit tosigned!", regs[5], -1)

	print(dev:write_register(0x2006, 2))
end

lmb_unit_test(dev)
