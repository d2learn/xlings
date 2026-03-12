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
import xlings.core.log;
import xlings.platform;
import xlings.agent.output_buffer;
import xlings.core.utf8;
import xlings.runtime.cancellation;
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

// Shared output buffer for RunCommand / ViewOutput / SearchContent
agent::OutputBuffer& shared_output_buffer() {
    static agent::OutputBuffer buf;
    return buf;
}

libs::semantic_memory::MemoryStore* shared_memory_store_{nullptr};
agent::ContextManager* shared_ctx_mgr_{nullptr};

class SearchPackages : public Capability {
public:
    auto spec() const -> CapabilitySpec override {
        return {
            .name = "search_packages",
            .description = "Search for packages by keyword",
            .inputSchema = R"({"type":"object","properties":{"keyword":{"type":"string"}},"required":["keyword"]})",
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
            .inputSchema = R"({"type":"object","properties":{"targets":{"type":"array","items":{"type":"string"}},"yes":{"type":"boolean"},"noDeps":{"type":"boolean"},"global":{"type":"boolean"}},"required":["targets"]})",
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
            .description = "Remove an installed package",
            .inputSchema = R"({"type":"object","properties":{"target":{"type":"string"}},"required":["target"]})",
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
            .inputSchema = R"({"type":"object","properties":{"target":{"type":"string"}}})",
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
            .description = "List installed packages, optionally filtered",
            .inputSchema = R"({"type":"object","properties":{"filter":{"type":"string"}}})",
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
            .description = "Show detailed information about a package",
            .inputSchema = R"({"type":"object","properties":{"target":{"type":"string"}},"required":["target"]})",
            .outputSchema = R"({"type":"object","properties":{"exitCode":{"type":"integer"}}})",
            .destructive = false,
        };
    }
    auto execute(Params params, EventStream& stream) -> Result override {
        auto json = nlohmann::json::parse(params, nullptr, false);
        return exit_result(xim::cmd_info(json.value("target", ""), stream));
    }
};

