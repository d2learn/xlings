export module xlings.agent.package_tools;

import std;
import xlings.libs.json;
import xlings.agent.tool_bridge;
import xlings.agent.resource_cache;

namespace xlings::agent {

// Represents a tool definition from a package's agent_tools
export struct PackageTool {
    std::string package_name;
    std::string tool_name;
    std::string description;
    std::string input_schema;
    std::string handler_script;  // path to handler script
    int tier { 1 };              // default T1
};

// Scan installed packages for agent_tools definitions
// Package agent_tools are defined in xpkg as: agent_tools = { { name = "...", ... } }
// For now, we scan a JSON manifest at .agents/cache/package_tools.json
export class PackageToolRegistry {
    std::vector<PackageTool> tools_;

public:
    // Load from a manifest file (populated during install)
    void load_manifest(const std::filesystem::path& manifest_path) {
        namespace fs = std::filesystem;
        if (!fs::exists(manifest_path)) return;

        std::ifstream in(manifest_path);
        if (!in) return;
        auto j = nlohmann::json::parse(in, nullptr, false);
        if (j.is_discarded() || !j.is_array()) return;

        tools_.clear();
        for (auto& entry : j) {
            PackageTool tool;
            tool.package_name = entry.value("package", "");
            tool.tool_name = entry.value("name", "");
            tool.description = entry.value("description", "");
            tool.input_schema = entry.value("input_schema", "{}");
            tool.handler_script = entry.value("handler", "");
            tool.tier = entry.value("tier", 1);
            if (!tool.tool_name.empty()) {
                tools_.push_back(std::move(tool));
            }
        }
    }

    // Register a tool programmatically
    void register_tool(PackageTool tool) {
        tools_.push_back(std::move(tool));
    }

    // Convert to ToolDefs for the bridge
    auto tool_definitions() const -> std::vector<ToolDef> {
        std::vector<ToolDef> defs;
        for (auto& t : tools_) {
            defs.push_back({
                .name = t.tool_name,
                .description = "[pkg:" + t.package_name + "] " + t.description,
                .inputSchema = t.input_schema,
            });
        }
        return defs;
    }

    // Populate resource cache with package tool entries
    void populate_cache(ResourceCache& cache) const {
        for (auto& t : tools_) {
            nlohmann::json data;
            data["package"] = t.package_name;
            data["handler"] = t.handler_script;
            data["tier"] = t.tier;
            cache.put("pkg-tool:" + t.tool_name, ResourceKind::Tool,
                t.description, data);
        }
    }

    // Event handler: re-scan when packages change
    void on_package_change(const std::filesystem::path& manifest_path) {
        load_manifest(manifest_path);
    }

    auto all_tools() const -> const std::vector<PackageTool>& { return tools_; }
    auto size() const -> std::size_t { return tools_.size(); }
};

} // namespace xlings::agent
