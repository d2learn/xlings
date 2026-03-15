export module xlings.agent.context_manager;

import std;
import mcpplibs.llmapi;
import xlings.libs.json;
import xlings.agent.token_tracker;
import xlings.core.utf8;

namespace xlings::agent {

namespace llm = mcpplibs::llmapi;

// ─── L2 Turn Summary ───
// One per user-assistant turn pair, stored on disk
export struct TurnSummary {
    int turn_id;                    // sequential turn number
    std::string user_brief;         // first ~80 chars of user message
    std::string assistant_brief;    // first ~80 chars of assistant reply
    std::vector<std::string> tool_names;  // tools used in this turn
    std::vector<std::string> keywords;    // extracted topic keywords
    int estimated_tokens;           // approximate token count of original turn
};

// ─── L3 Topic Index Entry ───
// Sparse keyword → turn mapping for retrieval
export struct TopicEntry {
    std::string keyword;
    std::vector<int> turn_ids;      // which turns mention this keyword
};

// ─── Auto-compact configuration ───
export struct AutoCompactConfig {
    float trigger_ratio {0.75f};        // auto-compact when ctx_used > ratio * ctx_limit
    float target_ratio {0.50f};         // compact down to this ratio
    int min_keep_turns {3};             // always keep at least N recent turns in L1
    int max_inject_summaries {10};      // max L2 summaries to inject as context
    bool enabled {true};
};

// ─── Context Manager ───
// Manages 3-level conversation cache for ultra-long context support
//
//   L1 (Hot)  : Full messages in llm::Conversation (in-memory, sent to LLM)
//   L2 (Warm) : Turn summaries (in-memory + on-disk), injected on demand
//   L3 (Cold) : Topic keyword index for relevance-based retrieval
//
export class ContextManager {
    // L2 cache: turn summaries
    std::vector<TurnSummary> l2_summaries_;
    // L3 index: keyword → turn_ids
    std::unordered_map<std::string, std::vector<int>> l3_index_;

    int next_turn_id_ {0};
    int total_evicted_tokens_ {0};

    // Persistence path (optional)
    std::filesystem::path cache_dir_;

    AutoCompactConfig config_;
    std::string model_;

public:
    ContextManager() = default;

    explicit ContextManager(std::string_view model, AutoCompactConfig config = {})
        : config_(config), model_(model) {}

    void set_cache_dir(const std::filesystem::path& dir) {
        cache_dir_ = dir;
        load_cache_();
    }

    void set_model(std::string_view model) { model_ = std::string(model); }
    void set_config(AutoCompactConfig config) { config_ = config; }

    auto config() const -> const AutoCompactConfig& { return config_; }

    // ─── Core API ───

    // Check if auto-compact should trigger before next LLM call.
    // Returns true if compaction was performed.
    auto maybe_auto_compact(llm::Conversation& conv, const TokenTracker& tracker) -> bool {
        if (!config_.enabled) return false;

        int ctx_limit = TokenTracker::context_limit(model_);
        int ctx_used = tracker.context_used();
        if (ctx_used <= 0) return false;

        float ratio = static_cast<float>(ctx_used) / static_cast<float>(ctx_limit);
        if (ratio < config_.trigger_ratio) return false;

        // Calculate how many turns to evict to reach target ratio
        int target_tokens = static_cast<int>(ctx_limit * config_.target_ratio);
        int excess = ctx_used - target_tokens;
        if (excess <= 0) return false;

        evict_oldest_turns_(conv, excess);
        return true;
    }

    // Manually evict turns to L2, keeping last N logical turns in L1
    void compact(llm::Conversation& conv, int keep_recent_turns = 3) {
        evict_to_l2_(conv, keep_recent_turns);
    }

