export module mcpplibs.cmdline;

import std;

export import :options;
export import :parse;

namespace mcpplibs::cmdline {

class SubcommandBuilder;
class SubcommandArgBuilder;
class OptBuilder;
class ArgBuilder;

export using Argv = std::vector<std::string>;
export using detail::Arg;
export using detail::Option;
export using detail::OptionValue;
export using detail::ParseError;
export using detail::ParseResult;
export using detail::ParsedArgs;

export class App {
    template <typename> friend class AppItemBuilder;
    friend class SubcommandBuilder;
    friend class OptBuilder;
    friend class ArgBuilder;
public:
    explicit App(std::string_view name) : name_(name) {}
    App(App&&) noexcept = default;
    App& operator=(App&&) noexcept = default;
    App(const App& other) : name_(other.name_), version_(other.version_), description_(other.description_), author_(other.author_),
        args_(other.args_), options_(other.options_), subcommands_(other.subcommands_), action_(other.action_) {}
    App& operator=(const App& other) {
        if (this == &other) return *this;
        name_ = other.name_;
        version_ = other.version_;
        description_ = other.description_;
        author_ = other.author_;
        args_ = other.args_;
        options_ = other.options_;
        subcommands_ = other.subcommands_;
        action_ = other.action_;
        return *this;
    }

    [[nodiscard]] App& version(std::string_view version) {
        version_ = version;
        return *this;
    }
    [[nodiscard]] App& author(std::string_view author) {
        author_ = author;
        return *this;
    }
    [[nodiscard]] App& description(std::string_view desc) {
        description_ = desc;
        return *this;
    }
    [[nodiscard]] App& arg(Arg argument) {
        args_.push_back(std::move(argument));
        return *this;
    }
    [[nodiscard]] ArgBuilder arg(std::string_view name);
    [[nodiscard]] App& option(Option opt) {
        options_.push_back(std::move(opt));
        return *this;
    }
    [[nodiscard]] OptBuilder option(std::string_view name);
    [[nodiscard]] App& subcommand(App sub) {
        subcommands_.push_back(std::move(sub));
        return *this;
    }
    [[nodiscard]] SubcommandBuilder subcommand(std::string_view name);
    template <typename Fn>
    [[nodiscard]] App& action(Fn&& fn) {
        action_ = std::function<void(const ParsedArgs&)>(std::forward<Fn>(fn));
        return *this;
    }

    [[nodiscard]] ParseResult parse(int argc, char* argv[]) const {
        std::vector<std::string_view> tokens;
        tokens.reserve(argc > 1 ? argc - 1 : 0);
        for (int i = 1; i < argc; ++i) tokens.emplace_back(argv[i]);
        global_keys_t global_keys;
        global_opts_t global_opts;
        std::vector<Option> root_globals;
        for (const auto& opt : options_)
            if (opt.global_) { global_keys.insert(option_key(opt)); root_globals.push_back(opt); }
        return parse_impl(name_, tokens, global_keys, global_opts, root_globals);
    }
    [[nodiscard]] ParseResult parse_from(std::span<const std::string> args) const {
        if (args.empty()) return std::unexpected(ParseError{.message = "no program name"});
        std::vector<std::string_view> tokens;
        tokens.reserve(args.size() - 1);
        for (std::size_t i = 1; i < args.size(); ++i) tokens.emplace_back(args[i]);
        global_keys_t global_keys;
        global_opts_t global_opts;
        std::vector<Option> root_globals;
        for (const auto& opt : options_)
            if (opt.global_) { global_keys.insert(option_key(opt)); root_globals.push_back(opt); }
        return parse_impl(name_, tokens, global_keys, global_opts, root_globals);
    }
    [[nodiscard]] ParseResult parse_from(std::string_view command_line) const {
        std::vector<std::string> tokens = split_command_line(command_line);
        if (tokens.empty()) return std::unexpected(ParseError{.message = "empty command line"});
        return parse_from(std::span<const std::string>(tokens));
    }

    void run(const ParsedArgs& parsed) const {
        if (parsed.has_subcommand()) {
            for (const auto& sub : subcommands_) {
                if (sub.name_ == parsed.subcommand_name()) {
                    auto sub_ref = parsed.subcommand();
                    if (sub_ref && sub.action_) sub.action_(sub_ref->get());
                    return;
                }
            }
        } else if (action_) {
            action_(parsed);
        }
    }

