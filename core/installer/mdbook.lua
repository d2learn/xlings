import("utils.archive")

import("common")
import("platform")

-- v0.4.40
local mdbook_url = {
    linux = "https://github.com/rust-lang/mdBook/releases/download/v0.4.40/mdbook-v0.4.40-x86_64-unknown-linux-gnu.tar.gz",
    windows = "https://github.com/rust-lang/mdBook/releases/download/v0.4.40/mdbook-v0.4.40-x86_64-pc-windows-msvc.zip",
}

local mdbook_bin = {
    linux = "mdbook",
    windows = "mdbook.exe",
}

local mdbook_zip = {
    linux = "mdbook.tar.gz",
    windows = "mdbook.zip",
}

local install_dir = platform.get_config_info().bindir
local mdbook_bin_file = path.join(install_dir, mdbook_bin[os.host()])
local mdbook_zip_file = path.join(platform.get_config_info().rcachedir, mdbook_zip[os.host()])

function installed()
    return try {
        function()
            os.exec("mdbook --version")
            return true
        end, catch {
            function(e)
                return false
            end
        }
    }
end

function install()

    cprint("[xlings]: install mdbook...")

    local release_url = mdbook_url[os.host()]

    if not os.isfile(mdbook_zip_file) then
        common.xlings_download(release_url, mdbook_zip_file)
    end

    archive.extract(mdbook_zip_file, install_dir)

    if os.isfile(mdbook_bin_file) then
        cprint("[xlings]: mdbook installed")
    end

end