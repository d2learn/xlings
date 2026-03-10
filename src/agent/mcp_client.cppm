export module xlings.agent.mcp_client;

import std;
import xlings.libs.json;
import xlings.libs.agentfs;
import xlings.agent.tool_bridge;

namespace xlings::agent::mcp {

// MCP server config (from .agents/mcps/*.json)
export struct McpServerConfig {
    std::string name;
    std::string command;          // e.g. "npx", "python"
    std::vector<std::string> args;
    std::map<std::string, std::string> env;
};

// External tool discovered from an MCP server
export struct ExternalTool {
    std::string server_name;
    std::string name;
    std::string description;
    std::string input_schema;
};

// Load MCP server configs from .agents/mcps/ directory
export auto load_mcp_configs(const std::filesystem::path& mcps_dir) -> std::vector<McpServerConfig> {
    namespace fs = std::filesystem;
    std::vector<McpServerConfig> configs;
    std::error_code ec;
    if (!fs::exists(mcps_dir, ec)) return configs;

    for (auto& entry : fs::directory_iterator(mcps_dir, ec)) {
        if (entry.path().extension() != ".json") continue;
        auto j = libs::agentfs::AgentFS::read_json(entry.path());
        if (j.is_null()) continue;

        McpServerConfig cfg;
        cfg.name = j.value("name", entry.path().stem().string());
        cfg.command = j.value("command", "");
        if (j.contains("args") && j["args"].is_array()) {
            for (auto& a : j["args"]) cfg.args.push_back(a.get<std::string>());
        }
        if (j.contains("env") && j["env"].is_object()) {
            for (auto it = j["env"].begin(); it != j["env"].end(); ++it) {
                cfg.env[it.key()] = it.value().get<std::string>();
            }
        }
        if (!cfg.command.empty()) {
            configs.push_back(std::move(cfg));
        }
    }
    return configs;
}

// MCP Client manages connections to external MCP servers
// Actual stdio subprocess spawning is deferred — this provides the config/tool framework
export class McpClient {
    std::vector<McpServerConfig> configs_;
    std::vector<ExternalTool> discovered_tools_;

public:
    void set_configs(std::vector<McpServerConfig> configs) {
        configs_ = std::move(configs);
    }

    // For testing: register discovered tools directly
    void register_discovered_tool(ExternalTool tool) {
        discovered_tools_.push_back(std::move(tool));
    }

    auto discovered_tools() const -> const std::vector<ExternalTool>& {
        return discovered_tools_;
    }

    // Convert discovered tools to ToolDefs
    auto tool_definitions() const -> std::vector<ToolDef> {
        std::vector<ToolDef> defs;
        for (auto& t : discovered_tools_) {
            defs.push_back({
                .name = "mcp:" + t.server_name + ":" + t.name,
                .description = "[mcp:" + t.server_name + "] " + t.description,
                .inputSchema = t.input_schema,
            });
        }
        return defs;
    }

    auto configs() const -> const std::vector<McpServerConfig>& { return configs_; }
};

} // namespace xlings::agent::mcp
