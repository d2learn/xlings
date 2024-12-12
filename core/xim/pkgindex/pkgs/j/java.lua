package = {
    -- base info
    name = "java",

    -- xim pkg info
    status = "stable", -- dev, stable, deprecated
    categories = {"plang", "pm-wrapper"},
    keywords = {"java", "openjdk"},

    pm_wrapper = {
        winget = "AdoptOpenJDK.OpenJDK.8",
        apt = "openjdk-8-jdk",
        pacman = "jdk8-openjdk",
    }
}
