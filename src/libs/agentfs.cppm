export module xlings.libs.agentfs;

import std;
import xlings.libs.json;
import xlings.libs.flock;

namespace xlings::libs::agentfs {

// Directory tree under .agents/
static constexpr std::string_view SUBDIRS[] = {
    "config",
    "soul",
    "sessions",
    "journal",
    "cache",
    "cache/embeddings",
    "mcps",
    "skills",
    "skills/builtin",
    "skills/user",
    "memory",
    "memory/entries",
    "tmp",
};

export class AgentFS {
    std::filesystem::path root_;

public:
    explicit AgentFS(std::filesystem::path root) : root_(std::move(root)) {}

    // Create .agents/ directory tree if not present
    void ensure_initialized() {
        namespace fs = std::filesystem;
        fs::create_directories(root_);
        for (auto d : SUBDIRS) {
            fs::create_directories(root_ / d);
        }
        // Write version.json if missing
        auto ver = root_ / "version.json";
        if (!fs::exists(ver)) {
            write_json(ver, {{"schema_version", 1}});
        }
    }

    // Path accessors
    auto root()            const -> const std::filesystem::path& { return root_; }
    auto soul_path()       const { return root_ / "soul" / "soul.seed.json"; }
    auto config_path()     const { return root_ / "config"; }
    auto llm_config_path() const { return root_ / "config" / "llm.json"; }
    auto sessions_path()   const { return root_ / "sessions"; }
    auto journal_path()    const { return root_ / "journal"; }
    auto cache_path()      const { return root_ / "cache"; }
    auto mcps_path()       const { return root_ / "mcps"; }
    auto skills_path()     const { return root_ / "skills"; }
    auto memory_path()     const { return root_ / "memory"; }
    auto tmp_path()        const { return root_ / "tmp"; }

    // Read a JSON file, returns null json on failure
    static auto read_json(const std::filesystem::path& path) -> nlohmann::json {
        namespace fs = std::filesystem;
        if (!fs::exists(path)) return nlohmann::json{};
        std::ifstream in(path);
        if (!in) return nlohmann::json{};
        auto j = nlohmann::json::parse(in, nullptr, false);
        return j.is_discarded() ? nlohmann::json{} : j;
    }

    // Write JSON atomically (write to .tmp, rename)
    static void write_json(const std::filesystem::path& path, const nlohmann::json& j) {
        namespace fs = std::filesystem;
        auto tmp = path;
        tmp += ".tmp";
        {
            std::ofstream out(tmp);
            out << j.dump(2);
        }
        fs::rename(tmp, path);
    }

    // Append a JSON object as a JSONL line (with file lock)
    static void append_jsonl(const std::filesystem::path& path, const nlohmann::json& j) {
        flock::FileLock lk(path.string() + ".lock");
        std::ofstream out(path, std::ios::app);
        out << j.dump(-1) << '\n';
    }

    // Read all JSONL lines from a file
    static auto read_jsonl(const std::filesystem::path& path) -> std::vector<nlohmann::json> {
        std::vector<nlohmann::json> result;
        std::ifstream in(path);
        if (!in) return result;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            auto j = nlohmann::json::parse(line, nullptr, false);
            if (!j.is_discarded()) result.push_back(std::move(j));
        }
        return result;
    }

    // Clean tmp/ directory
    void clean_tmp() const {
        namespace fs = std::filesystem;
        std::error_code ec;
        for (auto& entry : fs::directory_iterator(tmp_path(), ec)) {
            fs::remove_all(entry.path(), ec);
        }
    }

    // Clean cache/ directory
    void clean_cache() const {
        namespace fs = std::filesystem;
        std::error_code ec;
        for (auto& entry : fs::directory_iterator(cache_path(), ec)) {
            fs::remove_all(entry.path(), ec);
        }
    }

    // Schema version
    int schema_version() const {
        auto j = read_json(root_ / "version.json");
        return j.value("schema_version", 0);
    }
};

} // namespace xlings::libs::agentfs
