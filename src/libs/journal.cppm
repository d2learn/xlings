export module xlings.libs.journal;

import std;
import xlings.libs.json;
import xlings.libs.agentfs;

namespace xlings::libs::journal {

export class Journal {
    agentfs::AgentFS& fs_;

    auto today_path_() const -> std::filesystem::path {
        auto now = std::chrono::system_clock::now();
        auto days = std::chrono::floor<std::chrono::days>(now);
        std::chrono::year_month_day ymd{days};
        std::ostringstream oss;
        oss << int(ymd.year()) << "-"
            << std::setw(2) << std::setfill('0') << unsigned(ymd.month()) << "-"
            << std::setw(2) << std::setfill('0') << unsigned(ymd.day())
            << ".jsonl";
        return fs_.journal_path() / oss.str();
    }

    void append_(const nlohmann::json& entry) {
        auto j = entry;
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        j["timestamp_ms"] = ms;
        agentfs::AgentFS::append_jsonl(today_path_(), j);
    }

public:
    explicit Journal(agentfs::AgentFS& fs) : fs_(fs) {}

    // For testing: write to a specific path
    void log_llm_turn(std::string_view role, std::string_view content) {
        nlohmann::json j;
        j["type"] = "llm_turn";
        j["role"] = std::string(role);
        j["content"] = std::string(content);
        append_(j);
    }

    void log_tool_call(std::string_view tool_name, std::string_view arguments) {
        nlohmann::json j;
        j["type"] = "tool_call";
        j["tool"] = std::string(tool_name);
        j["arguments"] = std::string(arguments);
        append_(j);
    }

    void log_tool_result(std::string_view tool_name, std::string_view result, bool is_error) {
        nlohmann::json j;
        j["type"] = "tool_result";
        j["tool"] = std::string(tool_name);
        j["result"] = std::string(result);
        j["is_error"] = is_error;
        append_(j);
    }

    auto read_today() const -> std::vector<nlohmann::json> {
        return agentfs::AgentFS::read_jsonl(today_path_());
    }

    // Read entries from a specific file (for testing)
    static auto read_file(const std::filesystem::path& path) -> std::vector<nlohmann::json> {
        return agentfs::AgentFS::read_jsonl(path);
    }
};

} // namespace xlings::libs::journal
