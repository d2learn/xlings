function support()
    return {
        windows = true,
        linux = true,
        macosx = false, -- TODO
    }
end

function installed()
    return try {
        function()
            os.exec("dotnet --version")
            return true
        end, catch {
            function(e)
                return false
            end
        }
    }
end

function install()
    print("[xlings]: Installing C# compiler...")

    return try {
        function()
            -- os.exec("csc -version")
            -- os.exec("dotnet tool install -g csc")
            os.exec("dotnet --version")
            cprint("[xlings]: csc -> csc.dll.")
            cprint("\n\t ${yellow}https://github.com/dotnet/sdk/issues/8742${clear} \n")

            return true
        end, catch {
            function(e)
                print("[xlings]: Failed to install C# compiler: " .. e)
                return false
            end
        }
    }
end

function uninstall()
    -- TODO: implement uninstall logic for different platforms
end

function deps()
    return {
        windows = {
            "dotnet"
        },
        linux = {
            "dotnet"
        },
        macosx = {
            "dotnet"
        }
    }
end

function info()
    return {
        name = "csharp",
        homepage = "https://docs.microsoft.com/dotnet/csharp/",
        author = "Microsoft Corporation",
        licenses = "MIT",
        github = "https://github.com/dotnet/csharplang",
        docs = "https://learn.microsoft.com/dotnet/csharp/",
        profile = "CShparp is a modern, object-oriented programming language developed by Microsoft",
    }
end