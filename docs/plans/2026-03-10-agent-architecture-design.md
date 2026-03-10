# xlings Agent 架构设计

> Version: 1.2 | Date: 2026-03-10 | Status: Draft

## 1. 设计目标

将 xlings 从 "CLI 工具" 演进为 "能力可编程平台"。Agent 模块是这个平台的核心消费者——通过 LLM 驱动，自主调用 xlings 能力完成用户任务。

**演进路线：** xlings 的核心理念是「万物皆可成包」。当包即是工具时，包生态的规模直接决定了 agent 能力的上限。xlings agent 初期是**包管理/环境构建领域专家**，但随着包生态的扩展，每个包带来新工具，agent 的能力域自然膨胀——最终演化为基于庞大包/工具生态的**通用 agent（xagent）**。这不是重写，而是同一架构的自然涌现。

**核心原则：**

- Core 产出统一事件流，Agent 是事件流的消费者之一
- `.agents/` 目录 = Agent 的核心标识（类比 `.git/` 之于 repo）
- Soul（灵魂）定义 Agent 的身份边界，不可篡改
- 安装即扩展——每个包自身就是一组 tool，安装/卸载 = 能力动态增减
- 一切皆 Resource——包、工具、记忆、环境统一抽象，LLM 自主探索
- 可独立发布的模块遵循通用规范，不绑定 xlings 业务

### 1.1 `.agents/` 作为 Agent 核心标识

`.agents/` 目录是一个 agent 实例的完整身份。拥有该目录 = 拥有该 agent。

**身份链：**
```
.agents/
  soul.seed.json    ← 身份锚点（不可变 ID + 宪法）
  profile.json      ← 当前状态（可变，从 soul 派生）
  memory/           ← 积累的经验（agent 的"成长"）
  sessions/         ← 交互历史
  journal/          ← 行为记录（可审计）
```

**特性：**
- **可移植**：拷贝 `.agents/` 到另一台机器，agent 带着记忆、性格、会话全部恢复
- **可审计**：journal/ 记录所有行为，可追溯
- **可销毁**：删除 `.agents/` = 销毁该 agent 实例
- **soul.seed.json 中的 `id` 是全局唯一标识**，整个 `.agents/` 是该 id 的物理实体

**规范与特化分离：**

目录结构本身是 100% 通用的（`mcpplibs.agentfs` 规范）。xlings 的特化全部在文件**内容**层面：
- `soul.seed.json` 的 boundaries 填 xlings 能力名
- `prompt/core.md` 描述 xlings 工具和行为
- `skills/builtin/` 引用 xlings 的 tool 名

`agentfs` 规范定义结构和协议，不定义具体业务内容。

## 2. 模块总览

### 2.1 独立可发布模块（mcpplibs 级别）

这些模块设计为通用基础设施，不依赖 xlings 业务逻辑，未来可独立发布到 mcpplibs：

| 模块 | 定位 | 发布名 |
|------|------|--------|
| `agentfs` | Agent 活文件系统规范与读写 | `mcpplibs.agentfs` |
| `soul` | Agent 身份 + 能力边界 + 人格系统 | `mcpplibs.soul` |
| `semantic-memory` | 向量化记忆存储与语义检索 | `mcpplibs.semantic-memory` |
| `agent-skill` | Skill 定义、加载、匹配引擎 | `mcpplibs.agent-skill` |
| `agent-journal` | 结构化审计日志（追加写） | `mcpplibs.agent-journal` |
| `mcp-server` | MCP 协议 server 框架（stdio + HTTP） | `mcpplibs.mcp-server` |
| `flock` | 跨平台文件锁原语 | `mcpplibs.flock` |
| `resource-lock` | 命名资源级锁管理器 | `mcpplibs.resource-lock` |

### 2.2 xlings 集成模块

这些模块依赖 xlings 业务，不独立发布：

| 模块 | 子目录 | 定位 |
|------|--------|------|
| `agent.fs.agentfs` | fs/ | `.agents/` 目录管理与原子读写 |
| `agent.fs.soul` | fs/ | 灵魂系统（身份 + 能力边界） |
| `agent.fs.journal` | fs/ | 审计日志（追加写 JSONL） |
| `agent.llm.loop` | llm/ | LLM ↔ Tool 核心循环 |
| `agent.llm.tool_bridge` | llm/ | 三路聚合（内置 + 包工具 + MCP）→ 统一 ToolDef |
| `agent.llm.resource_cache` | llm/ | 统一资源缓存引擎（T0/T1/T2 分层 + Resource Index） |
| `agent.llm.approval` | llm/ | Tool call 审批（Soul 驱动） |
| `agent.llm.llm_config` | llm/ | LLM provider/model/profiles 三级配置 |
| `agent.llm.prompt_builder` | llm/ | 4 层 system prompt 组装 |
| `agent.knowledge.memory` | knowledge/ | 语义记忆存储与检索 |
| `agent.knowledge.embedding` | knowledge/ | 向量化引擎 |
| `agent.knowledge.skill` | knowledge/ | Skill 加载/匹配/管理 |
| `agent.knowledge.session` | knowledge/ | 对话会话持久化 |
| `agent.mcp.client` | mcp/ | MCP client（调用外部 MCP server） |
| `agent.mcp.config` | mcp/ | mcps/ 配置加载与管理 |

### 2.3 分层架构图

```
┌──────────────────────────────────────────────────────────────────┐
│ L3: Frontends                                                    │
│  ┌──────────┐  ┌───────────────────────┐  ┌────────────────────┐ │
│  │ cli.cppm │  │ agent TUI (ftxui)     │  │ mcp server         │ │
│  │          │  │ (底部输入+滚动输出)     │  │ (uses libs/mcp_*)  │ │
│  └────┬─────┘  └──────────┬────────────┘  └──────┬─────────────┘ │
│       │                   │                       │               │
├───────┼───────────────────┼───────────────────────┼───────────────┤
│ L2: Business Logic                                                │
│                                                                   │
│  ┌────┴──────┐  ┌─────────┴──────────────────────────────────┐   │
│  │  core/    │  │  agent/                                    │   │
│  │ xim xvm  │  │                                            │   │
│  │ xself    │  │  ┌─ llm/ ─────────────────────────────────┐│   │
│  │ subos    │  │  │ loop ← tool_bridge ← approval          ││   │
│  │ config   │  │  │        prompt_builder   llm_config      ││   │
│  │ ...      │  │  └──┬─────────────────────────────────────┘│   │
│  │          │  │     │ uses libs/*                           │   │
│  │          │  │  session                                    │   │
│  │          │  │                                             │   │
│  │          │  │  ┌─ mcp/ ────────┐                          │   │
│  │          │  │  │ client config │ ──MCP──→ 外部 server     │   │
│  │          │  │  └───────────────┘                          │   │
│  └────┬─────┘  └─────────────┬───────────────────────────────┘   │
│       │                      │                                    │
├───────┼──────────────────────┼────────────────────────────────────┤
│ L1: Runtime                                                       │
│  ┌──────────────┐  ┌────────────┐  ┌──────────┐                  │
│  │ event_stream │  │ capability │  │ task     │                  │
│  │ event        │  │ (registry) │  │ (manager)│                  │
│  └──────────────┘  └────────────┘  └──────────┘                  │
├──────────────────────────────────────────────────────────────────┤
│ L0: Platform + Libs                                               │
│                                                                   │
│  ┌─ 已有 ─────────────────────────────────────────────────────┐  │
│  │ platform (linux/macos/windows)  json  tinyhttps            │  │
│  └────────────────────────────────────────────────────────────┘  │
│                                                                   │
│  ┌─ 通用 agent 基础设施（libs/ 下，未来独立发布 mcpplibs）────┐  │
│  │ flock            跨平台文件锁                              │  │
│  │ resource_lock    命名资源锁管理器                          │  │
│  │ agentfs          .agents/ 活文件系统规范                   │  │
│  │ soul             agent 身份 + 能力边界                     │  │
│  │ semantic_memory  向量化记忆 + 语义检索                     │  │
│  │ agent_skill      skill 定义/加载/匹配                     │  │
│  │ agent_journal    审计日志                                  │  │
│  │ mcp_server       MCP 协议框架 (stdio + HTTP)              │  │
│  └────────────────────────────────────────────────────────────┘  │
├──────────────────────────────────────────────────────────────────┤
│ External Libs                                                     │
│  ┌─────────┐                                                      │
│  │ llmapi  │  LLM 客户端 (OpenAI/Anthropic/DeepSeek)             │
│  └─────────┘                                                      │
└──────────────────────────────────────────────────────────────────┘
```

