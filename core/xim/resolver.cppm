export module xlings.xim.resolver;

import std;
import mcpplibs.xpkg;
import xlings.xim.types;
import xlings.xim.index;
import xlings.log;
import xlings.platform;

export namespace xlings::xim {

// Color for cycle detection (white/gray/black DFS)
enum class Color_ { White, Gray, Black };

// Parse a target like "gcc@15" into (name, version_hint)
std::pair<std::string, std::string> parse_target_(const std::string& target) {
    auto at = target.find('@');
    if (at == std::string::npos) return { target, "" };
    return { target.substr(0, at), target.substr(at + 1) };
}

// Resolve targets into a full install plan
// platform: "linux", "windows", "macosx"
std::expected<InstallPlan, std::string>
resolve(IndexManager& index,
        std::span<const std::string> targets,
        const std::string& platform) {

    InstallPlan plan;

    // Track visited nodes and their colors for cycle detection
    std::unordered_map<std::string, Color_> color;
    std::unordered_map<std::string, PlanNode> nodeMap;

    // Recursive DFS to expand dependencies
    std::function<bool(const std::string&, std::vector<std::string>&)> expand =
        [&](const std::string& target, std::vector<std::string>& path) -> bool {

        auto [baseName, versionHint] = parse_target_(target);

        // Resolve alias first
        auto resolved = index.resolve(baseName);

        // Try to find the entry
        auto* entry = index.find_entry(resolved);
        if (!entry) {
            // Try match_version
            auto match = index.match_version(resolved);
            if (!match) {
                plan.errors.push_back(
                    std::format("package '{}' not found in index", target));
                return false;
            }
            entry = index.find_entry(*match);
            resolved = *match;
        }

        if (!entry) {
            plan.errors.push_back(std::format("entry '{}' vanished", resolved));
            return false;
        }

        // Use the entry name as the canonical key
        std::string name = entry->name;

        // Already processed?
        auto it = color.find(name);
        if (it != color.end()) {
            if (it->second == Color_::Gray) {
                std::string cycle;
                for (auto& p : path) cycle += p + " -> ";
                cycle += name;
                plan.errors.push_back(
                    std::format("cyclic dependency detected: {}", cycle));
                return false;
            }
            return true;  // Black — already done
        }

        color[name] = Color_::Gray;
        path.push_back(name);

        // Build PlanNode
        PlanNode node;
        node.name = name;
        node.pkgFile = entry->path;
        node.alreadyInstalled = entry->installed;

        // Load full package data to get deps and version info
        auto pkg = index.load_package(name);
        if (pkg) {
            // Determine best version from xpm matrix
            if (!versionHint.empty()) {
                node.version = versionHint;
            } else {
                // Find "latest" ref or pick highest version
                auto platformIt = pkg->xpm.entries.find(platform);
                if (platformIt != pkg->xpm.entries.end()) {
                    auto& versions = platformIt->second;
                    auto latestIt = versions.find("latest");
                    if (latestIt != versions.end() && !latestIt->second.ref.empty()) {
                        node.version = latestIt->second.ref;
                    } else {
                        // Pick lexicographically greatest non-"latest" version
                        std::string best;
                        for (auto& [ver, _] : versions) {
                            if (ver != "latest" && ver > best) best = ver;
                        }
                        node.version = best;
                    }
                }
            }

            // Get deps for this platform
            auto depsIt = pkg->xpm.deps.find(platform);
            if (depsIt != pkg->xpm.deps.end()) {
                node.deps = depsIt->second;
                for (auto& dep : node.deps) {
                    if (!expand(dep, path)) {
                        // Continue collecting errors
                    }
                }
            }
        } else {
            log::warn("failed to load package {}: {}", name, pkg.error());
        }

        nodeMap[name] = std::move(node);
        color[name] = Color_::Black;
        path.pop_back();
        return true;
    };

    // Process all targets
    for (auto& target : targets) {
        std::vector<std::string> path;
        expand(target, path);
    }

    if (plan.has_errors()) {
        return plan;  // Return plan with errors
    }

    // Topological sort (DFS post-order)
    std::vector<std::string> topoOrder;
    std::unordered_set<std::string> visited;

    std::function<void(const std::string&)> topoVisit =
        [&](const std::string& name) {
        if (visited.count(name)) return;
        visited.insert(name);

        auto it = nodeMap.find(name);
        if (it == nodeMap.end()) return;

        for (auto& dep : it->second.deps) {
            // Resolve dep name to actual index entry name
            auto resolved = index.resolve(dep);
            auto match = index.match_version(resolved);
            std::string depKey = match ? *match : resolved;
            topoVisit(depKey);
        }
        topoOrder.push_back(name);
    };

    for (auto& [name, _] : nodeMap) {
        topoVisit(name);
    }

    // topoOrder is dependency-first (leaves first)
    for (auto& name : topoOrder) {
        auto it = nodeMap.find(name);
        if (it != nodeMap.end()) {
            plan.nodes.push_back(std::move(it->second));
        }
    }

    return plan;
}

} // namespace xlings::xim