    // Inject relevant L2 summaries into conversation as context
    // Call this before sending to LLM to provide historical context
    auto build_context_prefix() const -> std::string {
        if (l2_summaries_.empty()) return "";

        std::string prefix = "[Previous conversation summary";
        prefix += " (" + std::to_string(l2_summaries_.size()) + " turns evicted):\n";

        int count = 0;
        int max_inject = config_.max_inject_summaries;
        // Show most recent evicted turns (they're most relevant typically)
        int start = static_cast<int>(l2_summaries_.size()) - max_inject;
        if (start < 0) start = 0;

        for (int i = start; i < static_cast<int>(l2_summaries_.size()); ++i) {
            auto& s = l2_summaries_[i];
            prefix += "  Turn " + std::to_string(s.turn_id + 1) + ": ";
            prefix += "User: " + s.user_brief;
            if (!s.assistant_brief.empty()) {
                prefix += " → Agent: " + s.assistant_brief;
            }
            if (!s.tool_names.empty()) {
                prefix += " [tools: ";
                for (std::size_t j = 0; j < s.tool_names.size(); ++j) {
                    if (j > 0) prefix += ", ";
                    prefix += s.tool_names[j];
                }
                prefix += "]";
            }
            prefix += "\n";
            ++count;
        }
        prefix += "]\n";
        return prefix;
    }

    // Retrieve L2 summaries relevant to a query (keyword match against L3 index)
    auto retrieve_relevant(std::string_view query, int max_results = 5) const
        -> std::vector<const TurnSummary*> {

        // Extract words from query
        auto words = extract_keywords_(std::string(query));

        // Score each turn by keyword overlap
        std::unordered_map<int, int> scores;
        for (auto& word : words) {
            auto it = l3_index_.find(word);
            if (it != l3_index_.end()) {
                for (int tid : it->second) {
                    scores[tid]++;
                }
            }
        }

        // Sort by score descending
        std::vector<std::pair<int, int>> ranked;
        ranked.reserve(scores.size());
        for (auto& [tid, score] : scores) {
            ranked.emplace_back(score, tid);
        }
        std::sort(ranked.begin(), ranked.end(), std::greater<>{});

        std::vector<const TurnSummary*> results;
        for (auto& [score, tid] : ranked) {
            if (static_cast<int>(results.size()) >= max_results) break;
            for (auto& s : l2_summaries_) {
                if (s.turn_id == tid) {
                    results.push_back(&s);
                    break;
                }
            }
        }
        return results;
    }

    // ─── Stats ───

    auto l2_count() const -> int { return static_cast<int>(l2_summaries_.size()); }
    auto l3_keyword_count() const -> int { return static_cast<int>(l3_index_.size()); }
    auto total_evicted_tokens() const -> int { return total_evicted_tokens_; }
    auto next_turn_id() const -> int { return next_turn_id_; }

    // Record that a turn has completed (increments turn counter)
    void record_turn() { ++next_turn_id_; }

    // Synchronize state from a restored conversation (e.g. after /resume)
    void sync_from_conversation(const llm::Conversation& conv) {
        // Count logical turns (each user message starts a turn)
        int turns = 0;
        for (auto& msg : conv.messages) {
            if (msg.role == llm::Role::User) ++turns;
        }
        if (turns > next_turn_id_) next_turn_id_ = turns;
    }

    // Persist L2/L3 cache to disk
    void save_cache() const {
        if (cache_dir_.empty()) return;
        namespace fs = std::filesystem;
        fs::create_directories(cache_dir_);

        // Save L2 summaries
        nlohmann::json l2_json = nlohmann::json::array();
        for (auto& s : l2_summaries_) {
            nlohmann::json entry;
            entry["turn_id"] = s.turn_id;
            entry["user_brief"] = s.user_brief;
            entry["assistant_brief"] = s.assistant_brief;
            entry["tool_names"] = s.tool_names;
            entry["keywords"] = s.keywords;
            entry["estimated_tokens"] = s.estimated_tokens;
            l2_json.push_back(std::move(entry));
        }

        std::ofstream l2_out(cache_dir_ / "l2_summaries.json");
        if (l2_out) l2_out << utf8::safe_dump(l2_json, 2);

        // Save L3 index
        nlohmann::json l3_json = nlohmann::json::object();
        for (auto& [kw, tids] : l3_index_) {
            l3_json[kw] = tids;
        }

        std::ofstream l3_out(cache_dir_ / "l3_index.json");
        if (l3_out) l3_out << utf8::safe_dump(l3_json, 2);

        // Save metadata
        nlohmann::json meta;
        meta["next_turn_id"] = next_turn_id_;
        meta["total_evicted_tokens"] = total_evicted_tokens_;
        meta["model"] = model_;

        std::ofstream meta_out(cache_dir_ / "context_meta.json");
        if (meta_out) meta_out << utf8::safe_dump(meta, 2);
    }

private:
    // Find logical turn boundaries in the conversation.
    // A logical turn starts with a User message and includes all subsequent
    // Assistant and Tool messages until the next User message.
    // This prevents orphaning tool_calls/tool results during eviction.
    struct LogicalTurn {
        std::size_t start_idx;     // index of user message
        std::size_t end_idx;       // index past last message in this turn
        int estimated_tokens;
    };

