export module xlings.agent.lua_engine;

import std;
import mcpplibs.capi.lua;
import xlings.runtime.capability;
import xlings.runtime.event_stream;
import xlings.runtime.cancellation;
import xlings.libs.json;

namespace lua = mcpplibs::capi::lua;

namespace xlings::agent {

// ─── Layer constants (int, not enum — GCC 15 safety) ───

export inline constexpr int LayerPrimitive = 0;  // single capability call
export inline constexpr int LayerRoutine   = 1;  // learned Lua function
export inline constexpr int LayerDecision  = 2;  // LLM-generated Lua orchestration
export inline constexpr int LayerPlan      = 3;  // multi-step Decision composition

// ─── State constants ───

export inline constexpr int StatePending   = 0;
export inline constexpr int StateRunning   = 1;
export inline constexpr int StateDone      = 2;
export inline constexpr int StateFailed    = 3;
export inline constexpr int StateCancelled = 4;

// ─── Action result ───

export struct ActionResult {
    bool success;
    std::string data;  // JSON string
};

// ─── Action tree node ───

export struct Action {
    int layer;
    int state {StatePending};
    std::string name;
    std::string detail;
    std::optional<ActionResult> result;
    std::vector<Action> children;
    std::string summary;
    bool collapsed {false};
    std::int64_t start_ms {0};
    std::int64_t end_ms {0};
};

// ─── Execution log (feedback to LLM) ───

export struct StepLog {
    std::string fn;           // "pkg.search"
    std::string args_json;    // {"query": "vim"}
    std::string result_json;  // capability return value
    std::int64_t duration_ms;
    bool success;
};

export struct ExecutionLog {
    std::string status;       // "completed" | "error" | "timeout" | "cancelled"
    std::vector<StepLog> steps;
    std::string return_value; // Lua return value as JSON
    std::string error;        // if status != "completed"
    std::int64_t duration_ms;

