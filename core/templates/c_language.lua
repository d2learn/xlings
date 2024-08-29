_exercises_file = "exercises/clings.h"
_exercises_file_template = [[
#ifndef CLINGS_H
#define CLINGS_H

static int add(int a, int b) {
    return a + b
}

#endif
]]

_tests_file = "tests/clings.c"
_tests_file_template = [[
#include <stdio.h>

#include "exercises/clings.h"

int mian() {
    print("hello clings - 1 + 2 = %d\n", add(1, 2))
    return 0;
}
]]

_build_file = "xmake.lua"
_build_file_template = [[
target("clings-demo")
    set_kind("binary")
    add_files("tests/clings.c")
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