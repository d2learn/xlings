export module xlings.libs.semantic_memory;

import std;
import xlings.libs.json;
import xlings.libs.agentfs;

namespace xlings::libs::semantic_memory {

// Cosine similarity between two vectors
export auto cosine_similarity(std::span<const float> a, std::span<const float> b) -> float {
    if (a.size() != b.size() || a.empty()) return 0.0f;
    float dot = 0, norm_a = 0, norm_b = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }
    auto denom = std::sqrt(norm_a) * std::sqrt(norm_b);
    if (denom < 1e-8f) return 0.0f;
    return dot / denom;
}

// A memory entry with optional embedding
export struct MemoryEntry {
    std::string id;
    std::string content;
    std::string category;         // "fact", "preference", "experience"
    std::vector<float> embedding; // may be empty if not yet embedded
    long long created_at_ms { 0 };
};

export struct RecallResult {
    MemoryEntry entry;
    float score;
};

// MemoryStore: remember, recall, forget
export class MemoryStore {
    agentfs::AgentFS& fs_;
    std::vector<MemoryEntry> entries_;

    auto entries_dir_() const { return fs_.memory_path() / "entries"; }

    auto entry_path_(std::string_view id) const {
        return entries_dir_() / (std::string(id) + ".json");
    }

    static auto now_ms_() -> long long {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    }

    static auto generate_id_() -> std::string {
        static std::atomic<int> counter{0};
        auto ms = now_ms_();
        auto seq = counter.fetch_add(1);
        std::ostringstream oss;
        oss << "mem-" << std::hex << ms << "-" << seq;
        return oss.str();
    }

public:
    explicit MemoryStore(agentfs::AgentFS& fs) : fs_(fs) {}

    // Load all entries from disk
    void load() {
        namespace fs = std::filesystem;
        entries_.clear();
        std::error_code ec;
        auto dir = entries_dir_();
        if (!fs::exists(dir, ec)) return;
        for (auto& entry : fs::directory_iterator(dir, ec)) {
            if (entry.path().extension() != ".json") continue;
            auto j = agentfs::AgentFS::read_json(entry.path());
            if (j.is_null()) continue;
            MemoryEntry me;
            me.id = j.value("id", "");
            me.content = j.value("content", "");
            me.category = j.value("category", "fact");
            me.created_at_ms = j.value("created_at_ms", 0LL);
            if (j.contains("embedding") && j["embedding"].is_array()) {
                for (auto& v : j["embedding"]) {
                    me.embedding.push_back(v.get<float>());
                }
            }
            if (!me.id.empty()) entries_.push_back(std::move(me));
        }
    }

    // Store a new memory
    auto remember(std::string_view content, std::string_view category = "fact",
                  std::span<const float> embedding = {}) -> std::string {
        MemoryEntry me;
        me.id = generate_id_();
        me.content = std::string(content);
        me.category = std::string(category);
        me.created_at_ms = now_ms_();
        if (!embedding.empty()) {
            me.embedding.assign(embedding.begin(), embedding.end());
        }
        save_entry_(me);
        entries_.push_back(std::move(me));
        return entries_.back().id;
    }

    // Recall by keyword (text search)
    auto recall_text(std::string_view keyword, int max_results = 5) const -> std::vector<RecallResult> {
        std::vector<RecallResult> results;
        auto lower_kw = to_lower_(keyword);
        for (auto& me : entries_) {
            auto lower_content = to_lower_(me.content);
            if (lower_content.find(lower_kw) != std::string::npos) {
                results.push_back({me, 1.0f});
            }
        }
        if (static_cast<int>(results.size()) > max_results) {
            results.resize(max_results);
        }
        return results;
    }

    // Recall by embedding similarity (brute-force cosine)
    auto recall_embedding(std::span<const float> query_embedding, int max_results = 5,
                          float min_score = 0.5f) const -> std::vector<RecallResult> {
        std::vector<RecallResult> results;
        for (auto& me : entries_) {
            if (me.embedding.empty()) continue;
            float score = cosine_similarity(query_embedding, me.embedding);
            if (score >= min_score) {
                results.push_back({me, score});
            }
        }
        std::sort(results.begin(), results.end(), [](auto& a, auto& b) {
            return a.score > b.score;
        });
        if (static_cast<int>(results.size()) > max_results) {
            results.resize(max_results);
        }
        return results;
    }

    // Forget a memory by ID
    bool forget(std::string_view id) {
        namespace fs = std::filesystem;
        auto it = std::find_if(entries_.begin(), entries_.end(), [id](auto& e) { return e.id == id; });
        if (it == entries_.end()) return false;
        std::error_code ec;
        fs::remove(entry_path_(id), ec);
        entries_.erase(it);
        return true;
    }

    auto all_entries() const -> const std::vector<MemoryEntry>& { return entries_; }
    auto size() const -> std::size_t { return entries_.size(); }

private:
    void save_entry_(const MemoryEntry& me) {
        nlohmann::json j;
        j["id"] = me.id;
        j["content"] = me.content;
        j["category"] = me.category;
        j["created_at_ms"] = me.created_at_ms;
        if (!me.embedding.empty()) {
            j["embedding"] = me.embedding;
        }
        agentfs::AgentFS::write_json(entry_path_(me.id), j);
    }

    static auto to_lower_(std::string_view s) -> std::string {
        std::string r(s);
        for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return r;
    }
};

} // namespace xlings::libs::semantic_memory
