export module xlings.libs.mcp_server;

import std;
import xlings.libs.json;

namespace xlings::libs::mcp {

// JSON-RPC 2.0 message types
export struct JsonRpcRequest {
    std::string jsonrpc { "2.0" };
    std::string method;
    nlohmann::json params;
    nlohmann::json id;  // string or int
};

export struct JsonRpcResponse {
    std::string jsonrpc { "2.0" };
    nlohmann::json result;
    nlohmann::json error;
    nlohmann::json id;
};

// MCP Tool definition (server-side)
export struct McpToolDef {
    std::string name;
    std::string description;
    nlohmann::json inputSchema;
};

// MCP Tool handler: takes params JSON, returns result JSON
export using McpToolHandler = std::function<nlohmann::json(const nlohmann::json&)>;

// MCP Server over stdio
export class McpServer {
    std::string name_;
    std::string version_;
    std::vector<McpToolDef> tools_;
    std::unordered_map<std::string, McpToolHandler> handlers_;
    bool running_ { false };

public:
    McpServer(std::string_view name, std::string_view version)
        : name_(name), version_(version) {}

    void register_tool(McpToolDef def, McpToolHandler handler) {
        handlers_[def.name] = std::move(handler);
        tools_.push_back(std::move(def));
    }

    // Process a single JSON-RPC request and return response
    auto handle_request(const JsonRpcRequest& req) -> JsonRpcResponse {
        JsonRpcResponse resp;
        resp.id = req.id;

        if (req.method == "initialize") {
            resp.result = {
                {"protocolVersion", "2024-11-05"},
                {"capabilities", {{"tools", nlohmann::json::object()}}},
                {"serverInfo", {{"name", name_}, {"version", version_}}},
            };
        }
        else if (req.method == "tools/list") {
            nlohmann::json tools_json = nlohmann::json::array();
            for (auto& t : tools_) {
                tools_json.push_back({
                    {"name", t.name},
                    {"description", t.description},
                    {"inputSchema", t.inputSchema},
                });
            }
            resp.result = {{"tools", tools_json}};
        }
        else if (req.method == "tools/call") {
            auto tool_name = req.params.value("name", "");
            auto it = handlers_.find(tool_name);
            if (it == handlers_.end()) {
                resp.error = {
                    {"code", -32601},
                    {"message", "Tool not found: " + tool_name},
                };
            } else {
                auto arguments = req.params.value("arguments", nlohmann::json::object());
                auto result = it->second(arguments);
                resp.result = {
                    {"content", {{{"type", "text"}, {"text", result.dump()}}}},
                };
            }
        }
        else if (req.method == "notifications/initialized") {
            // Notification, no response needed
            resp.result = nullptr;
        }
        else {
            resp.error = {
                {"code", -32601},
                {"message", "Method not found: " + req.method},
            };
        }

        return resp;
    }

    // Parse a JSON string into a request
    static auto parse_request(std::string_view json_str) -> std::optional<JsonRpcRequest> {
        auto j = nlohmann::json::parse(json_str, nullptr, false);
        if (j.is_discarded()) return std::nullopt;

        JsonRpcRequest req;
        req.jsonrpc = j.value("jsonrpc", "2.0");
        req.method = j.value("method", "");
        req.params = j.value("params", nlohmann::json::object());
        if (j.contains("id")) req.id = j["id"];
        return req;
    }

    // Serialize a response to JSON string
    static auto serialize_response(const JsonRpcResponse& resp) -> std::string {
        if (resp.result.is_null() && resp.error.is_null()) return ""; // notification
        nlohmann::json j;
        j["jsonrpc"] = resp.jsonrpc;
        j["id"] = resp.id;
        if (!resp.error.is_null()) {
            j["error"] = resp.error;
        } else {
            j["result"] = resp.result;
        }
        return j.dump();
    }

    // Run stdio loop (blocking)
    void run_stdio() {
        running_ = true;
        std::string line;
        while (running_ && std::getline(std::cin, line)) {
            if (line.empty()) continue;
            auto req = parse_request(line);
            if (!req) continue;
            auto resp = handle_request(*req);
            auto out = serialize_response(resp);
            if (!out.empty()) {
                std::println("{}", out);
                std::cout.flush();
            }
        }
    }

    void stop() { running_ = false; }

    auto tools() const -> const std::vector<McpToolDef>& { return tools_; }
};

} // namespace xlings::libs::mcp
