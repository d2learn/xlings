export module xlings.agent.tool_bridge;

import std;
import xlings.runtime.event;
import xlings.runtime.capability;
import xlings.runtime.event_stream;
import xlings.runtime.cancellation;
import xlings.libs.json;
import xlings.core.utf8;

namespace xlings::agent {

export struct ToolDef {
    std::string name;
    std::string description;
    std::string inputSchema;
};

export struct ToolResult {
    std::string content;
    bool isError { false };
};

export struct ToolInfo {
    std::string name;
    std::string source;       // "builtin" or "pkg:<name>" or "mcp:<server>"
    bool destructive { false };
    int tier { 0 };           // 0=T0, 1=T1, 2=T2
};

export class ToolBridge {
    capability::Registry& registry_;
    std::vector<DataEvent> event_buffer_;

public:
    explicit ToolBridge(capability::Registry& registry) : registry_(registry) {}

    // EventStream consumer callback — captures DataEvents during tool execution
    void on_data_event(const DataEvent& e) {
        event_buffer_.push_back(e);
    }

    auto tool_definitions() const -> std::vector<ToolDef> {
        std::vector<ToolDef> tools;
        for (const auto& spec : registry_.list_all()) {
            tools.push_back(ToolDef{
                .name = spec.name,
                .description = spec.description,
                .inputSchema = spec.inputSchema,
            });
        }
        return tools;
    }

    auto execute(
        std::string_view name,
        std::string_view arguments,
        EventStream& stream,
        CancellationToken* cancel = nullptr
    ) -> ToolResult {
        auto* cap = registry_.get(name);
        if (!cap) {
            return ToolResult{
                .content = "{\"error\":\"unknown tool: " + std::string(name) + "\"}",
                .isError = true,
            };
        }
        try {
            if (cancel) cancel->throw_if_cancelled();

            // Clear buffer before execution so we only capture this tool's events
            event_buffer_.clear();

            auto result = cap->execute(std::string(arguments), stream, cancel);

            if (cancel) cancel->throw_if_cancelled();

            // Check exitCode in result to determine error status
            auto result_json = nlohmann::json::parse(result, nullptr, false);
            bool is_error = false;
            if (!result_json.is_discarded() && result_json.contains("exitCode")) {
                is_error = result_json["exitCode"].get<int>() != 0;
            }

            // If events were captured, inject them into the result JSON
            if (!event_buffer_.empty()) {
                bool was_discarded = result_json.is_discarded();
                auto json = was_discarded
                    ? nlohmann::json::object() : std::move(result_json);
                if (was_discarded) {
                    json["rawResult"] = result;
                }
                nlohmann::json events = nlohmann::json::array();
                for (auto& ev : event_buffer_) {
                    auto data = nlohmann::json::parse(ev.json, nullptr, false);
                    if (data.is_discarded()) {
                        events.push_back({{"kind", ev.kind}, {"data", ev.json}});
                    } else {
                        events.push_back({{"kind", ev.kind}, {"data", std::move(data)}});
                    }
                }
                json["events"] = std::move(events);
                event_buffer_.clear();
                return ToolResult{.content = utf8::safe_dump(json), .isError = is_error};
            }

            return ToolResult{.content = result, .isError = is_error};
        } catch (const std::exception& e) {
            event_buffer_.clear();
            return ToolResult{
                .content = std::string("{\"error\":\"") + e.what() + "\"}",
                .isError = true,
            };
        }
    }

    auto tool_info(std::string_view name) const -> ToolInfo {
        for (const auto& spec : registry_.list_all()) {
            if (spec.name == name) {
                return ToolInfo{
                    .name = spec.name,
                    .source = "builtin",
                    .destructive = spec.destructive,
                    .tier = 0,
                };
            }
        }
        return ToolInfo{.name = std::string(name), .source = "unknown"};
    }
};

} // namespace xlings::agent
