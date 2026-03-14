export module xlings.agent.token_tracker;

import std;

namespace xlings::agent {

export class TokenTracker {
    int session_input_ {0};
    int session_output_ {0};
    int session_cache_read_ {0};
    int session_cache_write_ {0};
    int last_context_size_ {0};

    // Per-turn counters (reset each turn)
    int turn_input_ {0};
    int turn_output_ {0};

public:
    void record(int input_tokens, int output_tokens,
                int cache_read = 0, int cache_write = 0) {
        session_input_ += input_tokens;
        session_output_ += output_tokens;
        session_cache_read_ += cache_read;
        session_cache_write_ += cache_write;
        turn_input_ += input_tokens;
        turn_output_ += output_tokens;
        last_context_size_ = input_tokens;
    }

    void begin_turn() { turn_input_ = 0; turn_output_ = 0; }
    auto turn_input() const -> int { return turn_input_; }
    auto turn_output() const -> int { return turn_output_; }

    auto session_input() const -> int { return session_input_; }
    auto session_output() const -> int { return session_output_; }
    auto session_cache_read() const -> int { return session_cache_read_; }
    auto session_cache_write() const -> int { return session_cache_write_; }
    auto context_used() const -> int { return last_context_size_; }

    void reset() {
        session_input_ = 0;
        session_output_ = 0;
        session_cache_read_ = 0;
        session_cache_write_ = 0;
        last_context_size_ = 0;
    }

    static auto context_limit(std::string_view model) -> int {
        if (model.find("claude") != std::string_view::npos) return 200000;
        if (model.find("gpt-4") != std::string_view::npos) return 128000;
        if (model.find("gpt-5") != std::string_view::npos) return 128000;
        if (model.find("deepseek") != std::string_view::npos) return 64000;
        if (model.find("qwen") != std::string_view::npos) return 128000;
        if (model.find("MiniMax") != std::string_view::npos) return 128000;
        if (model.find("minimax") != std::string_view::npos) return 128000;
        return 32000;  // default
    }

    static auto format_tokens(int tokens) -> std::string {
        if (tokens >= 1000) {
            return std::format("{:.1f}k", tokens / 1000.0);
        }
        return std::to_string(tokens);
    }
};

} // namespace xlings::agent
