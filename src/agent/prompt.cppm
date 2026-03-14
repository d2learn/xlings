export module xlings.agent.prompt;

import std;
import mcpplibs.llmapi;
import xlings.agent.tool_bridge;
import xlings.libs.soul;

namespace xlings::agent {

namespace llm = mcpplibs::llmapi;

// Convert ToolBridge ToolDefs to llmapi ToolDefs
export auto to_llmapi_tools(const ToolBridge& bridge) -> std::vector<llm::ToolDef> {
    std::vector<llm::ToolDef> tools;
    for (const auto& td : bridge.tool_definitions()) {
        tools.push_back(llm::ToolDef{
            .name = td.name,
            .description = td.description,
            .inputSchema = td.inputSchema,
        });
    }
    return tools;
}

// Build system prompt from Soul + ToolBridge
export auto build_system_prompt(
    const ToolBridge& bridge,
    const libs::soul::Soul& soul
) -> std::string {
    std::string prompt = "You are xlings agent";
    if (!soul.persona.empty()) {
        prompt += ", " + soul.persona;
    }
    prompt += ".\n";

    // ── Trust boundaries ──
    if (soul.trust_level == "readonly") {
        prompt += "\nIMPORTANT: You are in read-only mode. Do NOT modify any packages or system state.\n";
    }
    if (!soul.denied_capabilities.empty()) {
        prompt += "\nYou must NOT use these tools: ";
        for (std::size_t i = 0; i < soul.denied_capabilities.size(); ++i) {
            if (i > 0) prompt += ", ";
            prompt += soul.denied_capabilities[i];
        }
        prompt += ".\n";
    }
    if (!soul.forbidden_actions.empty()) {
        prompt += "\nForbidden actions: ";
        for (std::size_t i = 0; i < soul.forbidden_actions.size(); ++i) {
            if (i > 0) prompt += ", ";
            prompt += soul.forbidden_actions[i];
        }
        prompt += ".\n";
    }

    // ── Core rules ──
    prompt += R"(
## Rules

1. ALWAYS use built-in tools for package/version operations:
   - install → `install_packages`, remove → `remove_package`, update → `update_packages`
   - search → `search_packages`, info → `package_info`, list → `list_packages`
   - version switch → `use_version`, status → `system_status`

2. ALL tools provided to you are available and functional. NEVER say a tool is unavailable — just call it.

3. Keep responses short, plain text, no markdown.

4. Call tools directly — do NOT describe what you will do.

4. You have long-term memory via `search_memory` and `save_memory`. Use `search_memory` when past context may be relevant.)";

    return prompt;
}

} // namespace xlings::agent