### 2.4 源码目录

```
src/
├── platform/
│   ├── linux.cppm              # L0: Linux 平台抽象
│   ├── macos.cppm              # L0: macOS 平台抽象
│   └── windows.cppm            # L0: Windows 平台抽象
│
├── libs/                       # L0: 第三方封装 + 通用可独立发布模块
│   ├── json.cppm               #   已有: nlohmann/json 封装
│   ├── tinyhttps.cppm          #   已有: HTTP/TLS 封装
│   │
│   │── 以下为通用 agent 基础设施（未来独立发布到 mcpplibs）──
│   ├── flock/                  #   跨平台文件锁原语
│   │   └── flock.cppm          #     → mcpplibs.flock
│   ├── resource_lock/          #   命名资源级锁管理器
│   │   └── resource_lock.cppm  #     → mcpplibs.resource-lock
│   ├── agentfs/                #   .agents/ 活文件系统规范
│   │   └── agentfs.cppm        #     → mcpplibs.agentfs
│   ├── soul/                   #   agent 身份 + 能力边界
│   │   └── soul.cppm           #     → mcpplibs.soul
│   ├── semantic_memory/        #   向量化记忆存储与检索
│   │   ├── memory.cppm         #     → mcpplibs.semantic-memory
│   │   └── embedding.cppm
│   ├── agent_skill/            #   skill 定义、加载、匹配
│   │   └── skill.cppm          #     → mcpplibs.agent-skill
│   ├── agent_journal/          #   结构化审计日志
│   │   └── journal.cppm        #     → mcpplibs.agent-journal
│   └── mcp_server/             #   MCP 协议 server 框架
│       ├── server.cppm         #     → mcpplibs.mcp-server
│       ├── stdio_transport.cppm
│       └── http_transport.cppm
│
├── runtime/
│   ├── event.cppm              # L1: 事件类型（6 种 std::variant）
│   ├── event_stream.cppm       # L1: 事件流（emit/prompt/respond）
│   ├── capability.cppm         # L1: 能力接口 + Registry
│   └── task.cppm               # L1: TaskManager（异步执行）
│
├── core/                       # L2: 业务逻辑
│   ├── config.cppm             #   全局配置
│   ├── log.cppm                #   日志
│   ├── common.cppm, utils.cppm, i18n.cppm, profile.cppm
│   ├── cmdprocessor.cppm       #   命令处理器
│   ├── subos.cppm              #   子操作系统
│   ├── xself.cppm              #   自管理（init/install）
│   ├── xim/                    #   包管理
│   │   ├── commands.cppm, catalog.cppm, downloader.cppm
│   │   ├── index.cppm, installer.cppm, repo.cppm, resolver.cppm
│   │   └── libxpkg/types/      #   xpkg 类型
│   └── xvm/                    #   版本管理
│       ├── commands.cppm, db.cppm, shim.cppm, types.cppm
│
├── agent/                      # L2: Agent 模块（xlings 集成层）
│   ├── agent.cppm              #   re-export 子模块
│   │
│   ├── llm/                    #   LLM 交互循环
│   │   ├── loop.cppm           #     核心循环（LLM ↔ Tool）
│   │   ├── tool_bridge.cppm    #     三路聚合（内置 + 包工具 + MCP）
│   │   ├── resource_cache.cppm #     统一资源缓存 + Resource Index
│   │   ├── approval.cppm       #     审批策略（Soul 驱动）
│   │   ├── llm_config.cppm     #     LLM profiles 三级配置
│   │   └── prompt_builder.cppm #     4 层 prompt 组装
│   │
│   ├── session.cppm            #   对话会话持久化
│   │
│   └── mcp/                    #   MCP client（调用外部服务）
│       ├── client.cppm         #     MCP client 实现
│       └── config.cppm         #     mcps/ 配置加载
│
├── ui/                         # L3: TUI 前端
│   ├── theme.cppm, progress.cppm, table.cppm
│   ├── selector.cppm, banner.cppm, info_panel.cppm
├── cli.cppm                    # L3: CLI 入口 + 子命令路由
└── capabilities.cppm           # L2: 8 个 Capability 实现
```

### 2.5 模块依赖关系

```
libs/ (通用，未来独立发布)        agent/ (xlings 集成)
┌────────────────────────┐      ┌──────────────────────────┐
│ flock                  │      │                          │
│   ↑                    │      │  llm/                    │
│ resource_lock          │      │    loop                  │
│   ↑                    │      │      ├─ uses: agentfs,   │
│ agentfs ←──────────────┼──────┼──    │  soul, memory,    │
│   ↑        ↑           │      │      │  skill, session,  │
│ soul    journal        │      │      │  journal          │
│   ↑        ↑           │      │    tool_bridge           │
│ semantic_memory        │      │      └─ uses: mcp/client │
│   ↑                    │      │    approval              │
│ agent_skill            │      │      └─ uses: soul       │
│                        │      │    llm_config            │
│ mcp_server             │      │    prompt_builder        │
│ agent_journal          │      │                          │
└────────────────────────┘      │  session                 │
                                │    └─ uses: agentfs      │
         ↑ libs/ 被 agent/      │                          │
           和 src/mcp/ 引用     │  mcp/                    │
                                │    client, config        │
                                └──────────────────────────┘
```

依赖方向：`libs/` ← `agent/llm/` （通用模块被集成层调用，反之不可）

### 2.6 MCP 双向架构

Agent 同时是 MCP server（被调用）和 MCP client（调用外部）：

```
外部调用 xlings（src/mcp/ — L3 server）:
  Claude Code ──MCP──→ src/mcp/server
                         ├─→ Capability Registry（8 个 xlings 能力）
                         ├─→ AgentLoop（agent_chat 等 agent 层 tools）
                         └─→ MemoryStore（memory_recall 等 tools）

xlings 调用外部（src/agent/mcp/ — L2 client）:
  AgentLoop → tool_bridge → agent/mcp/client ──MCP──→ 外部 server
                                                       (filesystem, github, ...)
```

`tool_bridge` 统一聚合两种 tool 来源，LLM 看到一个统一 tools 列表：

```
tool_bridge.tool_definitions():
  ├── 内部 tools: Capability Registry (search_packages, install_packages, ...)
  └── 外部 tools: agent/mcp/client 从 mcps/ 配置加载 (read_file, github_pr, ...)
```

## 3. `.agents/` 活文件系统

`.agents/` 是 Agent 的本地存在。框架运行时持续读写，由 `agentfs` 模块统一管理。

### 3.1 目录规范

