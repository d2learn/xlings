# xlings C++23 迁移 — 设计方案导航

> **项目**: [xlings](https://github.com/d2learn/xlings) | **目标版本**: 0.2.0 | **语言标准**: C++23 Modules
>
> **状态**: 设计完成，实施阶段

---

## 文档地图

```
docs/mcpp-version/
├── README.md                 ← 本文件（导航入口）
│
├── 设计文档（6 份）
│   ├── main.md               ← 顶层设计方案（必读）
│   ├── env-analysis.md       ← 现有架构深度分析
│   ├── env-design.md         ← 多环境管理与业界对比
│   ├── env-store-design.md   ← Store/Profile/世代设计
│   ├── rpath-and-os-vision.md← RPATH 解决方案 + OS 演进
│   └── pkg-taxonomy.md       ← 包分类体系设计
│
└── tasks/                    ← 任务拆分（10 个可并行任务）
    ├── README.md             ← 任务总览 + 依赖拓扑 + 并行分组
    ├── T01-platform-exe-path.md
    ├── T02-config-multienv.md
    ├── T03-env-module.md
    ├── T04-profile-module.md
    ├── T05-cmdprocessor-update.md
    ├── T06-xvm-var-expand.md
    ├── T07-xim-pkgdir.md
    ├── T08-self-migrate.md
    ├── T09-ci-multiplatform.md
    └── T10-pkg-taxonomy-impl.md
```

---

## 设计文档概览

### [main.md](main.md) — 顶层设计方案（必读）

涵盖迁移范围、整体架构、C++23 模块列表、自包含检测机制、多环境命令设计、多平台支持策略、代码风格规范和构建发布流程。是所有其他文档的上下文基础。

**关键结论**:
- xim（Lua）和 xvm（Rust）暂不迁移，C++ 层做入口编排
- `XLINGS_HOME` 路径可完全自定义，实现系统环境隔离
- 自包含检测：可执行文件同级有 `xim/` 目录 → 自包含模式

---

### [env-analysis.md](env-analysis.md) — 现有架构深度分析

分析 `/xpkgs/`（160+ 包）、`data/bin/`（616 个 xvm-shim 副本）和 `.workspace.xvm.yaml` 的真实数据，揭示 xlings 已天然形成三层 Nix-like 架构。

**关键结论**:
- `xim/xpkgs/` 已是 **content-addressed store**（版本寻址，多版本并存）
- `data/bin/` 是 **激活层**（xvm-shim 物理副本，无状态动态路由）
- `.workspace.xvm.yaml` 是 **profile**（激活版本清单）

---

### [env-design.md](env-design.md) — 多环境管理与业界对比

与 Nix、Conda、Python venv、Rustup、Docker、Sandbox 的详细对比分析，论证 xlings 多环境方案在跨平台性和轻便性上的优势，以及 `xlings env` 的 5 个子命令接口设计。

---

### [env-store-design.md](env-store-design.md) — Store/Profile/世代设计

核心结论：`xim/xpkgs/` 已是 store，无需另建。重点设计：

- **多环境目录结构**：`envs/<name>/bin/` + `envs/<name>/xvm/workspace.yaml`（per-env 隔离）
- **共享 store**：`xim/xpkgs/`（全局，所有环境共用）
- **Profile 世代**：`generations/0XX.json`（每次安装/删除追加，支持回滚）
- **GC**：扫描所有 envs 引用，清理 xpkgs 孤立版本
- **XLINGS_PKGDIR**：新环境变量，指向全局 xpkgs，xim 优先读取

---

### [rpath-and-os-vision.md](rpath-and-os-vision.md) — RPATH 解决方案 + OS 演进

**RPATH 方案**（两层，~15 行代码实现）:
- 同包内：`$ORIGIN/../lib`（构建时注入）
- 跨包：`${XLINGS_HOME}` 变量替换 → `LD_LIBRARY_PATH`（xvm shims.rs 运行时展开）

**OS 演进三阶段**:
1. 用户空间工具管理器（当前）：`${XLINGS_HOME}` 变量替换
2. 多用户系统级（中期）：固定 `/xls/store/` 路径
3. xlings OS 原生包管理器（远期）：不可变系统根 + overlayfs

---

### [pkg-taxonomy.md](pkg-taxonomy.md) — 包分类体系设计

两个正交维度：
- **type**（获取方式）：`binary | fromsource | script | config | meta | template | scode | d2x`
- **source**（构建者）：`official | upstream | community`（仅 binary 类型有意义）

命名空间约定即为粗分类，xpkg 文件本身是元数据源，无需额外索引文件。

---

## 快速索引

| 想了解 | 去看 |
|--------|------|
| 项目整体目标和迁移范围 | [main.md §1](main.md#一项目定位与迁移目标) |
| 新旧架构对比 | [env-analysis.md](env-analysis.md) |
| `xlings env` 命令设计 | [main.md §5](main.md#五多环境管理设计) |
| 多环境目录结构 | [env-store-design.md §3](env-store-design.md#三多环境目录设计) |
| 世代回滚和 GC | [env-store-design.md §6-7](env-store-design.md#六回滚流程) |
| C++23 模块列表 | [main.md §3](main.md#三c23-模块设计) |
| 代码风格规范 | [main.md §7](main.md#七代码风格规范mcpp-style-ref-落地) |
| RPATH 解决方案 | [rpath-and-os-vision.md §3](rpath-and-os-vision.md#三最简洁的解决方案变量替换--origin) |
| 包类型和命名规范 | [pkg-taxonomy.md](pkg-taxonomy.md) |
| OS 演进路线 | [rpath-and-os-vision.md §4](rpath-and-os-vision.md#四xlings-作为操作系统包管理器的演进) |
| 实施任务和并行分组 | [tasks/README.md](tasks/README.md) |

---

## 现状 Gap（设计 vs 实现）

| 模块 | 状态 | 说明 |
|------|------|------|
| `core/config.cppm` | 部分实现 | 已有基础配置，缺 activeEnv / XLINGS_PKGDIR / 自包含检测 |
| `core/cmdprocessor.cppm` | 部分实现 | 已有基础命令分发，缺 env / store gc 命令 |
| `core/platform/` | 部分实现 | 三平台已有，缺 `get_executable_path()` |
| `core/env.cppm` | 未实现 | 需新建 |
| `core/profile.cppm` | 未实现 | 需新建 |
| `core/xvm/xvmlib/shims.rs` | 部分实现 | 已有 LD_LIBRARY_PATH 注入，缺 `${XLINGS_HOME}` 展开 |
| `core/xim/pm/XPackage.lua` | 部分实现 | 已有 type 字段，缺 source / maintainer |
| CI/CD | 部分实现 | 已有 Linux，缺 macOS / Windows |

详细的任务拆分和实施步骤见 [tasks/README.md](tasks/README.md)。
