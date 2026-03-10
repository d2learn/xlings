module;

#include <cstdio>
#if defined(__linux__) || defined(__APPLE__)
#include <sys/wait.h>
#endif

export module xlings.capabilities;

import std;
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
        auto json = nlohmann::json::parse(params, nullptr, false);
        std::vector<std::string> targets;
        if (json.contains("targets") && json["targets"].is_array()) {
            for (auto& t : json["targets"]) targets.push_back(t.get<std::string>());
        }
        bool yes = json.value("yes", false);
        bool noDeps = json.value("noDeps", false);
        bool global = json.value("global", false);
        return exit_result(xim::cmd_install(targets, yes, noDeps, stream, global));
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
        auto json = nlohmann::json::parse(params, nullptr, false);
        auto command = json.value("command", "");
        if (command.empty()) {
            return nlohmann::json({{"error", "empty command"}}).dump();
        }

        // Redirect stderr to a temp approach: use 2>&1 to merge
        std::string full_cmd = command + " 2>&1";
        std::string output;
        int exit_code = -1;

#ifdef _WIN32
        FILE* pipe = ::_popen(full_cmd.c_str(), "r");
#else
        FILE* pipe = ::popen(full_cmd.c_str(), "r");
#endif
        if (pipe) {
            char buffer[4096];
            while (auto n = std::fread(buffer, 1, sizeof(buffer), pipe)) {
                output.append(buffer, n);
            }
#ifdef _WIN32
            int status = ::_pclose(pipe);
#else
            int status = ::pclose(pipe);
#endif
#if defined(__linux__) || defined(__APPLE__)
            exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#else
            exit_code = status;
#endif
        }

        // Store in shared output buffer
        shared_output_buffer().set(output);

        // Truncate for LLM if too long
        constexpr size_t MAX_OUTPUT = 8000;
        bool truncated = false;
        std::string display_output = output;
        if (display_output.size() > MAX_OUTPUT) {
            display_output = display_output.substr(0, MAX_OUTPUT);
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
        return result.dump();
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
        return result.dump();
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
        return result.dump();
    }
};

export capability::Registry build_registry() {
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
    return reg;
}

} // namespace xlings::capabilities