```
$XLINGS_HOME/.agents/
│
│── 身份 & 配置 ──
├── version.json                # schema 版本（迁移检测）
├── soul.seed.json              # 灵魂种子（只读锚点，宪法）
├── profile.json                # 运行时 profile（soul 的可变投影）
├── config.json                 # agent 全局配置（审批策略等）
├── llm.json                    # LLM 配置（provider/model/profiles）
│
│── Prompt ──
├── prompt/
│   ├── core.md                 # 核心 prompt（随 xlings 发布更新）
│   └── user.md                 # 用户自定义（永不覆盖）
│
│── Skills ──
├── skills/
│   ├── builtin/                # 内置 skills（随 xlings 更新）
│   │   └── *.yaml
│   ├── user/                   # 用户自定义 skills
│   │   └── *.yaml
│   └── state/                  # skill 执行状态
│       └── {skill-name}.json
│
│── 记忆 ──
├── memory/
│   ├── meta.json               # 索引元数据（维度、模型、条目数）
│   ├── index.bin               # 向量索引
│   └── entries/                # 记忆条目
│       └── {hash}.json
│
│── 会话 ──
├── sessions/
│   └── {session-id}.json       # llmapi save_conversation 格式
│
│── MCP ──
├── mcps/
│   ├── builtin.json            # xlings 自身 MCP server 配置
│   └── {name}.json             # 外部 MCP server 配置
│
│── 日志 ──
├── journal/
│   └── YYYY-MM-DD.jsonl        # 审计事件流（追加写）
│
│── 扩展 ──
├── ext/                        # 宿主框架扩展目录（agentfs 不管理内部结构）
│   └── {framework}/            # 按框架名隔离，内部自定义
│       └── ...                 # 宿主框架自行定义、自行负责
│
│── 运行时 ──
├── cache/                      # 缓存（可安全清除）
│   ├── resources/              # 统一资源缓存（按 kind 分目录）
│   │   ├── pkg/                #   包信息缓存
│   │   │   └── {name}.json
│   │   ├── env/                #   环境信息缓存
│   │   │   └── {key}.json
│   │   ├── tool/               #   工具 schema 缓存
│   │   │   └── {name}.json
│   │   └── idx/                #   索引摘要缓存
│   │       └── {name}.json
│   ├── embeddings/             # 向量缓存
│   │   └── {content-hash}.vec
│   └── index.json              # 缓存索引（id → meta + 路径）
├── tmp/                        # 临时文件（启动时清理）
└── .locks/                     # 资源锁（实现细节）
    └── {resource}.lock
```

**xlings agent 的扩展目录示例：**

```
ext/xlings/
├── project_context/            # agent 对当前项目的分析缓存
├── tool_preferences/           # agent 学到的 tool 调用偏好
└── plan_history/               # agent 生成的执行计划历史
```

注意：`ext/xlings/` 只放 xlings **agent** 特有的扩展数据。
xlings 本身的数据（索引、下载缓存、subos 等）仍在 `$XLINGS_HOME/data/` 下，不迁入 `.agents/`。

`ext/` 的规则：
- agentfs 只负责创建 `ext/` 目录，不管理其内部结构
- 宿主框架通过 `agentfs.ext_dir("xlings")` 获取自己的扩展路径
- 各框架用自己的名称隔离（`ext/xlings/`、`ext/myagent/`）
- 扩展目录内的数据格式、生命周期由宿主框架自行负责

### 3.2 agentfs 规范边界

`mcpplibs.agentfs` 作为通用规范，定义与不定义的边界：

**规范定义：**
- 目录结构（上述完整树）
- `version.json` schema + 迁移协议
- 各文件的 JSON/YAML schema 骨架（字段名、类型、必填/可选）
- 读写 API 协议（原子操作、锁协调、追加写）
- 生命周期（初始化、迁移、清理、builtin 更新）
- `.agents/` 的可移植性保证（拷贝即恢复）

**规范不定义（由宿主框架填充）：**
- `soul.seed.json` 的具体 persona/boundaries 值
- `prompt/core.md` 的具体内容
- `skills/builtin/` 的具体 skill 定义
- `config.json` 的业务字段
- `mcps/` 中的具体 MCP server 配置
- `ext/{framework}/` 的内部结构和数据（宿主框架自行管理）

### 3.3 version.json

```json
{
  "schema": 1,
  "created_at": "2026-03-10T12:00:00Z",
  "xlings_version": "0.4.1"
}
```

schema 变更时 agentfs 自动执行迁移。

### 3.3 生命周期

```
首次 `xlings agent`
  → agentfs.ensure_initialized()
    → 创建目录结构
    → 写入 version.json
    → 创建 soul.seed.json（默认 or 交互引导）
    → 写入 prompt/core.md（从 xlings 内置资源）
    → 复制 builtin skills

后续启动
  → agentfs.migrate_if_needed()（检查 schema 版本）
  → 更新 prompt/core.md（随 xlings 版本更新）
  → 更新 builtin skills（不覆盖 user/）
  → 清理 tmp/
```

## 4. Soul 系统

Soul 是 Agent 的身份锚点和行为宪法。

### 4.1 soul.seed.json（只读）

```json
{
  "version": 1,
  "id": "agt_a3f2b8c1",
  "created_at": "2026-03-10T12:00:00Z",

  "persona": {
    "name": "xlings-agent",
    "tone": "concise-technical",
    "language": "follow-user",
    "traits": ["cautious-with-destructive", "explain-before-act"]
  },

  "boundaries": {
    "trust_level": "confirm",
    "allowed_capabilities": ["*"],
    "denied_capabilities": [],
    "max_concurrent_tasks": 4,
    "require_approval_for": ["destructive"],
    "forbidden_actions": ["rm -rf /", "format disk"]
  },

  "scope": {
    "operating_dirs": ["$XLINGS_HOME", "$PWD"],
    "allow_network": true,
    "allow_system_commands": false
  }
}
```

### 4.2 profile.json（可变投影）

```json
{
  "soul_id": "agt_a3f2b8c1",
  "last_active": "2026-03-10T14:30:00Z",
  "active_skills": ["setup-project"],
  "learned_preferences": {
    "mirror": "CN",
    "preferred_model": "claude-sonnet-4-20250514"
  },
  "stats": {
    "session_count": 12,
    "memory_count": 47,
    "total_tool_calls": 156,
    "total_tokens_used": 284000
  }
}
```

### 4.3 Soul ↔ Approval 关系

```
soul.seed.json
  │
  ├─ boundaries.trust_level ──────→ ApprovalPolicy 基础策略
  ├─ boundaries.allowed_capabilities → 白名单过滤
  ├─ boundaries.denied_capabilities  → 黑名单过滤
  ├─ boundaries.require_approval_for → 需确认的操作类型
  ├─ boundaries.forbidden_actions ───→ 硬性禁止（不可绕过）
  │
  └─ scope.* ────────────────────→ 运行时沙箱约束
```

### 4.4 模块接口

```cpp
// mcpplibs.soul

export struct Soul {
    int version;
    std::string id;
    std::string created_at;
    Persona persona;
    Boundaries boundaries;
    Scope scope;
};

export class SoulManager {
    SoulManager(/* agentfs path */);

    auto create_default() -> Soul;
    auto load() const -> Soul;
    bool exists() const;

    // 边界检查 API（其他模块调用）
    bool is_capability_allowed(std::string_view name) const;
    bool is_action_forbidden(std::string_view action) const;
    auto trust_level() const -> std::string_view;
    auto max_concurrent_tasks() const -> int;
};
```

## 5. Agent Loop（核心循环）

### 5.1 完整流程

