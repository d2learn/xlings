import("d2x.templates.c_language")
import("d2x.templates.cpp_language")
import("d2x.templates.py_language")

function get_template(type)
    if type == "c" then
        return c_language.get_template()
    elseif type == "cpp" then
        return cpp_language.get_template()
    elseif type == "python" then
        return py_language.get_template()
    end
end