    auto find_logical_turns_(const llm::Conversation& conv, std::size_t start) const
        -> std::vector<LogicalTurn> {
        std::vector<LogicalTurn> turns;
        std::size_t n = conv.messages.size();

        for (std::size_t i = start; i < n; ) {
            if (conv.messages[i].role != llm::Role::User) {
                ++i;  // skip orphaned non-user messages
                continue;
            }
            LogicalTurn turn;
            turn.start_idx = i;
            turn.estimated_tokens = 0;

            // Consume this user message + all following assistant/tool messages
            for (std::size_t j = i; j < n; ++j) {
                if (j > i && conv.messages[j].role == llm::Role::User) {
                    turn.end_idx = j;
                    break;
                }
                turn.estimated_tokens += estimate_tokens_(message_text_(conv.messages[j]));
                turn.end_idx = j + 1;
            }
            turns.push_back(turn);
            i = turn.end_idx;
        }
        return turns;
    }

    // Evict oldest logical turns from conversation to L2 until we've freed `target_tokens` worth
    void evict_oldest_turns_(llm::Conversation& conv, int target_tokens) {
        // Find system message boundary
        std::size_t start = 0;
        if (!conv.messages.empty() && conv.messages[0].role == llm::Role::System) {
            start = 1;
        }
        // Skip any existing context summary messages
        while (start < conv.messages.size() &&
               conv.messages[start].role == llm::Role::System) {
            ++start;
        }

        auto turns = find_logical_turns_(conv, start);
        if (static_cast<int>(turns.size()) <= config_.min_keep_turns) return;

        int evictable_turns = static_cast<int>(turns.size()) - config_.min_keep_turns;
        int freed = 0;
        int turns_evicted = 0;

        for (int t = 0; t < evictable_turns && freed < target_tokens; ++t) {
            auto& turn = turns[t];

            // Create L2 summary from this logical turn
            TurnSummary summary;
            summary.turn_id = next_turn_id_ - (evictable_turns - t);
            if (summary.turn_id < 0) summary.turn_id = static_cast<int>(l2_summaries_.size());

            // Extract user brief from first message, assistant brief from last non-tool message
            std::string user_text, assistant_text, all_text;
            std::vector<std::string> tool_names;
            for (std::size_t i = turn.start_idx; i < turn.end_idx; ++i) {
                auto& msg = conv.messages[i];
                auto text = message_text_(msg);
                all_text += text + " ";
                if (msg.role == llm::Role::User && user_text.empty()) {
                    user_text = text;
                } else if (msg.role == llm::Role::Assistant) {
                    assistant_text = text;  // keep last assistant text
                } else if (msg.role == llm::Role::Tool) {
                    // Try to extract tool name from content
                    tool_names.push_back("tool");
                }
            }

            summary.user_brief = truncate_(user_text, 120);
            summary.assistant_brief = truncate_(assistant_text, 120);
            summary.estimated_tokens = turn.estimated_tokens;
            summary.tool_names = std::move(tool_names);
            summary.keywords = extract_keywords_(all_text);

            for (auto& kw : summary.keywords) {
                l3_index_[kw].push_back(summary.turn_id);
            }

            total_evicted_tokens_ += turn.estimated_tokens;
            freed += turn.estimated_tokens;
            l2_summaries_.push_back(std::move(summary));
            ++turns_evicted;
        }

        if (turns_evicted == 0) return;

        // Calculate the message index boundary for eviction
        std::size_t evict_end = turns[turns_evicted - 1].end_idx;

        // Remove evicted messages from conversation
        std::vector<llm::Message> remaining;
        for (std::size_t i = 0; i < start; ++i) {
            remaining.push_back(std::move(conv.messages[i]));
        }
        for (std::size_t i = evict_end; i < conv.messages.size(); ++i) {
            remaining.push_back(std::move(conv.messages[i]));
        }

        conv.messages = std::move(remaining);

        // Inject context prefix as a system message after the main system prompt
        update_context_summary_(conv);

        // Persist to disk
        save_cache();
    }

