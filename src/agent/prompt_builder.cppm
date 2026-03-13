export module xlings.agent.prompt_builder;

import std;
import xlings.agent.tool_bridge;
import xlings.libs.soul;
import xlings.libs.agentfs;

namespace xlings::agent {

// 4-layer prompt assembly:
// L1: Core prompt + Soul persona + boundaries + tool list
// L2: Active skills prompt
// L3: Dynamic context (resource index, memory, project context)
// L4: User prompt (.agents/prompt/user.md)

export class PromptBuilder {
    const ToolBridge& bridge_;
    const libs::soul::Soul& soul_;
    libs::agentfs::AgentFS& fs_;

    std::vector<std::string> skill_prompts_;
    std::string context_summary_;

public:
    PromptBuilder(const ToolBridge& bridge, const libs::soul::Soul& soul, libs::agentfs::AgentFS& fs)
        : bridge_(bridge), soul_(soul), fs_(fs) {}

    // Add a skill prompt (L2)
    void add_skill(std::string_view prompt) {
        skill_prompts_.emplace_back(prompt);
    }

    // Set dynamic context summary (L3)
    void set_context(std::string_view summary) {
        context_summary_ = std::string(summary);
    }

    // Build the full system prompt
    auto build() const -> std::string {
        std::string result;

        // L1: Core + Soul + Tools
        result += build_core_layer_();

        // L2: Active skills
        if (!skill_prompts_.empty()) {
            result += "\n## Active Skills\n\n";
            for (auto& sp : skill_prompts_) {
                result += sp + "\n\n";
            }
        }

        // L3: Dynamic context
        if (!context_summary_.empty()) {
            result += "\n## Current Context\n\n" + context_summary_ + "\n";
        }

        // L4: User prompt from file
        auto user_prompt = load_user_prompt_();
        if (!user_prompt.empty()) {
            result += "\n## User Instructions\n\n" + user_prompt + "\n";
        }

        return result;
    }

private:
    auto build_core_layer_() const -> std::string {
        std::string prompt;
        prompt += "You are xlings-agent";
        if (!soul_.persona.empty()) {
            prompt += ", " + soul_.persona;
        }
        prompt += ".\n\n";

        // Boundaries from soul
        if (soul_.trust_level == "readonly") {
            prompt += "IMPORTANT: You are in read-only mode. Do not modify any packages or system state.\n\n";
        }
        if (!soul_.denied_capabilities.empty()) {
            prompt += "You must NOT use these capabilities: ";
            for (std::size_t i = 0; i < soul_.denied_capabilities.size(); ++i) {
                if (i > 0) prompt += ", ";
                prompt += soul_.denied_capabilities[i];
            }
            prompt += ".\n\n";
        }
        if (!soul_.forbidden_actions.empty()) {
            prompt += "Forbidden actions: ";
            for (std::size_t i = 0; i < soul_.forbidden_actions.size(); ++i) {
                if (i > 0) prompt += ", ";
                prompt += soul_.forbidden_actions[i];
            }
            prompt += ".\n\n";
        }

        // Tool list
        prompt += "You have access to the following tools:\n\n";
        for (const auto& td : bridge_.tool_definitions()) {
            prompt += "- **" + td.name + "**: " + td.description + "\n";
        }

        prompt += R"(
When the user asks you to install, search, or manage packages, use the appropriate tools.
Always explain what you're about to do before calling a tool.
After a tool completes, summarize the result for the user.

## Lua Execution Engine

You can use the `execute_lua` tool to run multi-step operations in a single call.

### Available APIs

```lua
-- Package management
pkg.search(query)            -- returns {found=bool, packages=[{name, version, description}]}
pkg.install(name [,version]) -- returns {success=bool, message=string}
pkg.remove(name)             -- returns {success=bool, message=string}
pkg.list()                   -- returns {packages=[{name, version, status}]}
pkg.info(name)               -- returns {name, version, description, installed=bool, ...}
pkg.update(name)             -- returns {success=bool, message=string}

-- System
sys.status()                 -- returns {os, arch, hostname, ...}
sys.run(command)             -- returns {exit_code=int, stdout=string, stderr=string}

-- Version management
ver.use(name, version)       -- returns {success=bool, message=string}
```

### When to use execute_lua vs individual tools
- **Use execute_lua** when: multiple operations with conditional logic, batch operations, data aggregation
- **Use individual tools** when: single operation, need streaming feedback, destructive operation requiring confirmation

### Example
```lua
-- Check and install multiple packages
local results = {}
for _, name in ipairs({"vim", "git", "curl"}) do
    local info = pkg.info(name)
    if not info.installed then
        results[name] = pkg.install(name)
    else
        results[name] = {success=true, message="already installed"}
    end
end
return results
```
)";
        return prompt;
    }

    auto load_user_prompt_() const -> std::string {
        namespace fs = std::filesystem;
        auto path = fs_.root() / "prompt" / "user.md";
        if (!fs::exists(path)) return "";
        std::ifstream in(path);
        if (!in) return "";
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
};

} // namespace xlings::agent