    int run(int argc, char* argv[]) const {
        auto result = parse(argc, argv);
        if (!result) {
            if (result.error().is_error())
                std::println("Error: {}", result.error().message);
            return result.error().is_error() ? 1 : 0;
        }
        run(*result);
        return 0;
    }

    void print_help(std::string_view program_name = "") const {
        std::string prog = program_name.empty() ? std::string(name_) : std::string(program_name);
        if (!version_.empty()) std::println("{} {}", prog, version_);
        if (!description_.empty()) std::println("\n{}\n", description_);
        std::println("USAGE:");
        std::println("    {} [OPTIONS]", prog);
        for (const auto& argument : args_)
            std::println("    {} <{}>", prog, argument.name);
        if (!options_.empty()) {
            std::println("\nOPTIONS:");
            for (const auto& opt : options_) {
                std::string opt_str;
                if (opt.short_) opt_str += std::format("-{}, ", opt.short_);
                if (!opt.long_name.empty()) opt_str += "--" + opt.long_name;
                if (!opt.value_name_.empty()) opt_str += " <" + opt.value_name_ + ">";
                std::println("    {:20} {}", opt_str, opt.help_);
            }
        }
        if (!subcommands_.empty()) {
            std::println("\nSUBCOMMANDS:");
            for (const auto& sub : subcommands_)
                std::println("    {:12} {}", sub.name_, sub.description_.empty() ? "" : sub.description_);
        }
    }

private:
    using global_keys_t = std::set<std::string, std::less<>>;
    using global_opts_t = std::map<std::string, OptionValue, std::less<>>;
    using OptRef = std::optional<std::reference_wrapper<const Option>>;

    static std::vector<std::string> split_command_line(std::string_view command_line) {
        std::vector<std::string> tokens;
        std::string current;
        bool in_quote = false;
        char quote_char = '\0';
        for (char ch : command_line) {
            if (in_quote) {
                if (ch == quote_char) in_quote = false;
                else current += ch;
            } else if (ch == '"' || ch == '\'') {
                in_quote = true;
                quote_char = ch;
            } else if (std::isspace(static_cast<unsigned char>(ch))) {
                if (!current.empty()) {
                    tokens.push_back(std::move(current));
                    current.clear();
                }
            } else {
                current += ch;
            }
        }
        if (!current.empty()) tokens.push_back(std::move(current));
        return tokens;
    }