```
xlings agent 启动
  │
  ├─ AgentFS.ensure_initialized()
  ├─ SoulManager.load() → soul
  ├─ LlmConfig.resolve(flags, env, llm.json) → llm
  ├─ llmapi::Client(llm) → client
  ├─ ResourceCache(agentfs) → cache
  ├─ ToolBridge(registry, cache) → tools
  │    ├─ load_package_tools()     ← 扫描已安装包的 agent_tools
  │    └─ load_external_mcps()     ← 加载外部 MCP server tools
  ├─ SkillManager.load_all()
  ├─ MemoryStore(embedding_engine, agentfs)
  ├─ Session.create() or .load(id)
  ├─ Journal(agentfs)
  │
  ▼
┌─ TUI Loop ─────────────────────────────────────────────┐
│                                                         │
│  User Input                                             │
│    │                                                    │
│    ├─ SkillManager.match(input) → active_skills         │
│    ├─ MemoryStore.recall(input, top_k=5) → memories     │
│    ├─ ResourceCache.build_resource_index(input) → index  │
│    │                                                    │
│    ├─ PromptBuilder.build_system(                       │
│    │    soul, active_skills, memories,                   │
│    │    resource_index, project_context                   │
│    │  ) → system_message                                │
│    │                                                    │
│    ├─ Session.add_message(user_msg)                     │
│    ├─ Journal.log_llm_turn("user", input)               │
│    │                                                    │
│    ▼                                                    │
│  llmapi::Client.chat_stream(                            │
│    session.messages(), tools, stream_cb                  │
│  )                                                      │
│    │                                                    │
│    ├─ stream_cb: chunk → TUI 实时渲染                   │
│    │                                                    │
│    ├─ StopReason::EndTurn                               │
│    │   ├─ Session.add_message(assistant_msg)            │
│    │   ├─ Journal.log_llm_turn("assistant", text)       │
│    │   ├─ 提取记忆候选 → MemoryStore.remember()         │
│    │   ├─ Profile.update_stats()                        │
│    │   ├─ Session.save()                                │
│    │   └─ → 等待下一轮输入                              │
│    │                                                    │
│    └─ StopReason::ToolUse                               │
│        │                                                │
│        for each ToolCall:                               │
│        │                                                │
│        ├─ ApprovalPolicy.check(spec, params)            │
│        │   ├─ Denied                                    │
│        │   │   └─ 注入拒绝 ToolResult → 继续循环        │
│        │   ├─ NeedConfirm                               │
│        │   │   ├─ TUI 展示 tool call 详情               │
│        │   │   ├─ User [Y] → Approved                   │
│        │   │   └─ User [n] → 注入拒绝 ToolResult        │
│        │   └─ Approved                                  │
│        │       ▼                                        │
│        ├─ locks.acquire("pkg.<name>")                   │
│        ├─ Journal.log_tool_call(name, params, approved) │
│        │                                                │
│        ├─ ToolBridge.execute(call, stream)               │
│        │   ├─ 创建 per-call EventStream                 │
│        │   ├─ 事件采样（Progress 20%间隔）               │
│        │   ├─ TUI 内联进度展示                          │
│        │   └─ 返回 ToolResultContent                    │
│        │                                                │
│        ├─ locks.release()                               │
│        ├─ Journal.log_tool_result(name, result)         │
│        ├─ 注入 ToolResult → Session                     │
│        └─ → 继续循环（re-send to LLM）                  │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

### 5.2 事件采样策略

Agent 消费 EventStream 事件时不全量注入 LLM context，按策略采样：

| 事件类型 | 策略 | 原因 |
|---------|------|------|
| ProgressEvent | 每 20% 阈值采样 | 节省 token |
| LogEvent(debug) | 全量保留 | LLM 诊断 |
| LogEvent(info+) | 优先注入 | 关键信息 |
| PromptEvent | 按 approval 策略决定 | 半自主/全自主 |
| DataEvent | 解析 JSON 全量注入 | 结构化数据 |
| ErrorEvent | 全量注入 | LLM 判断 retry/abort |
| CompletedEvent | 全量注入 | 任务完成信号 |

### 5.3 模块接口

```cpp
// xlings.agent.loop

export struct AgentConfig {
    LlmConfig llm;
    Soul soul;
    std::string session_id;       // 空 = 新建
    bool streaming { true };
};

export class AgentLoop {
    AgentLoop(
        AgentConfig config,
        capability::Registry& registry,
        TaskManager& tasks,
        AgentFS& fs
    );

    // TUI 交互模式（阻塞）
    auto run_interactive() -> int;

    // 单轮对话（MCP 用）
    auto chat(std::string_view message) -> ChatResult;

    // 会话访问
    auto session() const -> const Session&;
    void reset_session();
};

export struct ChatResult {
    std::string reply;                    // assistant 文本回复
    std::vector<ToolExecution> tools;     // 执行过的 tool calls
    bool has_pending_prompt;              // 是否有未响应的 prompt
};

export struct ToolExecution {
    std::string name;
    std::string params;
    ApprovalResult approval;
    std::string result;
    std::vector<Event> events;            // 采样后的事件
};
```

## 6. Tool Bridge 与统一资源系统

### 6.1 设计理念

**一切皆 Resource**——包信息、工具定义、记忆条目、环境信息等统一抽象为 Resource，通过一致的缓存语义和 LLM 可见性分层管理。LLM 持有摘要索引，按需深入探索。

**安装即扩展**——每个已安装的包自身就是一组 tool。安装一个包 = 给 agent 装一个能力插件，卸载 = 收缩能力。包作者 = agent 能力的贡献者。

### 6.2 统一资源抽象

```cpp
export enum class ResourceKind {
    Package,      // 包信息（索引、详情、依赖树）
    Tool,         // 工具定义（schema、来源、用法）
    Memory,       // 记忆条目（语义索引、内容）
    Environment,  // 环境信息（系统、已安装、路径）
    Session,      // 会话历史（摘要、关键决策）
    Skill,        // Skill 定义（触发条件、行为）
    Index,        // 包索引（仓库列表、更新时间）
};

export struct ResourceMeta {
    std::string     id;             // "pkg:python", "tool:install_package", "mem:a3f2b8"
    ResourceKind    kind;
    std::string     summary;        // 一句话摘要（LLM 始终可见）
    std::string     cached_at;
    int             ttl_seconds;    // 过期时间
    int             age_seconds;    // 当前年龄
    int             size_tokens;    // 完整内容的 token 估算
    bool            stale;          // age > ttl
    int             access_count;   // 被 LLM 访问次数（热度）
};
```

### 6.3 工具三层可见性（T0/T1/T2）

```
┌─────────────────────────────────────────────────────┐
│ T0: 始终可见（每次 LLM 调用都携带完整 schema）       │
│     ≤10 个，控制 token 开销                          │
├─────────────────────────────────────────────────────┤
│ T1: 摘要可见（LLM 看到名字+一句话，load 后展开）     │
│     LLM 知道存在，按需 load_resource 获取 schema    │
├─────────────────────────────────────────────────────┤
│ T2: 可搜索（LLM 通过 search_resources 发现）         │
│     LLM 不直接看到，搜索后提升到 T1/T0              │
└─────────────────────────────────────────────────────┘
```

**T0 — 始终可见 tools（10 个）：**

| Tool | 说明 |
|------|------|
| `search_resources` | 跨类型语义搜索（包、工具、记忆、环境） |
| `load_resource` | 按 id 加载资源详情（支持 refresh 控制） |
| `install_package` | 安装包（支持版本约束） |
| `uninstall_package` | 卸载包 |
| `switch_version` | 切换包版本 |
| `list_versions` | 列出包的可用版本 |
| `check_environment` | 检查环境是否满足需求 |
| `save_memory` | 保存经验到记忆 |
| `execute_plan` | 执行多步计划（plan-and-execute 模式） |
| `ask_user` | 需要用户确认/输入时调用 |

`search_resources` + `load_resource` 统一替代了分散的 `search_packages`、`search_tools`、`recall_memory`、`system_info`、`package_info`。

**T1 — 摘要可见 tools：**
- `configure_package`、`export_environment`、`import_environment`
- `subos_create`、`subos_switch`、`update_index`、`check_update`
- 已安装包提供的工具（见 6.5 包即工具）

**T2 — 可搜索 tools：**
- 外部 MCP server 提供的 tools（filesystem、git、github 等）
- xlings 高级操作（xpkg_validate、shim_create、mirror_switch 等）
- 已安装包提供的高级工具

### 6.4 Resource Index（每次 LLM 调用自动携带）

每次 LLM 调用前，`ResourceCache.build_resource_index()` 自动构建（~500 tokens）：

```markdown
## 当前环境
- env:os        linux x86_64              (30min ago)
- env:installed 47 packages               (5min ago)
- env:toolchain gcc-15, xmake-2.9         (30min ago)

## 包索引
- idx:main      61 packages, updated 2h ago  (stale)
- idx:sub       3 sub-indexes                (fresh)

## 相关记忆
- mem:a3f2 "用户偏好CN镜像"                (relevance:0.9)
- mem:b7c1 "python 3.12安装需要openssl"    (relevance:0.7)

## 包提供的工具 (5 packages → 14 tools)
| 包           | 工具                                 | tier |
|-------------|--------------------------------------|------|
| python@3.12 | python_run, pip_install, venv_create | T1   |
| nodejs@22   | node_run, npm_install, npx_exec      | T1   |
| cmake@3.29  | cmake_configure, cmake_build         | T1   |
| + 5 more T2 tools (search_resources 可查)             |      |

