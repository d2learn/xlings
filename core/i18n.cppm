export module xlings.i18n;

import std;

import xlings.platform;

namespace xlings::i18n {

std::string gLang_;

export void set_language(const std::string& lang) {
    gLang_ = lang;
}

export [[nodiscard]] const std::string& language() {
    if (gLang_.empty()) {
        gLang_ = platform::get_system_language();
        if (gLang_ != "zh") {
            gLang_ = "en";
        }
    }
    return gLang_;
}

export [[nodiscard]] bool is_chinese() {
    return language() == "zh";
}

} // namespace xlings::i18n
