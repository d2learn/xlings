local xlings_install_dir = {
    linux = os.getenv("HOME") .. "/.xlings",
    windows = "C:\\Users\\Public\\xlings",
}

-- v0.4.40
local xlings_mdbook_url = {
    linux = "https://github.com/rust-lang/mdBook/releases/download/v0.4.40/mdbook-v0.4.40-x86_64-unknown-linux-gnu.tar.gz",
    windows = "https://github.com/rust-lang/mdBook/releases/download/v0.4.40/mdbook-v0.4.40-x86_64-pc-windows-msvc.zip",
}

local xlings_sourcedir = os.scriptdir() .. "/../"

function get_config_info()
    return {
        install_dir = xlings_install_dir[os.host()],
        sourcedir = xlings_sourcedir,
        mdbook_url = xlings_mdbook_url[os.host()],
    }
end

