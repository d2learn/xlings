# XIM | Xlings Installation Manager

## Code style

> code style guide - tmp

```lua
--- class definition

local CodeStyle = {}
CodeStyle.__index = CodeStyle

-- member funhction
function CodeStyle:help()
    print("help Info: " .. self.info)
end

-- getter and setter -- for deep member variable
function CodeStyle:get_info()
    return self.info
end

function CodeStyle:set_info(new_info)
    self.info = new_info
end

-- private method

function CodeStyle:__other()
    print("CodeStyle: " .. self.info)
end

--- module function

-- export method

function new(name) --- create new object
    local instance = {}
    debug.setmetatable(instance, CodeStyle)
    -- member variable
    instance.name = name or "XIM - By Default"
    instance.info = "XIM Code Style"
    return instance
end

-- unit tests
function _tests()
    print("CodeSytle Test")

    local cs1 = new("Xlings Install Manager")
    local cs2 = new("XIM")

    assert(cs1.name == "Xlings Install Manager", "cs1.name == Xlings Install Manager")
    assert(cs2.name == "XIM", "cs2.name == XIM")

    cs1.name = "cs1"
    cs2.name = "cs2"

    assert(cs1.name == "cs1", "cs1.name == cs1")
    assert(cs2.name == "cs2", "cs2.name == cs2")

    cs1:set_info("cs1 info")
    cs2:set_info("cs2 info")

    assert(cs1:get_info() == "cs1 info", "cs1:get_info() == cs1 info")
    assert(cs2:get_info() == "cs2 info", "cs2:get_info() == cs2 info")

    cs1:help()
    cs2:help()
end

function main(info)
    _tests()
end
```