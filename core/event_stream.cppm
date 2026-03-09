module;

#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

export module xlings.event_stream;

import xlings.event;

namespace xlings {

export using EventConsumer = std::function<void(const Event&)>;

export class EventStream {
private:
    std::vector<EventConsumer> consumers_;
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
    void on_event(EventConsumer consumer) {
        consumers_.push_back(std::move(consumer));
    }

    void emit(Event event) {
        for (auto& consumer : consumers_) {
            consumer(event);
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
