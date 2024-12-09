function support()
    return {
        windows = true,
        linux = true,
        macosx = false -- TODO
    }
end

function installed()
    return try {
        function()
            os.exec("java -version")
            return true
        end, catch {
            function(e)
                return false
            end
        }
    }
end

function install()
    print("[xlings]: Installing Java...")

    return try {
        function()
            os.exec("java -version")
            return true
        end, catch {
            function(e)
                print("[xlings]: Failed to install Java: " .. e)
                return false
            end
        }
    }
end

function uninstall()
    -- TODO
end

function deps()
    -- TODO: use openjdk17?
    return {
        windows = {
            "openjdk8"
        },
        linux = {
            "openjdk8"
        },
    }
end

function info()
    return {
        name = "java",
        homepage = "https://www.oracle.com/java/",
        author = "Oracle Corporation",
        licenses = "GPL-2.0",
        github = "https://github.com/openjdk/jdk8u",
        docs = "https://docs.oracle.com/javase/8/docs/",
        profile = "Java is a high-level, class-based, object-oriented programming language",
    }
end