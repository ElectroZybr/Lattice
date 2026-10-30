-- Phony targets named after installed dependency packages, for a package's own build: the
-- library's xmake.lua keeps its add_deps and the child xmake installs nothing again (nested
-- installs rebuild every dependency and overflow MAX_PATH on Windows).

local function quoted(values)
    local items = {}
    for _, value in ipairs(values) do
        table.insert(items, ("%q"):format(value))
    end
    return table.concat(items, ", ")
end

function main(package, names)
    local lines = {}
    for _, name in ipairs(names) do
        local dep = assert(package:dep(name), name .. " is not a dependency of " .. package:name())
        local info = {includedirs = {}, sysincludedirs = {}, linkdirs = {}, links = {}, syslinks = {}, defines = {},
                      rpathdirs = {}}
        for _, package in ipairs(table.join({dep}, dep:orderdeps())) do
            local fetched = package:fetch() or {}
            for key, values in pairs(info) do
                table.join2(values, table.wrap(fetched[key]), table.wrap(package:get(key)))
            end
        end
        table.insert(lines, ('target(%q)\n    set_kind("phony")'):format(name))
        for _, key in ipairs(table.orderkeys(info)) do
            local values = table.unique(info[key])
            if #values > 0 then
                local api = key == "sysincludedirs" and "includedirs" or key
                table.insert(lines, ("    add_%s(%s, {public = true})"):format(api, quoted(values)))
            end
        end
        table.insert(lines, "target_end()")
    end
    return table.concat(lines, "\n") .. "\n"
end
