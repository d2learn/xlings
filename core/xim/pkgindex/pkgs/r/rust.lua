package = {
    homepage = "https://code.visualstudio.com",
    version = "latest",
    name = "rust",
    description = "Visual Studio Code",
    contributor = "https://github.com/microsoft/vscode/graphs/contributors",
    license = "MIT",
    repo = "https://github.com/microsoft/vscode",
    docs = "https://code.visualstudio.com/docs",

    status = "stable",
    categories = {"editor", "tools"},
    keywords = {"vscode", "cross-platform"},
    date = "2024-9-01",

    pmanager = {
        ["latest"] = {
            windows = {
                -- TODO: use winget
                xpm = {url = "https://vscode.download.prss.microsoft.com/dbazure/download/stable/38c31bc77e0dd6ae88a4e9cc93428cc27a56ba40/code_1.93.1-1726079302_amd64.deb", sha256 = nil},
            },
            ubuntu = {
                xpm = {url = "https://vscode.download.prss.microsoft.com/dbazure/download/stable/38c31bc77e0dd6ae88a4e9cc93428cc27a56ba40/VSCodeUserSetup-x64-1.93.1.exe", sha256 = nil},
            },
            arch = {
                -- TODO: add arch support
            },
        }
    },
}