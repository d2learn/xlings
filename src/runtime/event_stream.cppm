export module xlings.runtime.event_stream;

import std;

import xlings.runtime.event;

namespace xlings {

export using EventConsumer = std::function<void(const Event&)>;

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

    auto prompt(PromptEvent req) -> std::string {
        auto id = req.id;
        emit(Event{std::move(req)});

        std::unique_lock lock(promptMutex_);
        promptCv_.wait(lock, [&] {
            return promptResponses_.contains(id);
        });

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
};

}  // namespace xlings
