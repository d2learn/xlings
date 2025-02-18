_exercises_file = "exercises/pylings.py"
_exercises_file_template = [[
def add(a, b)
    return a + b
]]

_tests_file = "tests/pylings-demo.py"
_tests_file_template = [[
import sys, os
# Add the project root to PYTHONPATH
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

# ----------------------------

import exercises.pylings as pylings

print("hello pylings: 1 + 2 = ", pylings.add(1, 2)
]]

_build_file = "xmake.lua"
_build_file_template = [[
target("pylings-demo")
    set_kind("phony")
    add_files("exercises/pylings.py")
    add_files("tests/pylings-demo.py")
    on_run(function (target)
        import("common")
        common.xlings_python(os.scriptdir() .. "/tests/pylings-demo.py")
    end)
]]

--- xrun config template

_xmake_file_template = [[

xlings_runmode = "loop"
xlings_lang = "%s" -- 0.lang

local target_name = "%s" -- 1.name
local target_sourcefile = %s  -- 2.sourcefile
--xlings_name = target_sourcefile

target(target_name)
    set_kind("phony")
    add_files(target_sourcefile)
    on_run(function (target)
        import("common")
        common.xlings_python(target_sourcefile)
    end)

includes("%s") -- 3.xlings_file
]]

function get_template()
    return {
        exercises_file = _exercises_file,
        exercises_file_template = _exercises_file_template,
        tests_file = _tests_file,
        tests_file_template = _tests_file_template,
        build_file = _build_file,
        build_file_template = _build_file_template,
        xmake_file_template = _xmake_file_template
    }
end