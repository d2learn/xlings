module;

#include <string>
#include <variant>
#include <vector>

export module xlings.event;

namespace xlings {

// ─── Phase ───
export enum class Phase {
    resolving,
    downloading,
    extracting,
    installing,
    configuring,
    verifying
};

// ─── LogLevel ───
export enum class LogLevel { debug, info, warn, error };

// ─── Event Types ───

export struct ProgressEvent {
    Phase phase;
    float percent;           // 0.0 ~ 1.0
    std::string message;
};

export struct LogEvent {
    LogLevel level;
    std::string message;
};

export struct PromptEvent {
    std::string id;
    std::string question;
    std::vector<std::string> options;    // empty = free input
    std::string defaultValue;
};

export struct ErrorEvent {
    int code;
    std::string message;
    bool recoverable { false };
};

export struct DataEvent {
    std::string kind;        // "search_results" / "package_info" etc.
    std::string json;
};

export struct CompletedEvent {
    bool success;
    std::string summary;
};

// ─── Unified Event ───
export using Event = std::variant<
    ProgressEvent,
    LogEvent,
    PromptEvent,
    ErrorEvent,
    DataEvent,
    CompletedEvent
>;

}  // namespace xlings