    // Manual eviction: keep only last N logical turns
    void evict_to_l2_(llm::Conversation& conv, int keep_turns) {
        std::size_t start = 0;
        if (!conv.messages.empty() && conv.messages[0].role == llm::Role::System) {
            start = 1;
        }
        while (start < conv.messages.size() &&
               conv.messages[start].role == llm::Role::System) {
            ++start;
        }

        auto turns = find_logical_turns_(conv, start);
        if (static_cast<int>(turns.size()) <= keep_turns) return;

        int to_evict = static_cast<int>(turns.size()) - keep_turns;

        for (int t = 0; t < to_evict; ++t) {
            auto& turn = turns[t];

            TurnSummary summary;
            summary.turn_id = static_cast<int>(l2_summaries_.size());

            std::string user_text, assistant_text, all_text;
            for (std::size_t i = turn.start_idx; i < turn.end_idx; ++i) {
                auto& msg = conv.messages[i];
                auto text = message_text_(msg);
                all_text += text + " ";
                if (msg.role == llm::Role::User && user_text.empty()) {
                    user_text = text;
                } else if (msg.role == llm::Role::Assistant) {
                    assistant_text = text;
                }
            }

            summary.user_brief = truncate_(user_text, 120);
            summary.assistant_brief = truncate_(assistant_text, 120);
            summary.estimated_tokens = turn.estimated_tokens;
            summary.keywords = extract_keywords_(all_text);

            for (auto& kw : summary.keywords) {
                l3_index_[kw].push_back(summary.turn_id);
            }

            total_evicted_tokens_ += summary.estimated_tokens;
            l2_summaries_.push_back(std::move(summary));
        }

        // Remove evicted messages
        std::size_t evict_end = turns[to_evict - 1].end_idx;
        std::vector<llm::Message> remaining;
        for (std::size_t i = 0; i < start; ++i) {
            remaining.push_back(std::move(conv.messages[i]));
        }
        for (std::size_t i = evict_end; i < conv.messages.size(); ++i) {
            remaining.push_back(std::move(conv.messages[i]));
        }
        conv.messages = std::move(remaining);

        update_context_summary_(conv);
        save_cache();
    }

    // Update or insert context summary message in conversation
    void update_context_summary_(llm::Conversation& conv) {
        auto prefix = build_context_prefix();
        if (prefix.empty()) return;

        // Find insertion point: after first system message
        std::size_t insert_at = 0;
        if (!conv.messages.empty() && conv.messages[0].role == llm::Role::System) {
            insert_at = 1;
        }

        // Check if there's already a context summary at this position
        if (insert_at < conv.messages.size() &&
            conv.messages[insert_at].role == llm::Role::System) {
            // Check if it's our summary (starts with "[Previous")
            auto existing = message_text_(conv.messages[insert_at]);
            if (existing.starts_with("[Previous conversation summary") ||
                existing.starts_with("[Earlier conversation context")) {
                // Replace it
                conv.messages[insert_at] = llm::Message::system(prefix);
                return;
            }
        }

        // Insert new summary
        conv.messages.insert(conv.messages.begin() + insert_at,
                             llm::Message::system(prefix));
    }

    // Extract text from a Message
    static auto message_text_(const llm::Message& msg) -> std::string {
        if (auto* s = std::get_if<std::string>(&msg.content)) {
            return *s;
        }
        if (auto* parts = std::get_if<std::vector<llm::ContentPart>>(&msg.content)) {
            std::string result;
            for (auto& part : *parts) {
                if (auto* t = std::get_if<llm::TextContent>(&part)) {
                    result += t->text;
                }
            }
            return result;
        }
        return "";
    }

    // Rough token estimation (~4 chars per token for English, ~2 for CJK)
    static auto estimate_tokens_(const std::string& text) -> int {
        int chars = 0;
        int cjk = 0;
        for (unsigned char c : text) {
            ++chars;
            if (c > 0x7F) ++cjk;
        }
        // Rough heuristic: CJK chars ~2 per token, ASCII ~4 per token
        return (chars - cjk) / 4 + cjk / 2 + 1;
    }

