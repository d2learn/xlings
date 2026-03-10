export module xlings.agent.resource_cache;

import std;
import xlings.libs.json;

namespace xlings::agent {

export enum class ResourceKind {
    Package, Tool, Memory, Environment, Session, Skill, Index
};

export auto resource_kind_name(ResourceKind k) -> std::string_view {
    switch (k) {
        case ResourceKind::Package:     return "package";
        case ResourceKind::Tool:        return "tool";
        case ResourceKind::Memory:      return "memory";
        case ResourceKind::Environment: return "environment";
        case ResourceKind::Session:     return "session";
        case ResourceKind::Skill:       return "skill";
        case ResourceKind::Index:       return "index";
    }
    return "unknown";
}

export struct ResourceMeta {
    std::string id;
    ResourceKind kind;
    std::string summary;
    int ttl_seconds { 3600 };       // default 1 hour
    long long cached_at_ms { 0 };
};

export struct CacheEntry {
    ResourceMeta meta;
    nlohmann::json data;
    bool hit { false };
    int age_seconds { 0 };
};

export class ResourceCache {
    std::unordered_map<std::string, CacheEntry> store_;

    // Default TTL per kind (seconds)
    static int default_ttl_(ResourceKind k) {
        switch (k) {
            case ResourceKind::Package:     return 3600;
            case ResourceKind::Tool:        return 7200;
            case ResourceKind::Memory:      return 86400;
            case ResourceKind::Environment: return 1800;
            case ResourceKind::Session:     return 0;  // no expiry
            case ResourceKind::Skill:       return 86400;
            case ResourceKind::Index:       return 3600;
        }
        return 3600;
    }

    static auto now_ms_() -> long long {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    }

public:
    void put(std::string_view id, ResourceKind kind, std::string_view summary, const nlohmann::json& data) {
        CacheEntry entry;
        entry.meta.id = std::string(id);
        entry.meta.kind = kind;
        entry.meta.summary = std::string(summary);
        entry.meta.ttl_seconds = default_ttl_(kind);
        entry.meta.cached_at_ms = now_ms_();
        entry.data = data;
        entry.hit = false;
        entry.age_seconds = 0;
        store_[entry.meta.id] = std::move(entry);
    }

    auto get(std::string_view id) -> std::optional<CacheEntry> {
        auto it = store_.find(std::string(id));
        if (it == store_.end()) return std::nullopt;

        auto& entry = it->second;
        auto age_ms = now_ms_() - entry.meta.cached_at_ms;
        entry.age_seconds = static_cast<int>(age_ms / 1000);

        // Check TTL (0 = no expiry)
        if (entry.meta.ttl_seconds > 0 && entry.age_seconds > entry.meta.ttl_seconds) {
            store_.erase(it);
            return std::nullopt;
        }

        entry.hit = true;
        return entry;
    }

    auto search(std::string_view keyword, std::optional<ResourceKind> kind_filter = std::nullopt)
        -> std::vector<ResourceMeta> {
        std::vector<ResourceMeta> results;
        auto lower_kw = to_lower_(keyword);
        for (auto& [id, entry] : store_) {
            if (kind_filter && entry.meta.kind != *kind_filter) continue;
            auto lower_id = to_lower_(entry.meta.id);
            auto lower_summary = to_lower_(entry.meta.summary);
            if (lower_id.find(lower_kw) != std::string::npos ||
                lower_summary.find(lower_kw) != std::string::npos) {
                results.push_back(entry.meta);
            }
        }
        return results;
    }

    void invalidate(std::string_view id) {
        store_.erase(std::string(id));
    }

    void clear() { store_.clear(); }

    // Build a resource index summary (~500 tokens) for prompt injection
    auto build_resource_index() const -> std::string {
        std::map<ResourceKind, int> counts;
        for (auto& [id, entry] : store_) {
            counts[entry.meta.kind]++;
        }
        std::string summary = "Resource Index: ";
        bool first = true;
        for (auto& [kind, count] : counts) {
            if (!first) summary += ", ";
            summary += std::to_string(count) + " " + std::string(resource_kind_name(kind));
            if (count > 1) summary += "s";
            first = false;
        }
        return summary;
    }

    auto size() const -> std::size_t { return store_.size(); }

private:
    static auto to_lower_(std::string_view s) -> std::string {
        std::string r(s);
        for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return r;
    }
};

} // namespace xlings::agent
