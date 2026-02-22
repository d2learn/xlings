function i18n_print(object, ...)
    if type(object) == "string" then
        cprint(object, ...)
    elseif type(object) == "table" then
        -- is [ "1", "2", "3" ]
        for _, v in ipairs(object) do
            cprint(v)
        end
    else
        print(tostring(object), ...)
    end
end