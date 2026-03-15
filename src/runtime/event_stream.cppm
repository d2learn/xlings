export module xlings.runtime.event_stream;

import std;

import xlings.runtime.event;
import xlings.runtime.cancellation;

namespace xlings {

export using EventConsumer = std::function<void(const Event&)>;

// Auto-responder: given a PromptEvent, returns a response string
export using AutoResponder = std::function<std::string(const PromptEvent&)>;

export class EventStream {
private:
    struct ListenerEntry {
        int id;
        EventConsumer consumer;
        bool enabled;
    };
    std::vector<ListenerEntry> consumers_;
    int next_id_ {0};

    std::mutex promptMutex_;
    std::condition_variable promptCv_;
    std::unordered_map<std::string, std::string> promptResponses_;

    // Auto-responders: prefix → handler
    std::vector<std::pair<std::string, AutoResponder>> auto_responders_;

public:
    EventStream() = default;
    ~EventStream() = default;
    EventStream(const EventStream&) = delete;
    EventStream& operator=(const EventStream&) = delete;
    EventStream(EventStream&&) = delete;
    EventStream& operator=(EventStream&&) = delete;

    // Thread safety: register all consumers before emitting from other threads.
    // on_event() and emit() are not synchronized — call on_event() during setup only.
    auto on_event(EventConsumer consumer) -> int {
        int id = next_id_++;
        consumers_.push_back({id, std::move(consumer), true});
        return id;
    }

    void remove_listener(int id) {
        std::erase_if(consumers_, [id](const ListenerEntry& e) { return e.id == id; });
    }

    void set_enabled(int id, bool enabled) {
        for (auto& entry : consumers_) {
            if (entry.id == id) {
                entry.enabled = enabled;
                return;
            }
        }
    }

    void emit(Event event) {
        for (auto& entry : consumers_) {
            if (entry.enabled) entry.consumer(event);
        }
    }

    // Register an auto-responder for prompts whose id starts with the given prefix.
    // In agent mode, this allows automatic responses to install confirmations, etc.
    void register_auto_responder(std::string prompt_id_prefix, AutoResponder fn) {
        auto_responders_.emplace_back(std::move(prompt_id_prefix), std::move(fn));
    }

    void clear_auto_responders() {
        auto_responders_.clear();
    }

    // Prompt with optional cancellation and timeout support.
    // Returns empty string on cancel/timeout.
    auto prompt(PromptEvent req,
                CancellationToken* cancel = nullptr,
                std::chrono::milliseconds timeout = std::chrono::milliseconds{30000}) -> std::string {
        auto id = req.id;

        // Check auto-responders first (by prefix match)
        for (auto& [prefix, responder] : auto_responders_) {
            if (id.starts_with(prefix)) {
                return responder(req);
            }
        }

        emit(Event{std::move(req)});

        std::unique_lock lock(promptMutex_);

        if (cancel) {
            bool satisfied = cancel->wait_or_cancel(lock, promptCv_,
                [&] { return promptResponses_.contains(id); }, timeout);
            if (!satisfied) {
                // Cancelled or timed out — clean up any stale entry
                promptResponses_.erase(id);
                return "";
            }
        } else {
            promptCv_.wait(lock, [&] {
                return promptResponses_.contains(id);
            });
        }

        auto response = std::move(promptResponses_[id]);
        promptResponses_.erase(id);
        return response;
    }

    void respond(std::string_view promptId, std::string_view response) {
        {
            std::lock_guard lock(promptMutex_);
            promptResponses_[std::string(promptId)] = std::string(response);
        }
        promptCv_.notify_all();
    }

    // Cancel all pending prompts (call from ESC handler)
    void cancel_all_prompts() {
        std::lock_guard lock(promptMutex_);
        promptResponses_.clear();
        promptCv_.notify_all();
    }
};

}  // namespace xlings