💡 load_resource(id) 加载详情 / search_resources(query) 跨类型搜索
💡 install_package 后自动获得新工具
```

LLM 看到缓存状态（stale/age），自主决定是否 refresh。

### 6.5 包即工具（Package as Tool）

每个 xpkg 包可定义 `agent_tools`，安装后 agent 自动获得对应能力：

**xpkg 规范 v2 扩展（向后兼容）：**

```lua
-- python.lua (xpkg 包定义)
package = {
    -- v1 已有（不变）
    name = "python",
    version = "3.12.1",
    xpm = { ... },
    hooks = { ... },

    -- v2 新增（可选）
    agent_tools = {
        {
            name = "python_run",
            tier = "T1",           -- T1(摘要可见) 或 T2(需搜索)，默认 T1
            description = "Execute a Python script or expression",
            input_schema = {
                type = "object",
                properties = {
                    code = { type = "string", description = "Python code to execute" },
                    file = { type = "string", description = "Path to .py file" },
                    args = { type = "array", items = { type = "string" } },
                },
            },
            handler = function(input)
                if input.code then
                    return system.exec("python3 -c " .. utils.quote(input.code))
                else
                    return system.exec("python3 " .. input.file, input.args or {})
                end
            end,
        },
        {
            name = "pip_install",
            tier = "T1",
            description = "Install Python packages via pip",
            input_schema = {
                type = "object",
                properties = {
                    packages = { type = "array", items = { type = "string" } },
                    upgrade  = { type = "boolean", default = false },
                },
                required = { "packages" },
            },
            handler = function(input)
                local cmd = "pip install"
                if input.upgrade then cmd = cmd .. " --upgrade" end
                return system.exec(cmd .. " " .. table.concat(input.packages, " "))
            end,
        },
    },

    -- 包提供的知识资源（agent 可检索）
    agent_resources = {
        { type = "doc",   path = "README.md" },
        { type = "guide", path = "usage.md" },
    },
}
```

**包工具的层级规则：**
- T0 保留给 xlings 内置，包不能注册 T0
- 包工具默认 T1（摘要可见），可设 T2（需搜索）
- 没有 `agent_tools` 字段的旧包照常工作，只是 agent 无法直接使用它

**动态能力变更流程：**

```
安装事件 → tool_bridge.on_package_change()
  ├─ 加载包的 agent_tools
  ├─ 注册到 ToolBridge
  ├─ 更新 Resource Index
  └─ 缓存 invalidate("env:installed")

卸载事件 → tool_bridge.on_package_change()
  ├─ 移除包的 agent_tools
  ├─ 更新 Resource Index
  └─ 缓存 invalidate("env:installed")

下一轮 LLM 调用自动看到新/减少的工具
```

**包工具安全约束（Soul 层）：**

```json
// soul.seed.json → boundaries.package_tools
{
  "allow_system_exec": false,
  "sandbox_mode": "restricted",
  "require_approval_for": ["system.exec", "network.fetch"],
  "max_exec_timeout": 30
}
```

包工具的 handler 调用 `system.exec` 时经过 approval 层，forbidden actions 不可绕过。

### 6.6 统一缓存引擎

```cpp
export class ResourceCache {
    // TTL 配置（每种资源的默认过期时间）
    // Package: 600s(10min), Tool: 3600s(1h), Memory: -1(永不过期)
    // Environment: 1800s(30min), Session: -1, Skill: 3600s, Index: 300s(5min)

    // 查询
    auto get(std::string_view id) -> std::optional<CacheEntry>;
    auto search(std::string_view query,
                std::span<const ResourceKind> kinds = {},
                int limit = 10) -> std::vector<ResourceMeta>;

    // 写入
    void put(std::string_view id, ResourceKind kind,
             json content, std::string_view summary);

    // 每次 LLM 调用前构建 Resource Index
    auto build_resource_index(
        std::string_view user_input,    // 用于记忆相关性排序
        int max_tokens = 800            // index 的 token 预算
    ) -> std::string;

    // 缓存管理
    void invalidate(std::string_view id);
    void invalidate_kind(ResourceKind kind);
    void evict_stale();
    auto stats() const -> CacheStats;
};

export enum class CachePolicy {
    Normal,        // 有缓存且未过期就用
    ForceRefresh,  // 忽略缓存
    CacheOnly,     // 只用缓存，没有则返回空
};
```

**缓存结果对 LLM 透明**——返回值携带缓存元信息：

```json
{
  "tool": "search_resources",
  "result": { "packages": ["python@3.12.1"] },
  "cache": {
    "hit": true,
    "cached_at": "2026-03-10T14:20:00Z",
    "ttl_seconds": 600,
    "age_seconds": 180
  }
}
```

LLM 看到 `age_seconds` 和 `stale` 状态，自主决定是否 `force_refresh`。

### 6.7 ToolBridge 映射规则

```
CapabilitySpec              →  llmapi::ToolDef (MCP tool schema)
  .name                     →    .name
  .description              →    .description
  .inputSchema              →    .inputSchema
  .destructive              →    (metadata, 审批用)

llmapi::ToolCall            →  Capability.execute()
  .name                     →    Registry.get(name)
  .arguments (JSON string)  →    Capability.execute(arguments, stream)

Capability Result           →  llmapi::ToolResultContent
  (JSON string)             →    .content
  (exit code != 0)          →    .isError = true
```

### 6.8 工具来源三路聚合

```
ToolBridge
  ├─ 内部固定: Capability Registry（xlings 内置能力）
  ├─ 包提供:   已安装包的 agent_tools（动态，随 install/uninstall 变化）
  └─ 外部 MCP: mcps/ 配置的 MCP server 提供的 tools
```

### 6.9 模块接口

```cpp
// xlings.agent.tool_bridge

export class ToolBridge {
    ToolBridge(capability::Registry& registry, ResourceCache& cache);

    // 加载动态工具来源
    void load_external_mcps(const AgentFS& fs);
    void load_package_tools(const InstalledPackages& pkgs);  // 扫描已安装包

    // 安装/卸载事件回调
    void on_package_change(const InstalledPackages& pkgs);

    // 分层获取
    auto t0_tools() const -> std::vector<llmapi::ToolDef>;    // 始终携带
    auto t1_summaries() const -> std::string;                  // prompt 摘要文本
    auto search(std::string_view query) -> std::vector<ResourceMeta>;  // T2 搜索

    // 执行（带缓存）
    auto execute(
        const llmapi::ToolCall& call,
        EventStream& stream,
        CachePolicy policy = CachePolicy::Normal
    ) -> llmapi::ToolResultContent;

    // Schema 按需加载
    auto get_schema(std::string_view name) -> llmapi::ToolDef;

    // 查询 tool 元信息
    auto tool_info(std::string_view name) const -> ToolInfo;

    struct ToolInfo {
        std::string name;
        std::string source;           // "builtin", "pkg:python", "mcp:filesystem"
        bool is_external;
        bool destructive;
        int tier;                     // 0, 1, or 2
    };
};
```

## 7. Approval（审批）

### 7.1 审批流程

```
ToolCall 到达
  │
  ├─ soul.boundaries.denied_capabilities 包含? → Denied("capability denied by soul")
  ├─ allowed_capabilities != ["*"] 且不包含?   → Denied("capability not in allowlist")
  ├─ soul.boundaries.forbidden_actions 匹配?   → Denied("action forbidden by soul")
  │
  ├─ trust_level == "readonly"
  │   └─ spec.destructive? → Denied("readonly mode")
  │
  ├─ trust_level == "confirm"
  │   ├─ spec.destructive?                    → NeedConfirm
  │   ├─ name in require_approval_for?        → NeedConfirm
  │   └─ otherwise                            → Approved
  │
  └─ trust_level == "auto"                    → Approved
```

### 7.2 模块接口

```cpp
// xlings.agent.approval

export enum class ApprovalResult { Approved, Denied, NeedConfirm };

export class ApprovalPolicy {
    ApprovalPolicy(const Soul& soul);

    auto check(
        const capability::CapabilitySpec& spec,
        const std::string& params
    ) -> ApprovalResult;

