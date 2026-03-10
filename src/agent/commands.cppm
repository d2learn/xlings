export module xlings.agent.commands;

import std;

namespace xlings::agent {

export struct SlashCommand {
    std::string name;        // "/save"
    std::string description; // "Save current session"
    std::function<void(std::string_view args)> handler;
};

export class CommandRegistry {
    std::vector<SlashCommand> commands_;

public:
    void register_command(SlashCommand cmd) {
        commands_.push_back(std::move(cmd));
    }

    // Match commands by prefix (e.g., "/co" matches "/compact", "/copy")
    auto match(std::string_view prefix) const -> std::vector<std::pair<std::string, std::string>> {
        std::vector<std::pair<std::string, std::string>> results;
        for (auto& cmd : commands_) {
            if (cmd.name.starts_with(prefix)) {
                results.emplace_back(cmd.name, cmd.description);
            }
        }
        return results;
    }

    // Execute a slash command. Returns true if handled.
    auto execute(std::string_view input) -> bool {
        // Parse command name and args
        auto sv = input;
        // Trim leading whitespace
        while (!sv.empty() && sv.front() == ' ') sv.remove_prefix(1);
        if (sv.empty() || sv[0] != '/') return false;

        // Extract command name (up to first space)
        auto space = sv.find(' ');
        auto cmd_name = sv.substr(0, space);
        auto args = (space != std::string_view::npos) ? sv.substr(space + 1) : std::string_view{};
        // Trim leading spaces from args
        while (!args.empty() && args.front() == ' ') args.remove_prefix(1);

        for (auto& cmd : commands_) {
            if (cmd.name == cmd_name) {
                if (cmd.handler) cmd.handler(args);
                return true;
            }
        }
        return false;
    }

    auto list_all() const -> const std::vector<SlashCommand>& {
        return commands_;
    }
};

} // namespace xlings::agent
