# Agent 认知架构升级 — 执行引擎详细设计

## Context

当前 agent 的 `run_one_turn` 直接硬编码了 tool calling 逻辑（LLM → tool_call → result → LLM 循环）。新架构需要抽象出 ExecutionEngine 层，让 LuaEngine 和 ToolCallingEngine 共存，共享同一套 CapabilityRegistry。

**关键约束**:
- GCC 15 模块 bug：不能用 lambda 捕获复杂跨模块类型、不能用 enum class in atomic
- 现有 `mcpplibs.capi.lua` 提供完整 Lua 5.4 C API（90+ 函数）
- 现有 `CancellationToken` 是 3-state atomic<int>
- 现有 `llm_call_worker` 模式：worker 线程 + shared_ptr flags + condition_variable

---

## 1. 核心数据结构

### 1.1 Action（替代 TreeNode + ActionNode）

```cpp
// src/agent/lua_engine.cppm

// Layer constants (int, not enum — GCC 15 safety)
static constexpr int LayerPrimitive = 0;  // 单个 capability 调用
static constexpr int LayerRoutine   = 1;  // 已学习的 Lua 函数
static constexpr int LayerDecision  = 2;  // LLM 生成的 Lua 编排
static constexpr int LayerPlan      = 3;  // 多步 Decision 组合

// State constants
static constexpr int StatePending   = 0;
static constexpr int StateRunning   = 1;
static constexpr int StateDone      = 2;
static constexpr int StateFailed    = 3;
static constexpr int StateCancelled = 4;

struct ActionResult {
    bool success;
    std::string data;  // JSON string
};

struct Action {
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
```

### 1.2 ExecutionLog（Lua 执行结果→反馈给 LLM）

```cpp
struct StepLog {
    std::string fn;          // "pkg.search"
    std::string args_json;   // {"query": "vim"}
    std::string result_json; // capability 返回值
    std::int64_t duration_ms;
    bool success;
};

struct ExecutionLog {
    std::string status;      // "completed" | "error" | "timeout" | "cancelled"
    std::vector<StepLog> steps;
    std::string return_value; // Lua return 值的 JSON 序列化
    std::string error;        // 如果 status != "completed"
    std::int64_t duration_ms;

    auto to_json() const -> std::string;  // 序列化为 JSON 字符串给 LLM 看
};
```

---

## 2. LuaSandbox 设计

### 2.1 生命周期

```
create() → register_capabilities() → execute(code) → destroy()
   │              │                        │
   │   L_newstate + 受限 env       load+pcall in worker thread
   │                                       │
   │                              hook 每个 fn call → Action 树
   └── 每次 execute 复用同一 State（保持 Routine 注册状态）
```

### 2.2 沙箱 _ENV 构建

```cpp
class LuaSandbox {
    lua::State* L_ {nullptr};
    capability::Registry& registry_;
    EventStream& stream_;
    CancellationToken* cancel_ {nullptr};

    // 执行期间的可变状态（仅在 worker 线程中访问）
    Action* current_action_ {nullptr};
    std::vector<StepLog> step_log_;
    std::int64_t exec_start_ms_ {0};

public:
    explicit LuaSandbox(capability::Registry& registry, EventStream& stream);
    ~LuaSandbox();  // lua::close(L_)

    void set_cancel(CancellationToken* cancel);
    auto execute(std::string_view lua_code, Action& root_action,
                 std::chrono::milliseconds timeout = std::chrono::milliseconds{30000})
        -> ExecutionLog;
};
```

### 2.3 受限环境初始化

```cpp
void init_sandbox() {
    L_ = lua::L_newstate();

    // 只开放安全的标准库
    lua::open_base(L_);     // print, type, pairs, ipairs, tostring, tonumber, error, pcall, select...
    lua::open_string(L_);   // string.*
    lua::open_table(L_);    // table.*
    lua::open_math(L_);     // math.*

    // 不开放: io, os, package, debug, coroutine

    // 移除 base 中的危险函数
    const char* blocked[] = {"dofile", "loadfile", "load", "rawget", "rawset",
                             "rawequal", "rawlen", "collectgarbage"};
    for (auto name : blocked) {
        lua::pushnil(L_);
        lua::setglobal(L_, name);
    }

    register_pkg_module();   // pkg = { search, install, remove, list, info }
    register_sys_module();   // sys = { status, run_command }
    register_ver_module();   // ver = { use_version }
}
```

