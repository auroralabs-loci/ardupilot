-- Run from the repository root with Lua 5.3 or newer.
package.path = 'libraries/AP_Scripting/modules/?.lua;' .. package.path
local mav = require('MAVLink/mavlink_msgs')
-- Independent wire CRC fixtures for all signing/source-width/target flags.
local checksums = {0xd88a, 0x206d, 0x892f, 0xbe43, 0x4cfc, 0x93aa, 0x7066, 0xbd36}
local payload = string.pack('<I4BBBBB', 0, 2, 3, 81, 4, 3)
for flags=0,7 do
  local source = flags & 2 ~= 0 and 0xabcdef12 or 42
  local target = flags & 2 ~= 0 and 0xfedcba98 or 7
  for _,storage in ipairs({16,256,264}) do
    local data = string.pack('<I2BBBBBI4B', checksums[flags+1], 253, 9, flags, 1, 0, source, 11)
      .. string.rep('\0', 3) .. payload .. string.rep('\0', storage - #payload + 15)
      .. string.pack('<I4B', target, 250)
    local msg = assert(mav.decode(data, {[0]='HEARTBEAT'}, true))
    assert(msg.sysid == source and msg.base_mode == 81)
    assert(msg.target_sysid == (flags & 4 ~= 0 and target or nil))
    assert(msg.target_compid == (flags & 4 ~= 0 and 250 or nil))
    if storage == 264 then assert(mav.decode(data, {[0]='HEARTBEAT'})) end
    local bad = data:sub(1,4)..string.char(flags | 128)..data:sub(6)
    assert(mav.decode(bad, {[0]='HEARTBEAT'}, true) == nil)
    bad = string.char(data:byte(1) ~ 1)..data:sub(2)
    assert(mav.decode(bad, {[0]='HEARTBEAT'}, true) == nil)
  end
end
print('MAVLink layout and target tests passed')