    [[nodiscard]] ParseResult parse_impl(
        std::string_view program_name,
        std::span<const std::string_view> tokens,
        const global_keys_t& global_keys,
        global_opts_t& global_opts,
        const std::vector<Option>& root_global_options) const {

        ParsedArgs result;
        std::vector<std::string> positionals;
        bool only_positionals = false;

        auto apply_option = [&](const Option& opt, const std::string& key_str, std::string val, bool is_flag) {
            if (global_keys.contains(key_str)) {
                if (is_flag) global_opts[key_str].count++;
                else global_opts[key_str] = OptionValue{1, {std::move(val)}};
            } else {
                if (is_flag) result.opts[key_str].count++;
                else if (opt.multiple_) result.opts[key_str].values.push_back(std::move(val));
                else result.opts[key_str] = OptionValue{1, {std::move(val)}};
            }
        };

        auto find_global_option = [&root_global_options](std::string_view long_name, char short_name) -> OptRef {
            for (const auto& opt : root_global_options) {
                if (short_name && opt.matches_short(short_name)) return std::cref(opt);
                if (!long_name.empty() && opt.matches_long(long_name)) return std::cref(opt);
            }
            return std::nullopt;
        };

        for (std::size_t i = 0; i < tokens.size(); ++i) {
            std::string_view token = tokens[i];
            if (only_positionals) {
                positionals.emplace_back(token);
                continue;
            }
            if (token == "--") {
                only_positionals = true;
                continue;
            }
            if (token == "-h" || token == "--help") {
                print_help(program_name);
                return std::unexpected(ParseError::make_help());
            }
            if (token == "--version" && !version_.empty()) {
                std::println("{}", version_);
                return std::unexpected(ParseError::make_version());
            }
            if (!subcommands_.empty() && !token.starts_with('-')) {
                for (const auto& sub : subcommands_) {
                    if (sub.name_ == token) {
                        result.subcommand_name_ = sub.name_;
                        auto sub_result = sub.parse_impl(std::format("{} {}", program_name, sub.name_), tokens.subspan(i + 1), global_keys, global_opts, root_global_options);
                        if (!sub_result) return sub_result;
                        result.subcommand_matches = std::make_unique<ParsedArgs>(std::move(*sub_result));
                        result.positionals = std::move(positionals);
                        for (auto& [k, v] : global_opts) result.opts[k] = std::move(v);
                        return finish_parse(std::move(result));
                    }
                }
            }
            if (token.starts_with("--") && token.size() > 2) {
                std::string_view rest = token.substr(2);
                auto eq_pos = rest.find('=');
                std::string_view key = eq_pos != std::string_view::npos ? rest.substr(0, eq_pos) : rest;
                std::string_view value_part = eq_pos != std::string_view::npos ? rest.substr(eq_pos + 1) : std::string_view{};
                auto opt = find_option(key, '\0');
                if (!opt) opt = find_global_option(key, '\0');
                if (!opt) return std::unexpected(ParseError{.message = std::format("unknown option: --{}", key)});
                const Option& found = opt->get();
                std::string key_str = option_key(found);
                if (found.takes_value_) {
                    std::string val;
                    if (eq_pos != std::string_view::npos)
                        val = std::string(value_part);
                    else if (i + 1 < tokens.size())
                        val = std::string(tokens[++i]);
                    else
                        return std::unexpected(ParseError{.message = std::format("option --{} requires a value", key)});
                    apply_option(found, key_str, std::move(val), false);
                } else {
                    apply_option(found, key_str, "", true);
                }
                continue;
            }
            if (token.size() == 2 && token[0] == '-' && token[1] != '-') {
                char short_char = token[1];
                auto opt = find_option("", short_char);
                if (!opt) opt = find_global_option("", short_char);
                if (!opt) return std::unexpected(ParseError{.message = std::format("unknown option: -{}", short_char)});
                const Option& found = opt->get();
                std::string key_str = option_key(found);
                if (found.takes_value_) {
                    if (i + 1 >= tokens.size()) return std::unexpected(ParseError{.message = std::format("option -{} requires a value", short_char)});
                    std::string val(tokens[++i]);
                    apply_option(found, key_str, std::move(val), false);
                } else {
                    apply_option(found, key_str, "", true);
                }
                continue;
            }
            positionals.emplace_back(token);
        }

        result.positionals = std::move(positionals);
        for (auto& [k, v] : global_opts) result.opts[k] = std::move(v);
        return finish_parse(std::move(result));
    }

    ParseResult finish_parse(ParsedArgs parsed) const {
        for (std::size_t idx = 0; idx < args_.size(); ++idx) {
            const auto& argument = args_[idx];
            if (idx < parsed.positionals.size()) continue;
            if (argument.required_) return std::unexpected(ParseError{.message = std::format("required argument '{}' missing", argument.name)});
            if (!argument.default_val.empty()) parsed.positionals.push_back(argument.default_val);
        }
        parsed.positional_names_.reserve(args_.size());
        for (const auto& argument : args_) parsed.positional_names_.push_back(argument.name);
        return parsed;
    }

    OptRef find_option(std::string_view long_name, char short_name) const {
        for (const auto& opt : options_) {
            if (short_name && opt.matches_short(short_name)) return std::cref(opt);
            if (!long_name.empty() && opt.matches_long(long_name)) return std::cref(opt);
        }
        return std::nullopt;
    }
    static std::string option_key(const Option& opt) {
        return opt.long_name.empty() ? std::string(1, opt.short_) : opt.long_name;
    }

    std::string name_;
    std::string version_;
    std::string description_;
    std::string author_;
    std::vector<Arg> args_;
    std::vector<Option> options_;
    std::vector<App> subcommands_;
    std::function<void(const ParsedArgs&)> action_;
};

// CRTP base: shared transition methods and parent/commit management for OptBuilder & ArgBuilder.
template <typename Derived>
class AppItemBuilder {
protected:
    App* parent_;
    bool committed_ = false;

