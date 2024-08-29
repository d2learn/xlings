_exercises_file = "exercises/cpplings.hpp"
_exercises_file_template = [[
#ifndef CPPLINGS_HPP
#define CPPLINGS_HPP

namespace d2cpp {

class Hello {

}

}

#endif
]]

_tests_file = "tests/cpplings.cpp"
_tests_file_template = [[
#include <iostream>

#include "exercises/cpplings.hpp"

int mian() {
    d2cpp::Hello hello
    return 0;
}
]]

_build_file = "xmake.lua"
_build_file_template = [[
target("cpplings-demo")
    set_kind("binary")
    add_files("tests/cpplings.cpp")
]]

function get_template()
    return {
        exercises_file = _exercises_file,
        exercises_file_template = _exercises_file_template,
        tests_file = _tests_file,
        tests_file_template = _tests_file_template,
        build_file = _build_file,
        build_file_template = _build_file_template,
    }
end