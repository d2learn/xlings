module;

#include <cstdio>

export module xlings.capabilities;

import std;
import xlings.runtime.event;
import xlings.runtime.event_stream;
import xlings.runtime.capability;
import xlings.libs.json;
import xlings.core.xim.commands;
import xlings.core.xvm.commands;
import xlings.core.config;
import xlings.platform;

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
    return reg;
}

} // namespace xlings::capabilities
