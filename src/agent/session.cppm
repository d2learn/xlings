export module xlings.agent.session;

import std;
import mcpplibs.llmapi;
import xlings.libs.json;
import xlings.libs.agentfs;

namespace xlings::agent {

namespace llm = mcpplibs::llmapi;

export struct SessionMeta {
    std::string id;
    std::string title;
    std::string model;
    std::string created_at;
    std::string updated_at;
    int turn_count { 0 };
};

export class SessionManager {
    libs::agentfs::AgentFS& fs_;

    auto session_dir_(std::string_view id) const {
        return fs_.sessions_path() / std::string(id);
    }

    auto meta_path_(std::string_view id) const {
        return session_dir_(id) / "meta.json";
    }

    auto conversation_path_(std::string_view id) const {
        return session_dir_(id) / "conversation.json";
    }

    static auto now_iso_() -> std::string {
        auto now = std::chrono::system_clock::now();
        auto days = std::chrono::floor<std::chrono::days>(now);
        std::chrono::year_month_day ymd{days};
        auto tod = std::chrono::floor<std::chrono::seconds>(now) - days;
        auto hh = std::chrono::duration_cast<std::chrono::hours>(tod);
        auto mm = std::chrono::duration_cast<std::chrono::minutes>(tod - hh);
        auto ss = tod - hh - mm;
        std::ostringstream oss;
        oss << int(ymd.year()) << "-"
            << std::setw(2) << std::setfill('0') << unsigned(ymd.month()) << "-"
            << std::setw(2) << std::setfill('0') << unsigned(ymd.day()) << "T"
            << std::setw(2) << std::setfill('0') << hh.count() << ":"
            << std::setw(2) << std::setfill('0') << mm.count() << ":"
            << std::setw(2) << std::setfill('0') << ss.count();
        return oss.str();
    }

    static auto generate_id_() -> std::string {
        static std::atomic<int> counter{0};
        auto now = std::chrono::system_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
        auto seq = counter.fetch_add(1);
        std::ostringstream oss;
        oss << std::hex << us << "-" << seq;
        return oss.str();
    }

public:
    explicit SessionManager(libs::agentfs::AgentFS& fs) : fs_(fs) {}

    auto create(std::string_view model, std::string_view title = "") -> SessionMeta {
        SessionMeta meta;
        meta.id = generate_id_();
        meta.model = std::string(model);
        meta.title = title.empty() ? "new session" : std::string(title);
        meta.created_at = now_iso_();
        meta.updated_at = meta.created_at;
        meta.turn_count = 0;

        namespace fs = std::filesystem;
        fs::create_directories(session_dir_(meta.id));

        save_meta_(meta);

        // Create empty conversation
        llm::Conversation conv;
        save_conversation(meta.id, conv);

        return meta;
    }

    auto load_meta(std::string_view id) const -> std::optional<SessionMeta> {
        auto j = libs::agentfs::AgentFS::read_json(meta_path_(id));
        if (j.is_null()) return std::nullopt;

        SessionMeta meta;
        meta.id = j.value("id", "");
        meta.title = j.value("title", "");
        meta.model = j.value("model", "");
        meta.created_at = j.value("created_at", "");
        meta.updated_at = j.value("updated_at", "");
        meta.turn_count = j.value("turn_count", 0);
        return meta;
    }

    auto load_conversation(std::string_view id) const -> llm::Conversation {
        namespace fs = std::filesystem;
        auto path = conversation_path_(id);
        if (!fs::exists(path)) return llm::Conversation{};
        return llm::Conversation::load(path.string());
    }

    void save_conversation(std::string_view id, const llm::Conversation& conv) {
        conv.save(conversation_path_(id).string());
    }

    void update_meta(SessionMeta& meta) {
        meta.updated_at = now_iso_();
        save_meta_(meta);
    }

    auto list() const -> std::vector<SessionMeta> {
        namespace fs = std::filesystem;
        std::vector<SessionMeta> result;
        std::error_code ec;
        for (auto& entry : fs::directory_iterator(fs_.sessions_path(), ec)) {
            if (!entry.is_directory()) continue;
            auto id = entry.path().filename().string();
            if (auto meta = load_meta(id)) {
                result.push_back(*meta);
            }
        }
        // Sort by updated_at descending
        std::sort(result.begin(), result.end(), [](const SessionMeta& a, const SessionMeta& b) {
            return a.updated_at > b.updated_at;
        });
        return result;
    }

    bool remove(std::string_view id) {
        namespace fs = std::filesystem;
        std::error_code ec;
        auto dir = session_dir_(id);
        if (!fs::exists(dir)) return false;
        fs::remove_all(dir, ec);
        return !ec;
    }

private:
    void save_meta_(const SessionMeta& meta) {
        nlohmann::json j;
        j["id"] = meta.id;
        j["title"] = meta.title;
        j["model"] = meta.model;
        j["created_at"] = meta.created_at;
        j["updated_at"] = meta.updated_at;
        j["turn_count"] = meta.turn_count;
        libs::agentfs::AgentFS::write_json(meta_path_(meta.id), j);
    }
};

} // namespace xlings::agent