    explicit AppItemBuilder(App* parent) : parent_(parent) {}
    AppItemBuilder(AppItemBuilder&& o) noexcept : parent_(o.parent_), committed_(o.committed_) { o.parent_ = nullptr; }
    AppItemBuilder(const AppItemBuilder&) = delete;
    AppItemBuilder& operator=(const AppItemBuilder&) = delete;
    ~AppItemBuilder() = default;

    void commit() {
        if (parent_ && !committed_) {
            static_cast<Derived*>(this)->do_commit();
            committed_ = true;
        }
    }

public:
    App& option(Option opt)              { commit(); parent_->options_.push_back(std::move(opt)); return *parent_; }
    OptBuilder option(std::string_view name);
    App& arg(Arg argument)               { commit(); parent_->args_.push_back(std::move(argument)); return *parent_; }
    ArgBuilder arg(std::string_view name);
    App& subcommand(App sub)             { commit(); parent_->subcommands_.push_back(std::move(sub)); return *parent_; }
    SubcommandBuilder subcommand(std::string_view name);
    App& version(std::string_view v)     { commit(); (void)parent_->version(v); return *parent_; }
    App& description(std::string_view d) { commit(); (void)parent_->description(d); return *parent_; }
    App& author(std::string_view a)      { commit(); (void)parent_->author(a); return *parent_; }
    template <typename Fn>
    App& action(Fn&& fn)                 { commit(); (void)parent_->action(std::forward<Fn>(fn)); return *parent_; }
    App& end()                           { auto* p = parent_; commit(); parent_ = nullptr; return *p; }
};

class OptBuilder : public AppItemBuilder<OptBuilder> {
    friend class AppItemBuilder<OptBuilder>;
public:
    OptBuilder(App* parent, Option opt) : AppItemBuilder(parent), option_(std::move(opt)) {}
    OptBuilder(OptBuilder&& o) noexcept : AppItemBuilder(std::move(o)), option_(std::move(o.option_)) {}
    OptBuilder& operator=(OptBuilder&& o) noexcept {
        if (this != &o) { commit(); parent_ = o.parent_; committed_ = o.committed_; o.parent_ = nullptr; option_ = std::move(o.option_); }
        return *this;
    }
    ~OptBuilder() { commit(); }

    OptBuilder& long_opt(std::string_view v)   { (void)option_.long_opt(v); return *this; }
    OptBuilder& short_name(char c)             { (void)option_.short_name(c); return *this; }
    OptBuilder& help(std::string_view h)       { (void)option_.help(h); return *this; }
    OptBuilder& takes_value(bool v = true)     { (void)option_.takes_value(v); return *this; }
    OptBuilder& value_name(std::string_view v) { (void)option_.value_name(v); return *this; }
    OptBuilder& multiple(bool m = true)        { (void)option_.multiple(m); return *this; }
    OptBuilder& global(bool g = true)          { (void)option_.global(g); return *this; }

private:
    void do_commit() { parent_->options_.push_back(std::move(option_)); }
    Option option_;
};

class ArgBuilder : public AppItemBuilder<ArgBuilder> {
    friend class AppItemBuilder<ArgBuilder>;
public:
    ArgBuilder(App* parent, Arg a) : AppItemBuilder(parent), arg_(std::move(a)) {}
    ArgBuilder(ArgBuilder&& o) noexcept : AppItemBuilder(std::move(o)), arg_(std::move(o.arg_)) {}
    ArgBuilder& operator=(ArgBuilder&& o) noexcept {
        if (this != &o) { commit(); parent_ = o.parent_; committed_ = o.committed_; o.parent_ = nullptr; arg_ = std::move(o.arg_); }
        return *this;
    }
    ~ArgBuilder() { commit(); }

    ArgBuilder& help(std::string_view h)          { (void)arg_.help(h); return *this; }
    ArgBuilder& required(bool r = true)           { (void)arg_.required(r); return *this; }
    ArgBuilder& default_value(std::string_view v) { (void)arg_.default_value(v); return *this; }

private:
    void do_commit() { parent_->args_.push_back(std::move(arg_)); }
    Arg arg_;
};

class SubcommandBuilder {
    friend class SubcommandArgBuilder;
public:
    SubcommandBuilder(App* parent, App sub) : parent_(parent), sub_(std::move(sub)) {}
    SubcommandBuilder(SubcommandBuilder&& o) noexcept
        : parent_(o.parent_), sub_(std::move(o.sub_)), committed_(o.committed_) { o.parent_ = nullptr; }
    SubcommandBuilder& operator=(SubcommandBuilder&& o) noexcept {
        if (this != &o) { commit(); parent_ = o.parent_; sub_ = std::move(o.sub_); committed_ = o.committed_; o.parent_ = nullptr; }
        return *this;
    }
    SubcommandBuilder(const SubcommandBuilder&) = delete;
    SubcommandBuilder& operator=(const SubcommandBuilder&) = delete;
    ~SubcommandBuilder() { commit(); }