    auto to_json() const -> std::string {
        nlohmann::json j;
        j["status"] = status;
        j["duration_ms"] = duration_ms;
        if (!error.empty()) j["error"] = error;
        if (!return_value.empty()) {
            auto rv = nlohmann::json::parse(return_value, nullptr, false);
            if (!rv.is_discarded()) {
                j["return_value"] = std::move(rv);
            } else {
                j["return_value"] = return_value;
            }
        }
        if (!steps.empty()) {
            nlohmann::json arr = nlohmann::json::array();
            for (auto& s : steps) {
                nlohmann::json step;
                step["fn"] = s.fn;
                step["duration_ms"] = s.duration_ms;
                step["success"] = s.success;
                // Parse args/result as JSON if possible
                auto a = nlohmann::json::parse(s.args_json, nullptr, false);
                step["args"] = a.is_discarded() ? nlohmann::json(s.args_json) : std::move(a);
                auto r = nlohmann::json::parse(s.result_json, nullptr, false);
                step["result"] = r.is_discarded() ? nlohmann::json(s.result_json) : std::move(r);
                arr.push_back(std::move(step));
            }
            j["steps"] = std::move(arr);
        }
        return j.dump();
    }
};

// ─── Lua ↔ JSON conversion helpers ───

// Forward declarations
void json_to_lua(lua::State* L, const nlohmann::json& j);
auto lua_to_json(lua::State* L, int idx) -> nlohmann::json;

void json_to_lua(lua::State* L, const nlohmann::json& j) {
    if (j.is_null()) {
        lua::pushnil(L);
    } else if (j.is_boolean()) {
        lua::pushboolean(L, j.get<bool>() ? 1 : 0);
    } else if (j.is_number_integer()) {
        lua::pushinteger(L, j.get<lua::Integer>());
    } else if (j.is_number_float()) {
        lua::pushnumber(L, j.get<lua::Number>());
    } else if (j.is_string()) {
        auto s = j.get<std::string>();
        lua::pushstring(L, s.c_str());
    } else if (j.is_array()) {
        lua::newtable(L);
        for (std::size_t i = 0; i < j.size(); ++i) {
            json_to_lua(L, j[i]);
            lua::seti(L, -2, static_cast<lua::Integer>(i + 1));
        }
    } else if (j.is_object()) {
        lua::newtable(L);
        for (auto it = j.begin(); it != j.end(); ++it) {
            json_to_lua(L, it.value());
            lua::setfield(L, -2, it.key().c_str());
        }
    } else {
        lua::pushnil(L);
    }
}

auto lua_to_json(lua::State* L, int idx) -> nlohmann::json {
    int t = lua::type(L, idx);
    switch (t) {
        case lua::TNIL:
            return nullptr;
        case lua::TBOOLEAN:
            return lua::toboolean(L, idx) != 0;
        case lua::TNUMBER:
            if (lua::isinteger(L, idx)) {
                return lua::tointeger(L, idx);
            }
            return lua::tonumber(L, idx);
        case lua::TSTRING:
            return std::string(lua::tostring(L, idx));
        case lua::TTABLE: {
            // Determine if array or object: check if sequential integer keys 1..N
            bool is_array = true;
            lua::Integer max_idx = 0;
            lua::Integer count = 0;

            lua::pushnil(L);
            while (lua::next(L, idx < 0 ? idx - 1 : idx) != 0) {
                ++count;
                if (lua::isinteger(L, -2)) {
                    auto k = lua::tointeger(L, -2);
                    if (k > max_idx) max_idx = k;
                    if (k < 1) is_array = false;
                } else {
                    is_array = false;
                }
                lua::pop(L, 1);  // pop value, keep key
            }

            // Array if all keys are 1..count
            if (is_array && max_idx == count && count > 0) {
                nlohmann::json arr = nlohmann::json::array();
                for (lua::Integer i = 1; i <= count; ++i) {
                    lua::geti(L, idx, i);
                    arr.push_back(lua_to_json(L, lua::gettop(L)));
                    lua::pop(L, 1);
                }
                return arr;
            }

            // Object (or empty table → object)
            nlohmann::json obj = nlohmann::json::object();
            lua::pushnil(L);
            while (lua::next(L, idx < 0 ? idx - 1 : idx) != 0) {
                std::string key;
                if (lua::type(L, -2) == lua::TSTRING) {
                    key = lua::tostring(L, -2);
                } else if (lua::isinteger(L, -2)) {
                    key = std::to_string(lua::tointeger(L, -2));
                } else {
                    lua::pop(L, 1);
                    continue;  // skip non-string/integer keys
                }
                obj[key] = lua_to_json(L, lua::gettop(L));
                lua::pop(L, 1);
            }
            return obj;
        }
        default:
            return nullptr;  // skip functions, userdata, etc.
    }
}

// Convert Lua stack value at idx to JSON string
auto lua_value_to_json(lua::State* L, int idx) -> std::string {
    return lua_to_json(L, idx).dump();
}

// Convert Lua stack arguments to JSON string
auto lua_args_to_json(lua::State* L) -> std::string {
    int nargs = lua::gettop(L);
    if (nargs == 0) return "{}";
    if (nargs == 1) return lua_value_to_json(L, 1);
    nlohmann::json arr = nlohmann::json::array();
    for (int i = 1; i <= nargs; ++i) {
        arr.push_back(lua_to_json(L, i));
    }
    return arr.dump();
}

// Push JSON string as Lua value onto the stack
void json_to_lua_table(lua::State* L, std::string_view json_str) {
    auto j = nlohmann::json::parse(json_str, nullptr, false);
    if (j.is_discarded()) {
        lua::pushnil(L);
        return;
    }
    json_to_lua(L, j);
}

// ─── Binding table ───

struct LuaBinding {
    const char* lua_module;
    const char* lua_func;
    const char* capability;
    bool destructive;
};

constexpr LuaBinding bindings[] = {
    {"pkg", "search",  "search_packages",  false},
    {"pkg", "install", "install_packages",  true},
    {"pkg", "remove",  "remove_package",    true},
    {"pkg", "list",    "list_packages",     false},
    {"pkg", "info",    "package_info",      false},
    {"pkg", "update",  "update_packages",   true},
    {"sys", "status",  "system_status",     false},
    {"sys", "run",     "run_command",       true},
    {"ver", "use",     "use_version",       true},
};

// ─── Time helper ───

auto now_ms() -> std::int64_t {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ─── LuaSandbox ───

export class LuaSandbox {
    lua::State* L_ {nullptr};
    capability::Registry& registry_;
    EventStream& stream_;
    CancellationToken* cancel_ {nullptr};

    // Mutable state during execution (only accessed from worker thread)
    Action* current_action_ {nullptr};
    std::vector<StepLog> step_log_;
    std::int64_t exec_start_ms_ {0};

    // For module.func reverse lookup in step log
    static auto find_lua_name(const char* cap_name) -> std::string {
        for (auto& b : bindings) {
            if (std::strcmp(b.capability, cap_name) == 0) {
                return std::string(b.lua_module) + "." + b.lua_func;
            }
        }
        return cap_name;
    }

public:
    explicit LuaSandbox(capability::Registry& registry, EventStream& stream)
        : registry_(registry), stream_(stream) {
        init_sandbox_();
    }

    ~LuaSandbox() {
        if (L_) lua::close(L_);
    }

    // Non-copyable, non-movable
    LuaSandbox(const LuaSandbox&) = delete;
    LuaSandbox& operator=(const LuaSandbox&) = delete;

    void set_cancel(CancellationToken* cancel) { cancel_ = cancel; }

    auto execute(std::string_view lua_code, Action& root_action,
                 std::chrono::milliseconds timeout = std::chrono::milliseconds{30000})
        -> ExecutionLog {

        // Set execution context
        current_action_ = &root_action;
        step_log_.clear();
        exec_start_ms_ = now_ms();
        root_action.state = StateRunning;
        root_action.start_ms = exec_start_ms_;

        // Store sandbox pointer in registry for hooks
        lua::pushlightuserdata(L_, this);
        lua::setfield(L_, lua::REGISTRYINDEX, "__sandbox_ptr");

        // Shared state (same pattern as llm_call_worker)
        auto done_flag  = std::make_shared<std::atomic<bool>>(false);
        auto abandoned  = std::make_shared<std::atomic<bool>>(false);
        auto result_log = std::make_shared<ExecutionLog>();
        auto err_ptr    = std::make_shared<std::exception_ptr>();
        auto cv_mtx     = std::make_shared<std::mutex>();
        auto cv_done    = std::make_shared<std::condition_variable>();

        // Set instruction count hook for infinite loop protection + cancel check
        lua::sethook(L_, instruction_count_hook_, lua::MASKCOUNT, 10000);

        // Worker thread executes Lua
        auto code = std::string(lua_code);
        std::thread worker([this, code,
                            done_flag, abandoned, result_log, err_ptr,
                            cv_mtx, cv_done]() {
            try {
                int load_err = lua::L_loadstring(L_, code.c_str());
                if (load_err != lua::OK) {
                    const char* msg = lua::tostring(L_, -1);
                    result_log->status = "error";
                    result_log->error = msg ? msg : "syntax error";
                    lua::pop(L_, 1);
                } else {
                    int call_err = lua::pcall(L_, 0, 1, 0);
                    if (call_err != lua::OK) {
                        const char* msg = lua::tostring(L_, -1);
                        result_log->status = "error";
                        result_log->error = msg ? msg : "runtime error";
                        lua::pop(L_, 1);
                    } else {
                        result_log->status = "completed";
                        if (!lua::isnoneornil(L_, -1)) {
                            result_log->return_value = lua_value_to_json(L_, -1);
                        }
                        lua::pop(L_, 1);
                    }
                }
                result_log->steps = step_log_;
                result_log->duration_ms = now_ms() - exec_start_ms_;
            } catch (...) {
                *err_ptr = std::current_exception();
            }
            done_flag->store(true, std::memory_order_release);
            cv_done->notify_all();
        });

        // Main thread waits (same pattern as llm_call_worker)
        {
            std::unique_lock lk(*cv_mtx);
            while (!done_flag->load(std::memory_order_acquire)) {
                // Check timeout
                if (now_ms() - exec_start_ms_ > timeout.count()) {
                    abandoned->store(true, std::memory_order_release);
                    lua::sethook(L_, force_stop_hook_, lua::MASKCOUNT, 1);
                    worker.detach();
                    root_action.state = StateFailed;
                    root_action.end_ms = now_ms();
                    return ExecutionLog{
                        .status = "timeout",
                        .steps = step_log_,
                        .error = "execution exceeded timeout",
                        .duration_ms = now_ms() - exec_start_ms_,
                    };
                }
                // Check cancellation
                if (cancel_ && !cancel_->is_active()) {
                    abandoned->store(true, std::memory_order_release);
                    lua::sethook(L_, force_stop_hook_, lua::MASKCOUNT, 1);
                    worker.detach();
                    root_action.state = StateCancelled;
                    root_action.end_ms = now_ms();
                    if (cancel_->is_paused()) throw PausedException{};
                    throw CancelledException{};
                }
                cv_done->wait_for(lk, std::chrono::milliseconds{100});
            }
        }
        worker.join();

        // Remove hook after execution
        lua::sethook(L_, nullptr, 0, 0);

        if (*err_ptr) std::rethrow_exception(*err_ptr);

        // Update Action state
        root_action.state = (result_log->status == "completed") ? StateDone : StateFailed;
        root_action.end_ms = now_ms();
        root_action.result = ActionResult{
            result_log->status == "completed",
            result_log->return_value,
        };

        return *result_log;
    }

    // Dispatch a capability call from Lua (called from trampoline)
    int dispatch_capability(lua::State* L, const char* cap_name, bool /*destructive*/) {
        // 1. Check CancellationToken
        if (cancel_ && !cancel_->is_active()) {
            lua::pushstring(L, "operation cancelled");
            return lua::error(L);
        }

        // 2. Collect Lua arguments → JSON
        std::string args_json = lua_args_to_json(L);

        // 3. Record start — add child Action
        auto step_start = now_ms();
        Action child;
        child.layer = LayerPrimitive;
        child.state = StateRunning;
        child.name = cap_name;
        child.detail = args_json;
        child.start_ms = step_start;
        current_action_->children.push_back(std::move(child));
        auto& action_ref = current_action_->children.back();

        // 4. Execute capability
        auto* cap = registry_.get(cap_name);
        if (!cap) {
            action_ref.state = StateFailed;
            action_ref.end_ms = now_ms();
            action_ref.result = ActionResult{false, R"({"error":"unknown capability"})"};
            lua::pushnil(L);
            lua::pushstring(L, "unknown capability");
            return 2;  // return nil, error_msg
        }

        std::string result_str;
        bool success = true;
        try {
            result_str = cap->execute(std::string(args_json), stream_, cancel_);
        } catch (const CancelledException&) {
            action_ref.state = StateCancelled;
            action_ref.end_ms = now_ms();
            lua::pushstring(L, "operation cancelled");
            return lua::error(L);
        } catch (const std::exception& e) {
            result_str = R"({"error":")" + std::string(e.what()) + R"("})";
            success = false;
        }

        // 5. Record completion
        auto step_end = now_ms();
        action_ref.state = success ? StateDone : StateFailed;
        action_ref.end_ms = step_end;
        action_ref.result = ActionResult{success, result_str};

        // 6. Record step log
        step_log_.push_back(StepLog{
            .fn = find_lua_name(cap_name),
            .args_json = args_json,
            .result_json = result_str,
            .duration_ms = step_end - step_start,
            .success = success,
        });

        // 7. JSON result → Lua table
        json_to_lua_table(L, result_str);
        return 1;
    }

private:
    void init_sandbox_() {
        L_ = lua::L_newstate();

        // Selectively open safe standard libraries using requiref
        lua::L_requiref(L_, "_G", lua::open_base, 1);
        lua::pop(L_, 1);
        lua::L_requiref(L_, "string", lua::open_string, 1);
        lua::pop(L_, 1);
        lua::L_requiref(L_, "table", lua::open_table, 1);
        lua::pop(L_, 1);
        lua::L_requiref(L_, "math", lua::open_math, 1);
        lua::pop(L_, 1);

        // Do NOT open: io, os, package, debug, coroutine

        // Remove dangerous functions from base
        static const char* blocked[] = {
            "dofile", "loadfile", "load", "rawget", "rawset",
            "rawequal", "rawlen", "collectgarbage"
        };
        for (auto name : blocked) {
            lua::pushnil(L_);
            lua::setglobal(L_, name);
        }

        // Register capability modules
        register_module_("pkg");
        register_module_("sys");
        register_module_("ver");
    }

    void register_module_(const char* module_name) {
        lua::newtable(L_);

        for (auto& b : bindings) {
            if (std::strcmp(b.lua_module, module_name) != 0) continue;

            // Push 3 upvalues: sandbox ptr, capability name, destructive
            lua::pushlightuserdata(L_, this);
            lua::pushstring(L_, b.capability);
            lua::pushboolean(L_, b.destructive ? 1 : 0);
            lua::pushcclosure(L_, lua_capability_trampoline_, 3);

            lua::setfield(L_, -2, b.lua_func);
        }

        lua::setglobal(L_, module_name);
    }

    // ─── Static trampolines (C function pointers, no captures) ───

    static int lua_capability_trampoline_(lua::State* L) {
        auto* sandbox = static_cast<LuaSandbox*>(lua::touserdata(L, lua::upvalueindex(1)));
        const char* cap_name = lua::tostring(L, lua::upvalueindex(2));
        bool destructive = lua::toboolean(L, lua::upvalueindex(3)) != 0;
        return sandbox->dispatch_capability(L, cap_name, destructive);
    }

    static void instruction_count_hook_(lua::State* L, lua::Debug* /*ar*/) {
        lua::getfield(L, lua::REGISTRYINDEX, "__sandbox_ptr");
        auto* sandbox = static_cast<LuaSandbox*>(lua::touserdata(L, -1));
        lua::pop(L, 1);

        if (sandbox && sandbox->cancel_ && !sandbox->cancel_->is_active()) {
            lua::pushstring(L, "operation cancelled");
            lua::error(L);
        }
    }

    static void force_stop_hook_(lua::State* L, lua::Debug* /*ar*/) {
        lua::pushstring(L, "execution interrupted");
        lua::error(L);
    }
};

} // namespace xlings::agent