    auto denial_reason() const -> std::string_view;
};
```

## 8. Prompt Builder（4 层 Prompt）

### 8.1 层级结构

```
┌──────────────────────────────────────────────────┐
│ L1: Core Prompt + Soul                            │
│  ├─ prompt/core.md          核心指令              │
│  ├─ soul.persona            性格注入              │
│  ├─ soul.boundaries         能力边界声明          │
│  └─ T0 tool definitions     始终可见的 10 个 tool │
├──────────────────────────────────────────────────┤
│ L2: Active Skills                                 │
│  └─ 匹配的 skill.prompt 内容                     │
├──────────────────────────────────────────────────┤
│ L3: Dynamic Context                               │
│  ├─ Resource Index          环境/包/工具/索引摘要  │
│  ├─ MemoryStore.recall()    相关记忆              │
│  ├─ profile.learned_preferences                   │
│  └─ 当前项目 .xlings.json 摘要                    │
├──────────────────────────────────────────────────┤
│ L4: User Prompt                                   │
│  └─ prompt/user.md          用户自定义指令        │
└──────────────────────────────────────────────────┘
         │
         ▼
   llmapi system message（单条拼接）
```

### 8.2 模块接口

```cpp
// xlings.agent.prompt_builder

export class PromptBuilder {
    PromptBuilder(const Soul& soul, const AgentFS& fs);

    // 构建完整 system prompt
    auto build(
        const std::vector<const Skill*>& active_skills,
        const std::vector<MemoryEntry>& relevant_memories,
        const std::string& resource_index,
        const std::string& project_context
    ) -> std::string;

    // 单独获取各层（调试用）
    auto layer_core() const -> std::string;
    auto layer_skills(const std::vector<const Skill*>&) const -> std::string;
    auto layer_context(const std::vector<MemoryEntry>&, const std::string&) const -> std::string;
    auto layer_user() const -> std::string;
};
```

## 9. LLM 配置

### 9.1 三级优先级

```
最高  --model / --profile / --base-url (CLI flags)
  ↓   $XLINGS_LLM_MODEL / $XLINGS_LLM_PROVIDER / $ANTHROPIC_API_KEY (环境变量)