class UseVersion : public Capability {
public:
    auto spec() const -> CapabilitySpec override {
        return {
            .name = "use_version",
            .description = "Switch tool version or list available versions",
            .inputSchema = R"({"type":"object","properties":{"target":{"type":"string"},"version":{"type":"string"}},"required":["target"]})",
            .outputSchema = R"({"type":"object","properties":{"exitCode":{"type":"integer"}}})",
            .destructive = true,
        };
    }
    auto execute(Params params, EventStream& stream) -> Result override {
        auto json = nlohmann::json::parse(params, nullptr, false);
        auto target = json.value("target", "");
        auto version = json.value("version", "");
        if (version.empty()) {
            return exit_result(xvm::cmd_list_versions(target, stream));
        }
        return exit_result(xvm::cmd_use(target, version, stream));
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

// ─── Phase 4: Agent Extension Tools ───

class SetLogLevel : public Capability {
public:
    auto spec() const -> CapabilitySpec override {
        return {
            .name = "set_log_level",
            .description = "Switch log verbosity: debug, info, warn, error",
            .inputSchema = R"({"type":"object","properties":{"level":{"type":"string","enum":["debug","info","warn","error"]}},"required":["level"]})",
            .outputSchema = R"({"type":"object","properties":{"success":{"type":"boolean"},"level":{"type":"string"}}})",
            .destructive = false,
        };
    }
    auto execute(Params params, EventStream& stream) -> Result override {
        auto json = nlohmann::json::parse(params, nullptr, false);
        auto level = json.value("level", "info");
        log::set_level(level);
        return nlohmann::json({{"success", true}, {"level", level}}).dump();
    }
};

class RunCommand : public Capability {
public:
    auto spec() const -> CapabilitySpec override {
        return {
            .name = "run_command",
            .description = "Execute a shell command and capture stdout/stderr as text",
            .inputSchema = R"({"type":"object","properties":{"command":{"type":"string"},"timeout_ms":{"type":"integer","default":30000}},"required":["command"]})",
            .outputSchema = R"({"type":"object","properties":{"exitCode":{"type":"integer"},"stdout":{"type":"string"},"stderr":{"type":"string"}}})",
            .destructive = true,
        };
    }
    auto execute(Params params, EventStream& stream) -> Result override {
        return execute(std::move(params), stream, nullptr);
    }

    auto execute(Params params, EventStream& stream, CancellationToken* cancel) -> Result override {
        auto json = nlohmann::json::parse(params, nullptr, false);
        auto command = json.value("command", "");
        if (command.empty()) {
            return nlohmann::json({{"error", "empty command"}}).dump();
        }

        int exit_code;
        std::string output;
        int timeout_ms = json.value("timeout_ms", 30000);

        if (cancel) {
            // Cancellable: use spawn + wait_or_kill
            auto h = xlings::platform::spawn_command(command);
            if (h.pid <= 0) {
                return nlohmann::json({{"error", "failed to spawn command"}}).dump();
            }
            auto [code, out] = xlings::platform::wait_or_kill(
                h, cancel, std::chrono::milliseconds{timeout_ms});
            exit_code = code;
            output = std::move(out);
        } else {
            // Non-cancellable fallback
            auto [code, out] = xlings::platform::run_command_capture(command);
            exit_code = code;
            output = std::move(out);
        }

        // Store in shared output buffer
        shared_output_buffer().set(output);

        // Truncate for LLM if too long
        constexpr std::size_t MAX_OUTPUT = 8000;
        bool truncated = false;
        std::string display_output = output;
        if (display_output.size() > MAX_OUTPUT) {
            display_output = utf8::safe_truncate(output, MAX_OUTPUT, "...[truncated]");
            truncated = true;
        }

        nlohmann::json result;
        result["exitCode"] = exit_code;
        result["stdout"] = std::move(display_output);
        if (truncated) {
            result["truncated"] = true;
            result["totalLines"] = shared_output_buffer().line_count();
            result["hint"] = "Output truncated. Use view_output to see specific line ranges.";
        }
        return utf8::safe_dump(result);
    }
};

class ViewOutput : public Capability {
public:
    auto spec() const -> CapabilitySpec override {
        return {
            .name = "view_output",
            .description = "View a range of lines from the last command output",
            .inputSchema = R"({"type":"object","properties":{"start_line":{"type":"integer","default":1},"end_line":{"type":"integer","default":50},"search":{"type":"string","description":"Optional: filter lines containing this text"}}})",
            .outputSchema = R"({"type":"object","properties":{"content":{"type":"string"},"totalLines":{"type":"integer"}}})",
            .destructive = false,
        };
    }
    auto execute(Params params, EventStream& stream) -> Result override {
        auto json = nlohmann::json::parse(params, nullptr, false);
        auto& buf = shared_output_buffer();
        auto total = buf.line_count();

        nlohmann::json result;
        result["totalLines"] = total;

        if (json.contains("search") && json["search"].is_string()) {
            auto pattern = json["search"].get<std::string>();
            int max_results = json.value("max_results", 20);
            result["content"] = buf.search(pattern, max_results);
            result["filter"] = pattern;
        } else {
            int start = json.value("start_line", 1);
            int end = json.value("end_line", 50);
            result["content"] = buf.lines(start, end);
            result["range"] = std::format("{}-{}", start, end);
        }
        return utf8::safe_dump(result);
    }
};

class SearchContent : public Capability {
public:
    auto spec() const -> CapabilitySpec override {
        return {
            .name = "search_content",
            .description = "Search for text patterns in files or last command output",
            .inputSchema = R"JSON({"type":"object","properties":{"pattern":{"type":"string"},"source":{"type":"string","enum":["last_output","file"],"default":"last_output"},"path":{"type":"string","description":"File path when source is file"},"max_results":{"type":"integer","default":20}},"required":["pattern"]})JSON",
            .outputSchema = R"({"type":"object","properties":{"matches":{"type":"string"},"count":{"type":"integer"}}})",
            .destructive = false,
        };
    }
    auto execute(Params params, EventStream& stream) -> Result override {
        auto json = nlohmann::json::parse(params, nullptr, false);
        auto pattern = json.value("pattern", "");
        auto source = json.value("source", "last_output");
        int max_results = json.value("max_results", 20);

        nlohmann::json result;

        if (source == "file") {
            auto path = json.value("path", "");
            if (path.empty()) {
                return nlohmann::json({{"error", "path required when source=file"}}).dump();
            }
            std::ifstream file(path);
            if (!file) {
                return nlohmann::json({{"error", "cannot open file: " + path}}).dump();
            }
            std::string matches;
            int count = 0;
            std::string line;
            int line_num = 0;
            while (std::getline(file, line) && count < max_results) {
                ++line_num;
                if (line.find(pattern) != std::string::npos) {
                    matches += std::to_string(line_num) + ": " + line + "\n";
                    ++count;
                }
            }
            result["matches"] = std::move(matches);
            result["count"] = count;
        } else {
            // Search last output buffer
            auto matches = shared_output_buffer().search(pattern, max_results);
            int count = 0;
            for (char c : matches) {
                if (c == '\n') ++count;
            }
            result["matches"] = std::move(matches);
            result["count"] = count;
        }
        return utf8::safe_dump(result);
    }
};

// ─── Phase 5: Memory & Context Tools ───

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
    reg.register_capability(std::make_unique<SearchPackages>());
    reg.register_capability(std::make_unique<InstallPackages>());
    reg.register_capability(std::make_unique<RemovePackage>());
    reg.register_capability(std::make_unique<UpdatePackages>());
    reg.register_capability(std::make_unique<ListPackages>());
    reg.register_capability(std::make_unique<PackageInfo>());
    reg.register_capability(std::make_unique<UseVersion>());
    reg.register_capability(std::make_unique<SystemStatus>());
    // Agent extension tools
    reg.register_capability(std::make_unique<SetLogLevel>());
    reg.register_capability(std::make_unique<RunCommand>());
    reg.register_capability(std::make_unique<ViewOutput>());
    reg.register_capability(std::make_unique<SearchContent>());
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
