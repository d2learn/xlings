export module xlings.agent.resource_tools;

import std;
import xlings.libs.json;
import xlings.agent.resource_cache;

namespace xlings::agent {

// T0 built-in tools for the agent to search/load resources from the cache

export auto tool_search_resources(ResourceCache& cache, std::string_view arguments) -> std::string {
    auto args = nlohmann::json::parse(arguments, nullptr, false);
    if (args.is_discarded()) return R"({"error":"invalid JSON arguments"})";

    auto keyword = args.value("keyword", "");
    if (keyword.empty()) return R"({"error":"keyword is required"})";

    std::optional<ResourceKind> kind_filter;
    if (args.contains("kind") && args["kind"].is_string()) {
        auto k = args["kind"].get<std::string>();
        if (k == "package")     kind_filter = ResourceKind::Package;
        else if (k == "tool")   kind_filter = ResourceKind::Tool;
        else if (k == "memory") kind_filter = ResourceKind::Memory;
        else if (k == "skill")  kind_filter = ResourceKind::Skill;
        else if (k == "index")  kind_filter = ResourceKind::Index;
    }

    auto results = cache.search(keyword, kind_filter);

    nlohmann::json out = nlohmann::json::array();
    for (auto& meta : results) {
        out.push_back({
            {"id", meta.id},
            {"kind", std::string(resource_kind_name(meta.kind))},
            {"summary", meta.summary},
        });
    }
    return out.dump();
}

export auto tool_load_resource(ResourceCache& cache, std::string_view arguments) -> std::string {
    auto args = nlohmann::json::parse(arguments, nullptr, false);
    if (args.is_discarded()) return R"({"error":"invalid JSON arguments"})";

    auto id = args.value("id", "");
    if (id.empty()) return R"({"error":"id is required"})";

    auto entry = cache.get(id);
    if (!entry) return R"({"error":"resource not found or expired"})";

    nlohmann::json out;
    out["id"] = entry->meta.id;
    out["kind"] = std::string(resource_kind_name(entry->meta.kind));
    out["summary"] = entry->meta.summary;
    out["data"] = entry->data;
    out["cache_hit"] = entry->hit;
    out["age_seconds"] = entry->age_seconds;
    return out.dump();
}

// Tool definitions for registration
export auto search_resources_tool_def() -> std::pair<std::string, std::string> {
    return {
        "search_resources",
        R"JSON({"type":"object","properties":{"keyword":{"type":"string","description":"Search keyword"},"kind":{"type":"string","description":"Resource kind filter (package/tool/memory/skill/index)"}},"required":["keyword"]})JSON"
    };
}

export auto load_resource_tool_def() -> std::pair<std::string, std::string> {
    return {
        "load_resource",
        R"JSON({"type":"object","properties":{"id":{"type":"string","description":"Resource ID to load"}},"required":["id"]})JSON"
    };
}

} // namespace xlings::agent
