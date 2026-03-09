module;

#include <string>
#include <variant>
#include <vector>

export module xlings.event;

namespace xlings {

export enum class Phase {
    resolving,
    downloading,
    extracting,
    installing,
    configuring,
    verifying
};

export enum class LogLevel { debug, info, warn, error };

export struct ProgressEvent {
    Phase phase;
    float percent;
    std::string message;
};

export struct LogEvent {
    LogLevel level;
    std::string message;
};

export struct PromptEvent {
    std::string id;
    std::string question;
    std::vector<std::string> options;
    std::string default_value;
};

export struct ErrorEvent {
    int code;
    std::string message;
    bool recoverable = false;
};

export struct DataEvent {
    std::string kind;
    std::string json;
};

export struct CompletedEvent {
    bool success;
    std::string summary;
};

export using Event = std::variant<
    ProgressEvent,
    LogEvent,
    PromptEvent,
    ErrorEvent,
    DataEvent,
    CompletedEvent
>;

}  // namespace xlings
