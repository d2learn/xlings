export module xlings.agent.prompt;

import std;
import mcpplibs.llmapi;
import xlings.agent.tool_bridge;
import xlings.libs.soul;
import xlings.libs.agent_skill;

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

// Build system prompt from Soul + ToolBridge + Skills
export auto build_system_prompt(
    const ToolBridge& bridge,
    const libs::soul::Soul& soul,
    const std::vector<libs::agent_skill::Skill>& skills = {}
) -> std::string {
    std::string prompt = "You are xlings agent";
    if (!soul.persona.empty()) {
        prompt += ", " + soul.persona;
    }
    if (!soul.scope.empty()) {
        prompt += ". Scope: " + soul.scope;
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

1. Call tools directly — don't describe intent first.
2. Keep replies as short and fast as possible, plain text.
3. Use parallel tool calls whenever possible — don't call tools one at a time if they are independent.
4. If a tool fails, explain the error to the user — don't retry the same call.
5. Use `search_memory` proactively when past context may help.
6. Reply in the user's language.)";

    // ── Skills ──
    if (!skills.empty()) {
        prompt += "\n\n## Skills\n";
        for (auto& skill : skills) {
            prompt += "\n### " + skill.name + "\n" + skill.prompt + "\n";
        }
    }

    return prompt;
}

} // namespace xlings::agent