### 2.4 Capability → Lua 函数桥接

用 Lua upvalue 存储 `LuaSandbox*` 指针解决 CFunction 签名限制。

```cpp
struct LuaBinding {
    const char* lua_module;
    const char* lua_func;
    const char* capability;
    bool destructive;
};

static constexpr LuaBinding bindings[] = {
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

static int lua_capability_trampoline(lua::State* L) {
    auto* sandbox = static_cast<LuaSandbox*>(lua::touserdata(L, lua::upvalueindex(1)));
    const char* cap_name = lua::tostring(L, lua::upvalueindex(2));
    bool destructive = lua::toboolean(L, lua::upvalueindex(3));
    return sandbox->dispatch_capability(L, cap_name, destructive);
}
```

### 2.5 dispatch_capability — 核心桥接逻辑

1. 检查 CancellationToken
2. Lua 参数 → JSON
3. 添加子 Action 到 current_action_
4. 执行 capability
5. 记录完成状态
6. 记录步骤日志
7. JSON result → Lua table 返回

### 2.6 JSON ↔ Lua 转换

| JSON | Lua | | Lua | JSON |
|------|-----|-|-----|------|
| object `{}` | table | | table (连续整数键) | array |
| array `[]` | table (1-indexed) | | table (字符串键) | object |
| string | string | | string | string |
| number (int) | integer | | number | number |
| boolean | boolean | | boolean | boolean |
| null | nil | | nil | null |

---

## 3. Worker 线程执行模型

### 3.1 execute() 实现

- 设置执行上下文 (current_action_, step_log_)
- 共享状态: done_flag, abandoned, result_log, err_ptr, cv_mtx, cv_done
- 设置 debug.sethook 每 10000 条指令检查
- Worker 线程: L_loadstring + pcall
- 主线程: cv_done->wait_for(100ms) 循环检查超时和取消

### 3.2 debug.sethook 回调

- instruction_count_hook: 每 10000 条指令检查 cancel
- force_stop_hook: 超时后立即 lua::error 中断

---

## 4. ExecutionEngine 抽象

### 4.1 方案：execute_lua 作为虚拟工具

类似现有 `manage_tree`，在 loop.cppm 中拦截 `execute_lua` tool call。

```cpp
auto execute_lua_tool_def() -> llm::ToolDef;
```

**优势**:
- 不改变 LLM 交互协议
- LLM 自主选择 tool calling 还是 Lua 编排
- 渐进式迁移，不破坏现有功能

### 4.2 loop.cppm 修改

在 tool call 执行循环中拦截 `execute_lua`:
- 解析 code 参数
- 创建 Decision layer Action
- 调用 lua_sandbox->execute()
- 返回 ExecutionLog JSON 给 LLM

---

## 5. System Prompt — Lua API 文档注入

在 `build_system_prompt()` 中注入 Lua API 文档，包含:
- 可用 API 列表 (pkg.*, sys.*, ver.*)
- 使用时机指南 (execute_lua vs individual tools)
- 示例代码

---

## 6. 文件清单与修改范围

| 文件 | 操作 | 说明 |
|------|------|------|
| `src/agent/lua_engine.cppm` | **新建** | LuaSandbox, Action, ExecutionLog, JSON↔Lua 转换 |
| `src/agent/loop.cppm` | **修改** | 添加 execute_lua 虚拟工具拦截 |
| `src/agent/agent.cppm` | **修改** | 添加 `export import xlings.agent.lua_engine` |
| `src/agent/prompt_builder.cppm` | **修改** | 注入 Lua API 文档到 system prompt |
| `src/cli.cppm` | **修改** | 创建 LuaSandbox 实例, 传入 loop |

---

## 7. 线程安全

- `LuaSandbox` 可变状态仅在 worker 线程中访问
- 主线程通过 `cancel_` (atomic) 和 `debug.sethook` 与 worker 通信
- `ExecutionLog` 通过 `shared_ptr` 安全传递
- `lua::State*` 不跨线程共享（worker 线程独占）

---

## 8. 审批机制

Phase 1 暂不实现细粒度审批。destructive 操作使用预审批模式（后续 Phase 2 细化）。

---

## Verification

1. `rm -rf build && xmake build` — 编译通过
2. 单元测试：LuaSandbox 基本执行、JSON↔Lua 转换、超时中断、取消中断
3. 手动测试：在 agent 中输入触发 execute_lua 的请求