    // Extract keywords from text for L3 indexing
    static auto extract_keywords_(const std::string& text) -> std::vector<std::string> {
        std::vector<std::string> keywords;
        std::string word;

        for (char c : text) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' ||
                static_cast<unsigned char>(c) > 0x7F) {
                word += std::tolower(static_cast<unsigned char>(c));
            } else {
                if (word.size() >= 3) {
                    // Skip common stop words
                    if (!is_stop_word_(word)) {
                        keywords.push_back(std::move(word));
                    }
                }
                word.clear();
            }
        }
        if (word.size() >= 3 && !is_stop_word_(word)) {
            keywords.push_back(std::move(word));
        }

        // Deduplicate
        std::sort(keywords.begin(), keywords.end());
        keywords.erase(std::unique(keywords.begin(), keywords.end()), keywords.end());

        // Keep at most 20 keywords per turn
        if (keywords.size() > 20) keywords.resize(20);

        return keywords;
    }

    static auto is_stop_word_(const std::string& w) -> bool {
        static const std::unordered_set<std::string> stops = {
            "the", "and", "for", "are", "but", "not", "you", "all",
            "can", "has", "her", "was", "one", "our", "out", "had",
            "that", "this", "with", "have", "from", "they", "been",
            "will", "your", "what", "when", "make", "like", "just",
            "know", "take", "come", "could", "than", "look", "only",
            "into", "some", "also", "after", "use", "two", "how",
        };
        return stops.contains(w);
    }

    static auto truncate_(const std::string& text, std::size_t max_len) -> std::string {
        if (text.size() <= max_len) return text;
        // Find a good break point
        auto pos = text.rfind(' ', max_len);
        if (pos == std::string::npos || pos < max_len / 2) pos = max_len;
        return std::string(text.substr(0, pos)) + "...";
    }

    // Load cache from disk
    void load_cache_() {
        if (cache_dir_.empty()) return;
        namespace fs = std::filesystem;

        // Load metadata
        auto meta_path = cache_dir_ / "context_meta.json";
        if (fs::exists(meta_path)) {
            std::ifstream in(meta_path);
            if (in) {
                auto j = nlohmann::json::parse(in, nullptr, false);
                if (!j.is_discarded()) {
                    next_turn_id_ = j.value("next_turn_id", 0);
                    total_evicted_tokens_ = j.value("total_evicted_tokens", 0);
                }
            }
        }

        // Load L2 summaries
        auto l2_path = cache_dir_ / "l2_summaries.json";
        if (fs::exists(l2_path)) {
            std::ifstream in(l2_path);
            if (in) {
                auto j = nlohmann::json::parse(in, nullptr, false);
                if (j.is_array()) {
                    for (auto& entry : j) {
                        TurnSummary s;
                        s.turn_id = entry.value("turn_id", 0);
                        s.user_brief = entry.value("user_brief", "");
                        s.assistant_brief = entry.value("assistant_brief", "");
                        if (entry.contains("tool_names") && entry["tool_names"].is_array()) {
                            for (auto& t : entry["tool_names"]) {
                                s.tool_names.push_back(t.get<std::string>());
                            }
                        }
                        if (entry.contains("keywords") && entry["keywords"].is_array()) {
                            for (auto& k : entry["keywords"]) {
                                s.keywords.push_back(k.get<std::string>());
                            }
                        }
                        s.estimated_tokens = entry.value("estimated_tokens", 0);
                        l2_summaries_.push_back(std::move(s));
                    }
                }
            }
        }

        // Load L3 index
        auto l3_path = cache_dir_ / "l3_index.json";
        if (fs::exists(l3_path)) {
            std::ifstream in(l3_path);
            if (in) {
                auto j = nlohmann::json::parse(in, nullptr, false);
                if (j.is_object()) {
                    for (auto it = j.begin(); it != j.end(); ++it) {
                        if (it.value().is_array()) {
                            std::vector<int> tids;
                            for (auto& v : it.value()) tids.push_back(v.get<int>());
                            l3_index_[it.key()] = std::move(tids);
                        }
                    }
                }
            }
        }
    }
};

} // namespace xlings::agent
