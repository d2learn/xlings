export module xlings.libs.agent_skill;

import std;
import xlings.libs.json;
import xlings.libs.agentfs;

namespace xlings::libs::agent_skill {

export struct Skill {
    std::string name;
    std::string description;
    std::string prompt;           // The skill prompt text
    std::vector<std::string> triggers;  // Keywords that activate this skill
    std::string source;           // "builtin" or "user"
};

export class SkillManager {
    agentfs::AgentFS& fs_;
    std::vector<Skill> skills_;

public:
    explicit SkillManager(agentfs::AgentFS& fs) : fs_(fs) {}

    // Load all skills from builtin/ and user/ directories
    void load_all() {
        skills_.clear();
        load_from_dir_(fs_.skills_path() / "builtin", "builtin");
        load_from_dir_(fs_.skills_path() / "user", "user");
    }

    // Match skills by keyword against triggers
    auto match(std::string_view input) const -> std::vector<const Skill*> {
        std::vector<const Skill*> matches;
        auto lower_input = to_lower_(input);
        for (auto& skill : skills_) {
            for (auto& trigger : skill.triggers) {
                if (lower_input.find(trigger) != std::string::npos) {
                    matches.push_back(&skill);
                    break;
                }
            }
        }
        return matches;
    }

    // Build combined prompt from matched skills
    auto build_prompt(std::span<const Skill* const> skills) const -> std::string {
        std::string prompt;
        for (auto* skill : skills) {
            prompt += "### Skill: " + skill->name + "\n";
            prompt += skill->prompt + "\n\n";
        }
        return prompt;
    }

    auto all_skills() const -> const std::vector<Skill>& { return skills_; }

    // Register a skill programmatically (for builtin skills)
    void register_skill(Skill skill) {
        skills_.push_back(std::move(skill));
    }

private:
    void load_from_dir_(const std::filesystem::path& dir, std::string_view source) {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(dir, ec)) return;
        for (auto& entry : fs::directory_iterator(dir, ec)) {
            if (entry.path().extension() != ".json") continue;
            auto j = agentfs::AgentFS::read_json(entry.path());
            if (j.is_null()) continue;
            Skill skill;
            skill.name = j.value("name", entry.path().stem().string());
            skill.description = j.value("description", "");
            skill.prompt = j.value("prompt", "");
            skill.source = std::string(source);
            if (j.contains("triggers") && j["triggers"].is_array()) {
                for (auto& t : j["triggers"]) {
                    skill.triggers.push_back(t.get<std::string>());
                }
            }
            skills_.push_back(std::move(skill));
        }
    }

    static auto to_lower_(std::string_view s) -> std::string {
        std::string result(s);
        for (auto& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return result;
    }
};

} // namespace xlings::libs::agent_skill