最低  .agents/llm.json (持久化配置)
```

### 9.2 llm.json（独立于 config.json）

LLM 配置从 `config.json` 分离为独立文件，好处：
- **职责分离** — agent 行为配置和 LLM provider 配置正交
- **敏感隔离** — API key 相关配置单独文件，更易做权限控制
- **多模型支持** — profiles 机制支持快速切换

```json
{
  "default": {
    "provider": "anthropic",
    "model": "claude-sonnet-4-20250514",
    "api_key_env": "ANTHROPIC_API_KEY",
    "base_url": null,
    "max_tokens": 8192,
    "temperature": 0.3
  },
  "profiles": {
    "fast": {
      "provider": "anthropic",
      "model": "claude-haiku-4-5-20251001",
      "max_tokens": 4096
    },
    "powerful": {
      "provider": "anthropic",
      "model": "claude-opus-4-6",
      "max_tokens": 16384
    },
    "local": {
      "provider": "openai",
      "base_url": "http://localhost:11434/v1",
      "model": "llama3",
      "api_key_env": null
    }
  }
}
```

`api_key_env` 指定环境变量名（而非明文存储 key），避免密钥泄露。

### 9.3 Provider 自动推断

```
claude-*      → anthropic (base: https://api.anthropic.com)
gpt-*         → openai    (base: https://api.openai.com)
deepseek-*    → openai    (base: https://api.deepseek.com)
其他           → openai    (需要 --base-url 或 base_url 配置)
```

### 9.4 模块接口

```cpp
// xlings.agent.llm_config

export struct LlmConfig {
    std::string provider;       // "anthropic" / "openai"
    std::string model;
    std::string api_key;
    std::string base_url;
    float temperature { 0.3 };
    int max_tokens { 8192 };
};

export struct LlmProfile {
    std::string name;           // "fast" / "powerful" / "local"
    LlmConfig config;
};

export auto resolve_config(
    int argc, char* argv[],      // CLI flags (--model, --profile)
    const AgentFS& fs            // .agents/llm.json
) -> LlmConfig;

export auto infer_provider(std::string_view model) -> std::string;
export auto list_profiles(const AgentFS& fs) -> std::vector<LlmProfile>;
```

## 10. Skill 系统

### 10.1 Skill 格式（YAML）

```yaml
name: setup-project
description: Set up a development project with required toolchains
version: "1.0"

# 行为指导（注入 agent system prompt）
prompt: |
  When setting up a project:
  1. Read .xlings.json to identify required packages
  2. Check installed packages (list_packages)
  3. Install missing packages in dependency order
  4. Verify with package_info
  5. Report summary

# 预定义步骤（agent 可参考或直接执行）
steps:
  - tool: list_packages
    params: {}
  - tool: install_packages
    params:
      targets: "{{from_config}}"
      yes: false

# 触发条件
triggers:
  - "setup"
  - "initialize project"
  - "配置项目"

# 依赖的能力
requires:
  - search_packages
  - install_packages
  - list_packages
```

### 10.2 模块接口

```cpp
// mcpplibs.agent-skill

export struct SkillStep {
    std::string tool;
    std::string params;        // JSON or template
};

export struct Skill {
    std::string name;
    std::string description;
    std::string version;
    std::string prompt;
    std::vector<SkillStep> steps;
    std::vector<std::string> triggers;
    std::vector<std::string> requires;
};

export class SkillManager {
    SkillManager(/* skills_dir path */);

    void load_all();
    void reload();

    auto get(std::string_view name) -> const Skill*;
    auto match(std::string_view user_input) -> std::vector<const Skill*>;
    auto list() -> std::vector<const Skill*>;

    void install(std::filesystem::path yaml);
    void remove(std::string_view name);

    // 生成 prompt 片段
    auto build_prompt(const std::vector<const Skill*>& active) -> std::string;
};
```

## 11. 语义记忆

### 11.1 Embedding 引擎

```cpp
// mcpplibs.semantic-memory (embedding 部分)

export class EmbeddingEngine {
    EmbeddingEngine(/* config: model, api_key, cache_dir */);

    auto embed(std::string_view text) -> std::vector<float>;
    auto embed_batch(std::span<const std::string> texts)
        -> std::vector<std::vector<float>>;

    static auto cosine_similarity(
        std::span<const float> a,
        std::span<const float> b
    ) -> float;

    auto dimension() const -> size_t;
};
```

### 11.2 记忆存储

```cpp
// mcpplibs.semantic-memory

export enum class MemoryCategory {
    Fact,           // "当前项目用 node 22.17.1"
    Preference,     // "用户偏好 CN mirror"
    Learning,       // "install gcc 需要先 update"
    Context,        // "这是 d2x 教程项目"
};

export struct MemoryEntry {
    std::string id;                     // content hash
    std::string text;
    MemoryCategory category;
    std::vector<float> embedding;
    std::string created_at;
    std::string updated_at;
    std::map<std::string, std::string> metadata;
};

export struct MemoryQuery {
    std::string text;
    size_t top_k { 5 };
    float min_similarity { 0.7 };
    std::optional<MemoryCategory> category;
};

export struct MemoryStats {
    size_t total;
    std::map<MemoryCategory, size_t> by_category;
    size_t index_size_bytes;
};

export class MemoryStore {
    MemoryStore(EmbeddingEngine& engine, /* memory_dir path */);

    auto remember(
        std::string_view text,
        MemoryCategory cat,
        std::map<std::string, std::string> meta = {}
    ) -> std::string;

    auto recall(const MemoryQuery& query) -> std::vector<MemoryEntry>;

    void forget(std::string_view id);
    void forget_category(MemoryCategory cat);

    auto stats() const -> MemoryStats;
    void rebuild_index();
};
```

### 11.3 记忆条目格式

`memory/entries/{hash}.json`:
```json
{
  "id": "a3f2b8c1",
  "text": "用户项目使用 node 22.17.1 和 gcc 15",
  "category": "fact",
  "embedding": [0.012, -0.034, ...],
  "created_at": "2026-03-10T12:00:00Z",
  "updated_at": "2026-03-10T12:00:00Z",
  "metadata": {
    "source": "session:abc123",
    "project": "/home/user/myproject"
  }
}
```

### 11.4 记忆在 Loop 中的使用

```
输入阶段: recall(user_input) → 注入 PromptBuilder L3
输出阶段: 从 assistant 回复中提取事实 → remember(text, Fact)
```

## 12. 会话管理

### 12.1 模块接口

```cpp
// xlings.agent.session

export struct SessionInfo {
    std::string id;
    std::string created_at;
    std::string last_active;
    size_t message_count;
    std::string preview;         // 最后一条消息预览
};

export class SessionManager {
    SessionManager(AgentFS& fs);

    auto create() -> Session;
    auto load(std::string_view id) -> Session;
    auto list() -> std::vector<SessionInfo>;
    void remove(std::string_view id);
    void clean(std::chrono::hours max_age = std::chrono::hours{720});
};

export class Session {
    auto id() const -> std::string_view;
    auto messages() const -> const std::vector<llmapi::Message>&;
    void add_message(llmapi::Message msg);
    void save();   // 带文件锁
};
```

## 13. Journal（审计日志）

### 13.1 日志格式

`journal/YYYY-MM-DD.jsonl`（每行一条，追加写）：

```jsonl
{"ts":"...","type":"llm_turn","role":"user","content":"install gcc","tokens":5}
{"ts":"...","type":"tool_call","tool":"install_packages","params":"{...}","approval":"confirm"}
{"ts":"...","type":"event","event_type":"ProgressEvent","phase":"downloading","percent":0.3}
{"ts":"...","type":"tool_result","tool":"install_packages","exit_code":0,"duration_ms":3200}
{"ts":"...","type":"llm_turn","role":"assistant","content":"gcc installed.","tokens":8}
{"ts":"...","type":"memory","action":"remember","id":"a3f2b8c1","category":"fact"}
```

### 13.2 模块接口

```cpp
// mcpplibs.agent-journal

export class Journal {
    Journal(/* journal_dir path, locks */);

    void log_llm_turn(std::string_view role, std::string_view content, size_t tokens);
    void log_tool_call(std::string_view tool, std::string_view params, std::string_view approval);
    void log_tool_result(std::string_view tool, int exit_code, int64_t duration_ms);
    void log_event(const Event& event);
    void log_memory(std::string_view action, std::string_view id, std::string_view category);

    // 读取（调试/分析）
    auto read_date(std::string_view date) const -> std::vector<nlohmann::json>;
    auto read_today() const -> std::vector<nlohmann::json>;
};
```

## 14. AgentFS（活文件系统管理）

### 14.1 模块接口

```cpp
// mcpplibs.agentfs

export class AgentFS {
    AgentFS(std::filesystem::path agents_root);

    // 生命周期
    void ensure_initialized();
    void migrate_if_needed();
    auto schema_version() const -> int;

    // 路径访问
    auto root() const -> path;
    auto soul_path() const -> path;
    auto profile_path() const -> path;
    auto config_path() const -> path;
    auto llm_config_path() const -> path;
    auto core_prompt_path() const -> path;
    auto user_prompt_path() const -> path;
    auto skills_dir(bool builtin) const -> path;
    auto skills_state_dir() const -> path;
    auto memory_dir() const -> path;
    auto sessions_dir() const -> path;
    auto mcps_dir() const -> path;
    auto journal_dir() const -> path;
    auto cache_dir() const -> path;
    auto tmp_dir() const -> path;
    auto locks_dir() const -> path;

    // 扩展目录（宿主框架自定义区域）
    auto ext_dir(std::string_view framework) const -> path;
    //   e.g. ext_dir("xlings") → .agents/ext/xlings/

    // 原子读写（协调并发）
    auto read_json(const path& file) const -> nlohmann::json;
    void write_json(const path& file, const nlohmann::json& data);
    void append_jsonl(const path& file, const nlohmann::json& line);

    // 缓存管理
    void clean_cache(std::chrono::hours max_age = std::chrono::hours{72});
    void clean_tmp();

    // 锁
    auto locks() -> ResourceLockManager&;

    // Builtin 资源更新（xlings 版本升级时）
    void update_builtins(
        std::string_view core_prompt,
        const std::vector<std::pair<std::string, std::string>>& builtin_skills
    );
};
```

## 15. 并发锁体系

### 15.1 两层架构

```
L0: flock（文件锁原语）
  └─ 跨平台封装：Linux/macOS flock(), Windows LockFileEx()
  └─ RAII: 构造加锁，析构解锁
  └─ 支持 shared（读）/ exclusive（写）

L1: resource_lock（资源锁管理器）
  └─ 命名锁：acquire("pkg.gcc") → .locks/pkg.gcc.lock
  └─ 共享/独占：acquire_shared() / acquire()
  └─ 非阻塞尝试：try_acquire()
  └─ stale lock 检测：进程 PID 验证
```

### 15.2 锁粒度规范

| 资源域 | 锁名称 | 模式 | 场景 |
|--------|--------|------|------|
| 包操作 | `pkg.{name}` | 独占 | install/remove 同包互斥 |
| 索引 | `index.update` | 独占 | update 互斥 |
| 下载 | `download.{hash}` | 独占 | 同文件不重复下载 |
| 会话 | `session.{id}` | 独占 | 同会话不并发写 |
| 记忆索引 | `memory.index` | 共享读/独占写 | 读并发，写互斥 |
| 日志 | `journal.{date}` | 独占 | append 互斥 |
| Profile | `profile` | 独占 | stats 更新互斥 |

### 15.3 并发场景

```
进程 A: xlings agent        进程 B: xlings install node
  │                            │
  ├ acquire("pkg.gcc")         ├ acquire("pkg.node")
  │ ← 不冲突，并行             │ ← 不冲突，并行
  │ install gcc                │ install node
  │ release                    │ release
  │                            │
  ├ acquire("pkg.node")        │
  │ ← 等待 B 释放              │
  │ ...                        │
```

```
进程 A: xlings agent        进程 B: xlings agent
  │                            │
  ├ acquire_shared             ├ acquire_shared
  │ ("memory.index")           │ ("memory.index")
  │ ← 并发读取                 │ ← 并发读取
  │ recall(...)                │ recall(...)
  │ release_shared             │ release_shared
  │                            │
  ├ acquire("memory.index")    │
  │ ← 独占写入                 │
  │ remember(...)              ├ acquire("memory.index")
  │ release                    │ ← 等待 A 释放
  │                            │ remember(...)
```

## 16. MCP Server

### 16.1 Tools 分层

**统一资源 Tools（供外部 MCP 调用者使用）：**

| Tool | 来源 |
|------|------|
| `search_resources` | ResourceCache 跨类型搜索 |
| `load_resource` | ResourceCache 按 id 加载 |
| `install_package` | xim::cmd_install |
| `uninstall_package` | xim::cmd_remove |
| `switch_version` | xvm::cmd_use / cmd_list_versions |
| `check_environment` | Config 内省 + 环境检测 |

**Agent Tools（agent 层交互）：**

| Tool | 功能 |
|------|------|
| `agent_chat` | 发送消息，返回 agent 回复 + tool 执行过程 |
| `agent_session_create` | 创建新会话 |
| `agent_session_load` | 恢复指定会话 |
| `agent_session_list` | 列出所有会话 |

**Memory Tools：**

| Tool | 功能 |
|------|------|
| `memory_remember` | 存入记忆 |
| `memory_recall` | 语义检索 |
| `memory_forget` | 删除记忆 |
| `memory_stats` | 记忆统计 |

**Task Tools：**

| Tool | 功能 |
|------|------|
| `task_status` | 查询活跃任务 |
| `task_events` | 增量获取事件 |
| `task_respond` | 响应 PromptEvent |

### 16.2 传输

```
xlings agent mcp                → stdio（默认，Claude Code/Desktop）
xlings agent mcp --http         → HTTP+SSE（远程调用）
xlings agent mcp --http --port 8080
```

### 16.3 模块接口

```cpp
// mcpplibs.mcp-server

export class McpServer {
    McpServer(/* tool registry */);

    // 注册 tool
    void add_tool(McpToolDef def, McpToolHandler handler);

    // 启动
    void serve_stdio();
    void serve_http(int port = 3000);
};

// xlings.mcp.server（xlings 集成层）
export auto create_mcp_server(
    capability::Registry& registry,
    AgentLoop& agent,
    MemoryStore& memory,
    TaskManager& tasks
) -> McpServer;
```

## 17. CLI 命令

```
xlings agent                              # TUI 交互（默认）
xlings agent --model <model>              # 指定模型
xlings agent -y                           # auto trust
xlings agent --readonly                   # 只读模式
xlings agent --session <id>               # 恢复会话
xlings agent mcp                          # MCP stdio
xlings agent mcp --http [--port 8080]     # MCP HTTP
```

## 18. TUI 界面

```
┌───────────────────────────────────────────────┐
│ xlings agent | claude-sonnet-4-20250514              │
├───────────────────────────────────────────────┤
│                                               │
│ You: install gcc and node                     │
│                                               │
│ Agent: I'll install both packages.            │
│                                               │
│ ┌ 🔧 install_packages                        │
│ │ targets: ["gcc", "node"]                    │
│ │ ⚠ destructive — approve? [Y/n] █           │
│ └─────────────────────────────────────────    │
│                                               │
│ ┌ 🔧 install_packages ✓                      │
│ │ ██████████████████ 100% done                │
│ │ gcc@15.1.0, node@22.17.1 installed         │
│ └─────────────────────────────────────────    │
│                                               │
│ Agent: Both packages installed successfully.  │
│                                               │
├───────────────────────────────────────────────┤
│ > _                                           │
└───────────────────────────────────────────────┘
```

## 19. 独立模块发布规划

以下模块设计为不依赖 xlings 业务逻辑，可独立发布到 mcpplibs：

### mcpplibs.agentfs
- `.agents/` 目录规范：目录结构、version schema、迁移机制
- 原子读写、路径管理、缓存/tmp 清理
- **价值：** 统一 agent 本地存储规范，任何 C++ agent 框架可复用

### mcpplibs.soul
- Soul seed 格式：persona + boundaries + scope
- SoulManager：加载、验证、边界检查 API
- **价值：** agent 身份与安全边界的标准化定义

### mcpplibs.semantic-memory
- EmbeddingEngine：向量化 + 缓存 + 批量
- MemoryStore：存入、语义检索、去重、分类
- 向量索引（内存 brute-force，未来可选 HNSW）
- **价值：** 轻量 C++ 语义记忆库，不依赖外部向量数据库

### mcpplibs.agent-skill
- Skill YAML 格式规范：prompt + steps + triggers + requires
- SkillManager：加载、匹配、builtin/user 分离
- **价值：** agent skill 的标准化定义与管理

### mcpplibs.agent-journal
- JSONL 审计日志：类型化条目、追加写、按日分片
- Journal：日志写入、读取、分析
- **价值：** agent 可审计性的标准实现

### mcpplibs.mcp-server
- MCP 协议 JSON-RPC 处理
- stdio + HTTP+SSE 双传输
- Tool 注册与分发
- **价值：** C++23 MCP server 框架，目前生态空白

### mcpplibs.flock
- 跨平台文件锁（Linux flock / macOS flock / Windows LockFileEx）
- RAII guard、shared/exclusive、非阻塞 try
- **价值：** C++23 模块化文件锁，替代 platform-specific 代码

### mcpplibs.resource-lock
- 命名资源锁管理器（基于 flock）
- 共享读/独占写、stale PID 检测
- **价值：** 多进程资源协调的通用方案

## 20. 演进愿景：从包管理专家到通用 Agent

### 20.1 核心逻辑链

```
万物皆可成包 → 包即是工具 → 包生态规模 = agent 能力上限
```

xlings 的独特定位不是「又一个 agent 框架」，而是**工具生态驱动的 agent 平台**。其他 agent 框架的能力扩展靠开发者写插件/tool，xlings agent 的能力扩展靠**社区贡献包**——门槛更低、规模更大、复利更强。

### 20.2 三阶段演进

```
Phase 1: 包管理专家（xlings agent）
  │
  │  内置 10 个 T0 tools + xim/xvm 能力
  │  能力域：安装、卸载、版本切换、环境检测
  │  用户价值："帮我搭建 Python ML 环境"
  │
  ▼ 包生态增长（61 → 500+ xpkg）
  │ 每个包携带 agent_tools + agent_resources
  │
Phase 2: 领域工具专家
  │
  │  安装 python → 获得 python_run, pip_install, venv_create
  │  安装 docker → 获得 docker_run, docker_build, compose_up
  │  安装 cmake  → 获得 cmake_configure, cmake_build
  │  安装 latex  → 获得 latex_compile, bibtex_run
  │  ...
  │
  │  能力域：环境构建 + 工具链使用 + 项目配置
  │  用户价值："帮我编译这个 C++ 项目并修复构建错误"
  │
  ▼ 包生态爆发（500 → 5000+ xpkg）
  │ 涵盖开发、运维、数据、文档、设计等领域
  │
Phase 3: 通用 Agent（xagent）
  │
  │  安装 ffmpeg    → 获得 video_convert, audio_extract
  │  安装 imagemagick → 获得 image_resize, image_convert
  │  安装 pandoc    → 获得 doc_convert, markdown_to_pdf
  │  安装 k8s-tools → 获得 kubectl_apply, helm_install
  │  安装 db-tools  → 获得 pg_query, redis_get
  │  ...
  │
  │  能力域：几乎无限——取决于包生态覆盖范围
  │  用户价值："帮我把这个视频转成 GIF 并部署到服务器"
  │
  │  此时 xagent 不是一个预置了所有能力的巨型 agent，
  │  而是一个能自主判断"我需要什么工具"→ 搜索包索引 →
  │  安装 → 获得能力 → 使用 → 完成任务的自适应 agent。
```

### 20.3 自适应能力获取

Phase 3 的关键能力——**agent 自主安装工具**：

```
用户: "帮我把 report.md 转成 PDF"

Agent 思考:
  1. search_resources("markdown to pdf") → 无匹配工具
  2. search_resources("pandoc", kinds=["Package"]) → pkg:pandoc (未安装)
  3. load_resource("pkg:pandoc") → 详情：支持 md→pdf，提供 doc_convert tool
  4. "我需要安装 pandoc 来完成这个任务"
  5. install_package("pandoc") → 安装成功
     → tool_bridge 自动注册 pandoc 的 agent_tools
  6. doc_convert(input="report.md", format="pdf") → 完成
```

agent 不需要预先知道所有工具——它通过**搜索包索引**发现能力缺口，通过**安装包**填补缺口。包索引就是 agent 的「能力目录」。

### 20.4 架构保证

当前架构已为这个演进做好准备：

| 机制 | 如何支撑演进 |
|------|------------|
| `agent_tools` in xpkg | 包作者零成本为 agent 贡献能力 |
| T0/T1/T2 分层 | 工具数从 10 到 10000 都不会淹没 LLM context |
| `search_resources` | agent 自主发现能力缺口 |
| Resource Index | 动态摘要，不受工具总量限制 |
| 统一缓存 | 10000 个包的工具 schema 不会每次重新加载 |
| Soul boundaries | 能力膨胀时安全边界不失控 |
| `agent_resources` | 包自带文档，agent 自学新工具用法 |

### 20.5 飞轮效应

```
更多包 → agent 能力更强 → 更多用户 → 更多包贡献者 → 更多包
                ↑                                      │
                └──────────────────────────────────────┘
```

xlings 的竞争壁垒不在 agent 框架本身（框架可以被复制），而在**包生态**（生态无法被复制）。`agent_tools` 让包的价值从「人用」扩展到「人+AI 用」，为包贡献者提供了新的动力。

### 20.6 命名演进

```
当前:  xlings agent         → 包管理专家
未来:  xlings agent / xagent → 通用 AI agent
       xlings                → xagent 的能力供给平台
```

`xagent` 可作为 `xlings agent` 的别名或独立入口，但底层共享同一套架构。不需要重写——当包生态足够丰富时，agent 自然「涌现」为通用 agent。
