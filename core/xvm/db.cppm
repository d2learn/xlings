export module xlings.xvm.db;

import std;

import xlings.xvm.types;
import xlings.json;

export namespace xlings::xvm {

// Add or update a version entry in the database
void add_version(VersionDB& db,
                 const std::string& target,
                 const std::string& version,
                 const std::string& path,
                 const std::string& type = "program",
                 const std::string& filename = "") {
    auto& info = db[target];
    if (info.type.empty()) info.type = type;
    if (info.filename.empty() && !filename.empty()) info.filename = filename;

    VData vdata;
    vdata.path = path;
    info.versions[version] = std::move(vdata);
}

// Remove a version from the database
void remove_version(VersionDB& db,
                    const std::string& target,
                    const std::string& version) {
    auto it = db.find(target);
    if (it == db.end()) return;
    it->second.versions.erase(version);
    if (it->second.versions.empty()) {
        db.erase(it);
    }
}

// Fuzzy version match: "15" -> "15.1.0", "14.2" -> "14.2.0"
// Returns the best matching full version string, or empty if not found.
std::string match_version(const VersionDB& db,
                          const std::string& target,
                          const std::string& prefix) {
    auto it = db.find(target);
    if (it == db.end()) return {};

    auto& versions = it->second.versions;

    // Exact match first
    if (versions.contains(prefix)) return prefix;

    // Prefix match: split input by '.', match leading components
    auto split = [](const std::string& s) -> std::vector<std::string> {
        std::vector<std::string> parts;
        std::istringstream iss(s);
        std::string part;
        while (std::getline(iss, part, '.')) {
            parts.push_back(part);
        }
        return parts;
    };

    auto input_parts = split(prefix);
    std::vector<std::string> candidates;

    for (auto& [ver, _] : versions) {
        auto ver_parts = split(ver);
        if (ver_parts.size() < input_parts.size()) continue;

        bool match = true;
        for (std::size_t i = 0; i < input_parts.size(); ++i) {
            if (input_parts[i] != ver_parts[i]) {
                match = false;
                break;
            }
        }
        if (match) candidates.push_back(ver);
    }

    if (candidates.empty()) return {};

    // Sort descending by version components, return highest
    std::ranges::sort(candidates, [&](const std::string& a, const std::string& b) {
        auto pa = split(a);
        auto pb = split(b);
        for (std::size_t i = 0; i < std::min(pa.size(), pb.size()); ++i) {
            int na = 0, nb = 0;
            std::from_chars(pa[i].data(), pa[i].data() + pa[i].size(), na);
            std::from_chars(pb[i].data(), pb[i].data() + pb[i].size(), nb);
            if (na != nb) return na > nb;
        }
        return pa.size() > pb.size();
    });

    return candidates[0];
}

// Get the active version for a target from a workspace
std::string get_active_version(const Workspace& workspace,
                               const std::string& target) {
    auto it = workspace.find(target);
    return it != workspace.end() ? it->second : std::string{};
}

// Check if a target has any versions registered
bool has_target(const VersionDB& db, const std::string& target) {
    return db.contains(target);
}

// Check if a specific version exists
bool has_version(const VersionDB& db,
                 const std::string& target,
                 const std::string& version) {
    auto it = db.find(target);
    if (it == db.end()) return false;
    return it->second.versions.contains(version);
}

// Get all version strings for a target
std::vector<std::string> get_all_versions(const VersionDB& db,
                                          const std::string& target) {
    std::vector<std::string> result;
    auto it = db.find(target);
    if (it == db.end()) return result;
    for (auto& [ver, _] : it->second.versions) {
        result.push_back(ver);
    }
    return result;
}

// Get VData for a specific version
const VData* get_vdata(const VersionDB& db,
                       const std::string& target,
                       const std::string& version) {
    auto it = db.find(target);
    if (it == db.end()) return nullptr;
    auto vit = it->second.versions.find(version);
    if (vit == it->second.versions.end()) return nullptr;
    return &vit->second;
}

// Get VInfo for a target
const VInfo* get_vinfo(const VersionDB& db, const std::string& target) {
    auto it = db.find(target);
    if (it == db.end()) return nullptr;
    return &it->second;
}

// Get binding for a target (e.g., g++ -> g++-15 for gcc version 15.1.0)
std::string get_binding(const VersionDB& db,
                        const std::string& target,
                        const std::string& binding_name,
                        const std::string& version) {
    auto it = db.find(target);
    if (it == db.end()) return {};
    auto bit = it->second.bindings.find(binding_name);
    if (bit == it->second.bindings.end()) return {};
    auto vit = bit->second.find(version);
    if (vit == bit->second.end()) return {};
    return vit->second;
}

// Expand ${XLINGS_HOME} in a path string
std::string expand_path(const std::string& path, const std::string& xlings_home) {
    std::string result = path;
    const std::string placeholder = "${XLINGS_HOME}";
    std::size_t pos = 0;
    while ((pos = result.find(placeholder, pos)) != std::string::npos) {
        result.replace(pos, placeholder.size(), xlings_home);
        pos += xlings_home.size();
    }
    return result;
}

// --- JSON serialization ---

nlohmann::json vdata_to_json(const VData& vdata) {
    nlohmann::json j;
    j["path"] = vdata.path;
    if (!vdata.includedir.empty()) j["includedir"] = vdata.includedir;
    if (!vdata.libdir.empty()) j["libdir"] = vdata.libdir;
    if (!vdata.alias.empty()) {
        j["alias"] = vdata.alias;
    }
    if (!vdata.envs.empty()) {
        nlohmann::json envs_j = nlohmann::json::object();
        for (auto it = vdata.envs.begin(); it != vdata.envs.end(); ++it) {
            envs_j[it->first] = it->second;
        }
        j["envs"] = envs_j;
    }
    return j;
}

VData vdata_from_json(const nlohmann::json& j) {
    VData vdata;
    if (j.contains("path") && j["path"].is_string())
        vdata.path = j["path"].get<std::string>();
    if (j.contains("includedir") && j["includedir"].is_string())
        vdata.includedir = j["includedir"].get<std::string>();
    if (j.contains("libdir") && j["libdir"].is_string())
        vdata.libdir = j["libdir"].get<std::string>();
    if (j.contains("alias") && j["alias"].is_array()) {
        for (auto& a : j["alias"]) {
            if (a.is_string()) vdata.alias.push_back(a.get<std::string>());
        }
    }
    if (j.contains("envs") && j["envs"].is_object()) {
        auto& envs = j["envs"];
        for (auto it = envs.begin(); it != envs.end(); ++it) {
            if (it.value().is_string())
                vdata.envs[it.key()] = it.value().get<std::string>();
        }
    }
    return vdata;
}

nlohmann::json vinfo_to_json(const VInfo& info) {
    nlohmann::json j;
    if (!info.type.empty()) j["type"] = info.type;
    if (!info.filename.empty()) j["filename"] = info.filename;

    nlohmann::json vers = nlohmann::json::object();
    for (auto it = info.versions.begin(); it != info.versions.end(); ++it) {
        vers[it->first] = vdata_to_json(it->second);
    }
    j["versions"] = vers;

    if (!info.bindings.empty()) {
        nlohmann::json binds = nlohmann::json::object();
        for (auto it = info.bindings.begin(); it != info.bindings.end(); ++it) {
            nlohmann::json vermap_j = nlohmann::json::object();
            for (auto vit = it->second.begin(); vit != it->second.end(); ++vit) {
                vermap_j[vit->first] = vit->second;
            }
            binds[it->first] = vermap_j;
        }
        j["bindings"] = binds;
    }

    return j;
}

VInfo vinfo_from_json(const nlohmann::json& j) {
    VInfo info;
    if (j.contains("type") && j["type"].is_string())
        info.type = j["type"].get<std::string>();
    if (j.contains("filename") && j["filename"].is_string())
        info.filename = j["filename"].get<std::string>();
    if (j.contains("versions") && j["versions"].is_object()) {
        auto& vers = j["versions"];
        for (auto it = vers.begin(); it != vers.end(); ++it) {
            info.versions[it.key()] = vdata_from_json(it.value());
        }
    }
    if (j.contains("bindings") && j["bindings"].is_object()) {
        auto& binds = j["bindings"];
        for (auto it = binds.begin(); it != binds.end(); ++it) {
            if (it.value().is_object()) {
                std::map<std::string, std::string> vermap;
                for (auto vit = it.value().begin(); vit != it.value().end(); ++vit) {
                    if (vit.value().is_string())
                        vermap[vit.key()] = vit.value().get<std::string>();
                }
                info.bindings[it.key()] = std::move(vermap);
            }
        }
    }
    return info;
}

nlohmann::json versions_to_json(const VersionDB& db) {
    nlohmann::json j = nlohmann::json::object();
    for (auto it = db.begin(); it != db.end(); ++it) {
        j[it->first] = vinfo_to_json(it->second);
    }
    return j;
}

VersionDB versions_from_json(const nlohmann::json& j) {
    VersionDB db;
    if (!j.is_object()) return db;
    for (auto it = j.begin(); it != j.end(); ++it) {
        db[it.key()] = vinfo_from_json(it.value());
    }
    return db;
}

Workspace workspace_from_json(const nlohmann::json& j) {
    Workspace ws;
    if (!j.is_object()) return ws;
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (it.value().is_string())
            ws[it.key()] = it.value().get<std::string>();
    }
    return ws;
}

nlohmann::json workspace_to_json(const Workspace& ws) {
    nlohmann::json j = nlohmann::json::object();
    for (auto it = ws.begin(); it != ws.end(); ++it) {
        j[it->first] = it->second;
    }
    return j;
}

} // namespace xlings::xvm
