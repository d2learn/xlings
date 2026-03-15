export module xlings.capabilities;

import std;
import xlings.platform;
import xlings.runtime.event;
import xlings.runtime.event_stream;
import xlings.runtime.capability;
import xlings.libs.json;
import xlings.core.xim.commands;
import xlings.core.xvm.commands;
import xlings.core.config;
import xlings.runtime.cancellation;
import xlings.core.utf8;
import xlings.libs.semantic_memory;
import xlings.agent.context_manager;

namespace xlings::capabilities {

using capability::Capability;
using capability::CapabilitySpec;
using capability::Params;
using capability::Result;

Result exit_result(int code) {
    return nlohmann::json({{"exitCode", code}}).dump();
}

class SearchPackages : public Capability {
public:
    auto spec() const -> CapabilitySpec override {
        return {
            .name = "search_packages",
            .description = "Search available packages by keyword (fuzzy match). Use plain name without version or namespace (e.g. gc matches gcc, musl-gcc)",
            .inputSchema = R"JSON({"type":"object","properties":{"keyword":{"type":"string","description":"Keyword for fuzzy matching package names, e.g. gc, nod, rust"}},"required":["keyword"]})JSON",
            .outputSchema = R"({"type":"object","properties":{"exitCode":{"type":"integer"}}})",
            .destructive = false,
        };
    }
    auto execute(Params params, EventStream& stream) -> Result override {
        auto json = nlohmann::json::parse(params, nullptr, false);
        auto keyword = json.value("keyword", "");
        return exit_result(xim::cmd_search(keyword, stream));
    }
};

class InstallPackages : public Capability {
public:
    auto spec() const -> CapabilitySpec override {
        return {
            .name = "install_packages",
            .description = "Install one or more packages",
            .inputSchema = R"({"type":"object","properties":{"targets":{"type":"array","items":{"type":"string"},"description":"Format: name, name@version, or namespace:name@version"},"yes":{"type":"boolean","description":"Auto-confirm without prompting"},"noDeps":{"type":"boolean","description":"Skip dependency installation"},"global":{"type":"boolean","description":"Install to global scope"}},"required":["targets"]})",
            .outputSchema = R"({"type":"object","properties":{"exitCode":{"type":"integer"}}})",
            .destructive = true,
        };
    }
    auto execute(Params params, EventStream& stream) -> Result override {
        return execute(std::move(params), stream, nullptr);
    }
    auto execute(Params params, EventStream& stream, CancellationToken* cancel) -> Result override {
        auto json = nlohmann::json::parse(params, nullptr, false);
        std::vector<std::string> targets;
        if (json.contains("targets") && json["targets"].is_array()) {
            for (auto& t : json["targets"]) targets.push_back(t.get<std::string>());
        }
        bool yes = json.value("yes", false);
        bool noDeps = json.value("noDeps", false);
        bool global = json.value("global", false);
        return exit_result(xim::cmd_install(targets, yes, noDeps, stream, global, cancel));
    }
};

class RemovePackage : public Capability {
public:
    auto spec() const -> CapabilitySpec override {
        return {
            .name = "remove_package",
            .description = "Remove one package or one specific version. Only removes one target per call — to remove multiple versions, call multiple times",
            .inputSchema = R"({"type":"object","properties":{"target":{"type":"string","description":"Format: name, name@version, or namespace:name@version"}},"required":["target"]})",
            .outputSchema = R"({"type":"object","properties":{"exitCode":{"type":"integer"}}})",
            .destructive = true,
        };
    }
    auto execute(Params params, EventStream& stream) -> Result override {
        auto json = nlohmann::json::parse(params, nullptr, false);
        return exit_result(xim::cmd_remove(json.value("target", ""), stream));
    }
};

class UpdatePackages : public Capability {
public:
    auto spec() const -> CapabilitySpec override {
        return {
            .name = "update_packages",
            .description = "Update package index or a specific package",
            .inputSchema = R"({"type":"object","properties":{"target":{"type":"string","description":"Package name to update, or omit to update index"}}})",
            .outputSchema = R"({"type":"object","properties":{"exitCode":{"type":"integer"}}})",
            .destructive = true,
        };
    }
    auto execute(Params params, EventStream& stream) -> Result override {
        auto json = nlohmann::json::parse(params, nullptr, false);
        return exit_result(xim::cmd_update(json.value("target", ""), stream));
    }
};

class ListPackages : public Capability {
public:
    auto spec() const -> CapabilitySpec override {
        return {
            .name = "list_packages",
            .description = "List installed packages. Filter is a plain package name keyword (fuzzy match, not regex)",
            .inputSchema = R"JSON({"type":"object","properties":{"filter":{"type":"string","description":"Plain package name to filter, e.g. gcc, node"}}})JSON",
            .outputSchema = R"({"type":"object","properties":{"exitCode":{"type":"integer"}}})",
            .destructive = false,
        };
    }
    auto execute(Params params, EventStream& stream) -> Result override {
        auto json = nlohmann::json::parse(params, nullptr, false);
        return exit_result(xim::cmd_list(json.value("filter", ""), stream));
    }
};

class PackageInfo : public Capability {
public:
    auto spec() const -> CapabilitySpec override {
        return {
            .name = "package_info",
            .description = "Get detailed package info: versions, install status, metadata. Use namespace:name format (e.g. xim:gcc)",
            .inputSchema = R"JSON({"type":"object","properties":{"target":{"type":"string","description":"Package in namespace:name format, e.g. xim:gcc"}},"required":["target"]})JSON",
            .outputSchema = R"({"type":"object","properties":{"exitCode":{"type":"integer"}}})",
            .destructive = false,
        };
    }
    auto execute(Params params, EventStream& stream) -> Result override {
        auto json = nlohmann::json::parse(params, nullptr, false);
        return exit_result(xim::cmd_info(json.value("target", ""), stream));
    }
};

class ListVersions : public Capability {
public:
    auto spec() const -> CapabilitySpec override {
        return {
            .name = "list_installed_versions",
            .description = "List installed versions for a package and show which is active. Use plain name (e.g. gcc not xim:gcc)",
            .inputSchema = R"JSON({"type":"object","properties":{"target":{"type":"string","description":"Plain package name, e.g. gcc, node"}},"required":["target"]})JSON",
            .outputSchema = R"({"type":"object","properties":{"exitCode":{"type":"integer"}}})",
            .destructive = false,
        };
    }
    auto execute(Params params, EventStream& stream) -> Result override {
        auto json = nlohmann::json::parse(params, nullptr, false);
        return exit_result(xvm::cmd_list_versions(json.value("target", ""), stream));
    }
};

class UseVersion : public Capability {
public:
    auto spec() const -> CapabilitySpec override {
        return {
            .name = "use_version",
            .description = "Switch to a specific version of a package. Use plain name (e.g. gcc not xim:gcc)",
            .inputSchema = R"JSON({"type":"object","properties":{"target":{"type":"string","description":"Plain package name, e.g. gcc, node"},"version":{"type":"string","description":"Version to switch to, e.g. 15, 22"}},"required":["target","version"]})JSON",
            .outputSchema = R"({"type":"object","properties":{"exitCode":{"type":"integer"}}})",
            .destructive = true,
        };
    }
    auto execute(Params params, EventStream& stream) -> Result override {
        auto json = nlohmann::json::parse(params, nullptr, false);
        return exit_result(xvm::cmd_use(json.value("target", ""), json.value("version", ""), stream));
    }
};

class SystemStatus : public Capability {
public:
    auto spec() const -> CapabilitySpec override {
        return {
            .name = "system_status",
            .description = "Show xlings system configuration and status",
            .inputSchema = R"({"type":"object","properties":{}})",
            .outputSchema = R"({"type":"object","properties":{"exitCode":{"type":"integer"}}})",
            .destructive = false,
        };
    }
    auto execute(Params params, EventStream& stream) -> Result override {
        auto& p = Config::paths();
        nlohmann::json fields = nlohmann::json::array();
        auto addField = [&](const std::string& label, const std::string& value, bool hl = false) {
            fields.push_back({{"label", label}, {"value", value}, {"highlight", hl}});
        };
        addField("XLINGS_HOME", p.homeDir.string());
        addField("XLINGS_DATA", p.dataDir.string());
        addField("XLINGS_SUBOS", p.subosDir.string());
        addField("active subos", p.activeSubos, true);
        addField("bin", p.binDir.string());

        auto mirror = Config::mirror();
        if (!mirror.empty()) addField("mirror", mirror);
        auto lang = Config::lang();
        if (!lang.empty()) addField("lang", lang);

        nlohmann::json payload;
        payload["title"] = "xlings status";
        payload["fields"] = std::move(fields);
        stream.emit(DataEvent{"info_panel", payload.dump()});
        return exit_result(0);
    }
};

// ─── Memory & Context Tools ───

libs::semantic_memory::MemoryStore* shared_memory_store_{nullptr};
agent::ContextManager* shared_ctx_mgr_{nullptr};

class SaveMemory : public Capability {
public:
    auto spec() const -> CapabilitySpec override {
        return {
            .name = "save_memory",
            .description = "Save a piece of information to long-term memory for future sessions",
            .inputSchema = R"({"type":"object","properties":{"content":{"type":"string","description":"The information to remember"},"category":{"type":"string","enum":["fact","preference","experience"],"default":"fact"}},"required":["content"]})",
            .destructive = false,
        };
    }
    auto execute(Params params, EventStream& stream) -> Result override {
        if (!shared_memory_store_) return nlohmann::json({{"error", "memory store not initialized"}}).dump();
        auto json = nlohmann::json::parse(params, nullptr, false);
        auto content = json.value("content", "");
        auto category = json.value("category", "fact");
        if (content.empty()) return nlohmann::json({{"error", "content is required"}}).dump();
        auto id = shared_memory_store_->remember(content, category);
        return nlohmann::json({{"ok", true}, {"id", id}, {"category", category}}).dump();
    }
};

class SearchMemory : public Capability {
public:
    auto spec() const -> CapabilitySpec override {
        return {
            .name = "search_memory",
            .description = "Search long-term memory by keyword",
            .inputSchema = R"({"type":"object","properties":{"query":{"type":"string","description":"Search keyword"},"max_results":{"type":"integer","default":5}},"required":["query"]})",
            .destructive = false,
        };
    }
    auto execute(Params params, EventStream& stream) -> Result override {
        if (!shared_memory_store_) return nlohmann::json({{"error", "memory store not initialized"}}).dump();
        auto json = nlohmann::json::parse(params, nullptr, false);
        auto query = json.value("query", "");
        int max_results = json.value("max_results", 5);
        auto results = shared_memory_store_->recall_text(query, max_results);
        nlohmann::json arr = nlohmann::json::array();
        for (auto& r : results) {
            arr.push_back({{"id", r.entry.id}, {"content", r.entry.content},
                           {"category", r.entry.category}, {"score", r.score}});
        }
        return nlohmann::json({{"results", std::move(arr)}, {"count", static_cast<int>(results.size())}}).dump();
    }
};

class ForgetMemory : public Capability {
public:
    auto spec() const -> CapabilitySpec override {
        return {
            .name = "forget_memory",
            .description = "Delete a memory entry by ID",
            .inputSchema = R"({"type":"object","properties":{"id":{"type":"string","description":"Memory entry ID to delete"}},"required":["id"]})",
            .destructive = true,
        };
    }
    auto execute(Params params, EventStream& stream) -> Result override {
        if (!shared_memory_store_) return nlohmann::json({{"error", "memory store not initialized"}}).dump();
        auto json = nlohmann::json::parse(params, nullptr, false);
        auto id = json.value("id", "");
        bool ok = shared_memory_store_->forget(id);
        return nlohmann::json({{"ok", ok}}).dump();
    }
};

class ManageContext : public Capability {
public:
    auto spec() const -> CapabilitySpec override {
        return {
            .name = "manage_context",
            .description = "Manage conversation context: check stats or retrieve relevant history",
            .inputSchema = R"({"type":"object","properties":{"action":{"type":"string","enum":["status","retrieve"],"description":"status=show cache stats, retrieve=find relevant past turns"},"query":{"type":"string","description":"Search query for retrieve action"}},"required":["action"]})",
            .destructive = false,
        };
    }
    auto execute(Params params, EventStream& stream) -> Result override {
        if (!shared_ctx_mgr_) return nlohmann::json({{"error", "context manager not initialized"}}).dump();
        auto json = nlohmann::json::parse(params, nullptr, false);
        auto action = json.value("action", "");
        if (action == "status") {
            return nlohmann::json({
                {"l2_summaries", shared_ctx_mgr_->l2_count()},
                {"l3_keywords", shared_ctx_mgr_->l3_keyword_count()},
                {"evicted_tokens", shared_ctx_mgr_->total_evicted_tokens()},
                {"turn_count", shared_ctx_mgr_->next_turn_id()},
            }).dump();
        } else if (action == "retrieve") {
            auto query = json.value("query", "");
            if (query.empty()) return nlohmann::json({{"error", "query required for retrieve"}}).dump();
            auto results = shared_ctx_mgr_->retrieve_relevant(query, 5);
            nlohmann::json arr = nlohmann::json::array();
            for (auto* s : results) {
                arr.push_back({{"turn_id", s->turn_id}, {"user", s->user_brief},
                               {"assistant", s->assistant_brief}, {"tools", s->tool_names}});
            }
            return nlohmann::json({{"results", std::move(arr)}}).dump();
        }
        return nlohmann::json({{"error", "unknown action: " + action}}).dump();
    }
};

export capability::Registry build_registry(
    libs::semantic_memory::MemoryStore* memory_store = nullptr,
    agent::ContextManager* ctx_mgr = nullptr
) {
    capability::Registry reg;
    // xlings core capabilities
    reg.register_capability(std::make_unique<SearchPackages>());
    reg.register_capability(std::make_unique<InstallPackages>());
    reg.register_capability(std::make_unique<RemovePackage>());
    reg.register_capability(std::make_unique<UpdatePackages>());
    reg.register_capability(std::make_unique<ListPackages>());
    reg.register_capability(std::make_unique<PackageInfo>());
    reg.register_capability(std::make_unique<ListVersions>());
    reg.register_capability(std::make_unique<UseVersion>());
    reg.register_capability(std::make_unique<SystemStatus>());
    // Memory tools
    if (memory_store) {
        shared_memory_store_ = memory_store;
        reg.register_capability(std::make_unique<SaveMemory>());
        reg.register_capability(std::make_unique<SearchMemory>());
        reg.register_capability(std::make_unique<ForgetMemory>());
    }
    // Context management tool
    if (ctx_mgr) {
        shared_ctx_mgr_ = ctx_mgr;
        reg.register_capability(std::make_unique<ManageContext>());
    }
    return reg;
}

} // namespace xlings::capabilities