    SubcommandBuilder& version(std::string_view v)     { (void)sub_.version(v); return *this; }
    SubcommandBuilder& author(std::string_view a)      { (void)sub_.author(a); return *this; }
    SubcommandBuilder& description(std::string_view d) { (void)sub_.description(d); return *this; }
    SubcommandBuilder& arg(Arg argument)               { (void)sub_.arg(std::move(argument)); return *this; }
    SubcommandArgBuilder arg(std::string_view name);
    SubcommandBuilder& option(Option opt)              { (void)sub_.option(std::move(opt)); return *this; }
    OptBuilder option(std::string_view name);
    template <typename Fn>
    App& action(Fn&& fn) {
        (void)sub_.action(std::forward<Fn>(fn));
        auto* p = parent_; commit(); return *p;
    }
    SubcommandBuilder& subcommand(std::string_view name) {
        commit(); sub_ = App(name); committed_ = false; return *this;
    }
    App& end() { auto* p = parent_; commit(); parent_ = nullptr; return *p; }

private:
    void commit() {
        if (parent_ && !committed_) { parent_->subcommands_.push_back(std::move(sub_)); committed_ = true; }
    }
    App* parent_;
    App sub_;
    bool committed_ = false;
};

class SubcommandArgBuilder {
public:
    SubcommandArgBuilder(SubcommandArgBuilder&& o) noexcept
        : sb_(o.sb_), arg_(std::move(o.arg_)), committed_(o.committed_) { o.sb_ = nullptr; }
    SubcommandArgBuilder& operator=(SubcommandArgBuilder&&) = delete;
    SubcommandArgBuilder(const SubcommandArgBuilder&) = delete;
    SubcommandArgBuilder& operator=(const SubcommandArgBuilder&) = delete;
    ~SubcommandArgBuilder() { commit(); }

    SubcommandArgBuilder& required(bool r = true)           { (void)arg_.required(r); return *this; }
    SubcommandArgBuilder& help(std::string_view h)          { (void)arg_.help(h); return *this; }
    SubcommandArgBuilder& default_value(std::string_view v) { (void)arg_.default_value(v); return *this; }
    SubcommandArgBuilder arg(std::string_view name)         { commit(); return sb_->arg(name); }
    template <typename Fn>
    App& action(Fn&& fn) { commit(); return sb_->action(std::forward<Fn>(fn)); }

private:
    SubcommandArgBuilder(SubcommandBuilder* sb, Arg a) : sb_(sb), arg_(std::move(a)) {}
    void commit() {
        if (sb_ && !committed_) { (void)sb_->sub_.arg(std::move(arg_)); committed_ = true; }
    }
    SubcommandBuilder* sb_ = nullptr;
    Arg arg_;
    bool committed_ = false;
    friend class SubcommandBuilder;
};

inline SubcommandBuilder App::subcommand(std::string_view name) { return SubcommandBuilder(this, App(name)); }
inline OptBuilder App::option(std::string_view name) { return OptBuilder(this, Option(name)); }
inline ArgBuilder App::arg(std::string_view name) { return ArgBuilder(this, Arg(name)); }

template <typename D>
OptBuilder AppItemBuilder<D>::option(std::string_view name) { this->commit(); return parent_->option(name); }
template <typename D>
ArgBuilder AppItemBuilder<D>::arg(std::string_view name) { this->commit(); return parent_->arg(name); }
template <typename D>
SubcommandBuilder AppItemBuilder<D>::subcommand(std::string_view name) { this->commit(); return parent_->subcommand(name); }

inline OptBuilder SubcommandBuilder::option(std::string_view name) { return OptBuilder(&sub_, Option(name)); }
inline SubcommandArgBuilder SubcommandBuilder::arg(std::string_view name) { return SubcommandArgBuilder(this, Arg(name)); }

} // namespace mcpplibs::cmdline
