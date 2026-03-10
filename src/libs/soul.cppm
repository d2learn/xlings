export module xlings.libs.soul;

import std;
import xlings.libs.json;
import xlings.libs.agentfs;

namespace xlings::libs::soul {

export struct Soul {
    std::string id;
    std::string persona;           // e.g. "xlings package management assistant"
    std::string trust_level;       // "auto" | "confirm" | "readonly"
    std::vector<std::string> allowed_capabilities;  // ["*"] = all
    std::vector<std::string> denied_capabilities;
    std::vector<std::string> forbidden_actions;
    std::string scope;             // "system" | "project"
};

export class SoulManager {
    agentfs::AgentFS& fs_;

public:
    explicit SoulManager(agentfs::AgentFS& fs) : fs_(fs) {}

    auto create_default() -> Soul {
        Soul s;
        s.id = generate_id_();
        s.persona = "xlings package management assistant";
        s.trust_level = "confirm";
        s.allowed_capabilities = {"*"};
        s.scope = "system";

        save_(s);
        return s;
    }

    auto load() -> std::optional<Soul> {
        auto j = agentfs::AgentFS::read_json(fs_.soul_path());
        if (j.is_null() || !j.contains("id")) return std::nullopt;

        Soul s;
        s.id = j.value("id", "");
        s.persona = j.value("persona", "");
        s.trust_level = j.value("trust_level", "confirm");
        if (j.contains("allowed_capabilities") && j["allowed_capabilities"].is_array()) {
            for (auto& v : j["allowed_capabilities"]) s.allowed_capabilities.push_back(v.get<std::string>());
        }
        if (j.contains("denied_capabilities") && j["denied_capabilities"].is_array()) {
            for (auto& v : j["denied_capabilities"]) s.denied_capabilities.push_back(v.get<std::string>());
        }
        if (j.contains("forbidden_actions") && j["forbidden_actions"].is_array()) {
            for (auto& v : j["forbidden_actions"]) s.forbidden_actions.push_back(v.get<std::string>());
        }
        s.scope = j.value("scope", "system");
        return s;
    }

    auto load_or_create() -> Soul {
        auto s = load();
        if (s) return *s;
        return create_default();
    }

    bool is_capability_allowed(const Soul& s, std::string_view cap_name) const {
        // Check denied first
        for (auto& d : s.denied_capabilities) {
            if (d == cap_name) return false;
        }
        // Check allowed
        for (auto& a : s.allowed_capabilities) {
            if (a == "*" || a == cap_name) return true;
        }
        return false;
    }

    bool is_action_forbidden(const Soul& s, std::string_view action) const {
        for (auto& f : s.forbidden_actions) {
            if (action.find(f) != std::string_view::npos) return true;
        }
        return false;
    }

private:
    void save_(const Soul& s) {
        nlohmann::json j;
        j["id"] = s.id;
        j["persona"] = s.persona;
        j["trust_level"] = s.trust_level;
        j["allowed_capabilities"] = s.allowed_capabilities;
        j["denied_capabilities"] = s.denied_capabilities;
        j["forbidden_actions"] = s.forbidden_actions;
        j["scope"] = s.scope;
        agentfs::AgentFS::write_json(fs_.soul_path(), j);
    }

    static auto generate_id_() -> std::string {
        // Simple timestamp-based ID
        auto now = std::chrono::system_clock::now();
        auto epoch = now.time_since_epoch();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count();
        std::ostringstream oss;
        oss << "soul-" << std::hex << ms;
        return oss.str();
    }
};

} // namespace xlings::libs::soul
