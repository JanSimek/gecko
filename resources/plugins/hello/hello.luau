-- Gecko example plugin (read-only).
--
-- A plugin's entry script runs once when the plugin is enabled, in a resident, sandboxed Luau VM
-- whose print() output is collected in the Plugin Manager's console. This MVP grants plugins the
-- read-only half of the map api: queries work, map mutators are simply absent. Tools, panels,
-- menus and map-write permissions arrive in a later release.

print("Hello from the Gecko example plugin!")

-- Pure-geometry queries need no open map, so they always work.
print("hex 0 is valid: " .. tostring(api:isValidHex(0)))
print("hex 20100 has " .. #api:hexNeighbors(20100) .. " neighbours")

-- Queries that read the open map raise when no map is open; pcall so that is reported cleanly
-- instead of counting as a plugin fault.
local ok, floor = pcall(function()
    return api:getFloor(0)
end)
if ok then
    print("floor tile id at tile index 0 is " .. floor)
else
    print("no map is open yet — open one and re-enable to read it")
end
