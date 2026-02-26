# T27 — PR #151 CI 修复：shim 初始化 + 镜像切换 + macOS xmake 兼容

> **Wave**: 依赖 T24/T25/T26（PR #151 合入后的修复）
> **PR**: [#151](https://github.com/d2learn/xlings/pull/151)
> **涉及文件**: CI YAML、E2E 测试脚本、C++ 平台模块

---

## 1. 背景

PR #151 将 shim 创建从打包时（release script）改为安装时（`xlings self install` / `self init`），以减少 ~3MB 包体积。同时引入 `config/xvm` 模板和统一的发布脚本。

合入后 CI 在 Linux、macOS、Windows 三个平台均失败，原因各不相同。

---

## 2. 问题清单

| # | 平台 | 失败步骤 | 根因 | 严重程度 |
|---|------|---------|------|---------|
| 1 | Linux + macOS | Phase 3: `xlings install d2x` | Phase 3 直接解压包使用，未运行 `self install`/`self init`，`subos/default/bin/` 为空，xvm 无法创建新 shim | **P0** |
| 2 | Linux + macOS | Phase 3 + E2E: 索引同步超时 | `.xlings.json` 默认 `mirror: "CN"` 指向 Gitee/gitcode，CI runner 无法访问中国镜像 | **P0** |
| 3 | macOS | Phase 3: `xlings install d2x@0.1.3` exit code 6 | PR 新增 macOS xmake bundle 打包，但 `xmake-bundle-v3.0.7.macos.arm64` 二进制在 ARM64 上 SIGABRT 崩溃（exit 134/6） | **P0** |
| 4 | macOS | `subos new` 后新 subos 的 shim 无法执行 | 复制二进制后未重新签名（Apple Silicon 要求 ad-hoc codesign） | **P1** |
| 5 | Windows | GitHub API 超时 | `quick_install.ps1` 查询 GitHub releases API 时遭遇 Unicorn 504/超时 | **P2（瞬态）** |

---

## 3. 修复方案

### 3.1 Phase 3 添加 `xlings self init`（修复问题 #1）

**原理**：shim 现在在安装/初始化时创建。Phase 3 测试直接解压包而不运行 `self install`，需要手动调用 `self init` 来创建 shim。

**改动文件**：
- `.github/workflows/xlings-ci-linux.yml`
- `.github/workflows/xlings-ci-macos.yml`

**改动内容**：在 Phase 3 "Setup xlings environment" 步骤中添加：

```yaml
export PATH="$XLINGS_HOME/subos/current/bin:$XLINGS_HOME/bin:$PATH"
export XLINGS_HOME XLINGS_DATA="$XLINGS_HOME/data" XLINGS_SUBOS="$XLINGS_HOME/subos/current"
"$XLINGS_HOME/bin/xlings" self init
```

**调用链**：`self init` → `cmd_init()` → `ensure_subos_shims(p.binDir, shimSrc, p.homeDir)` → 将 `$XLINGS_HOME/bin/xvm-shim` 复制到 `subos/default/bin/` 下的 7+1 个 shim。

---

### 3.2 镜像切换为 GLOBAL（修复问题 #2）

**原理**：`config/xlings.json` 默认 `mirror: "CN"`，活跃 URL 指向 Gitee/gitcode。GitHub Actions runner 无法可靠访问中国镜像。

**改动文件**：
- `.github/workflows/xlings-ci-linux.yml`（Phase 3 setup）
- `.github/workflows/xlings-ci-macos.yml`（Phase 3 setup）
- `tests/e2e/linux_usability_test.sh`（`prepare_runtime()`）
- `tests/e2e/macos_usability_test.sh`（`setup_env()`）

**改动内容**：解压包后用 `jq` 将 `.xlings.json` 中的活跃 URL 替换为 GLOBAL 镜像：

```bash
if command -v jq &>/dev/null && [[ -f "$PKG_DIR/.xlings.json" ]]; then
  jq '.xim["index-repo"] = .xim.mirrors["index-repo"].GLOBAL |
      .xim["res-server"] = .xim.mirrors["res-server"].GLOBAL |
      .repo = "https://github.com/d2learn/xlings.git"' \
    "$PKG_DIR/.xlings.json" > "$PKG_DIR/.xlings.json.tmp" && \
    mv "$PKG_DIR/.xlings.json.tmp" "$PKG_DIR/.xlings.json"
fi
```

| 字段 | 修改前（CN） | 修改后（GLOBAL） |
|------|------------|----------------|
| `xim.index-repo` | `https://gitee.com/sunrisepeak/xim-pkgindex.git` | `https://github.com/d2learn/xim-pkgindex.git` |
| `xim.res-server` | `https://gitcode.com/xlings-res` | `https://github.com/xlings-res` |
| `repo` | `https://gitee.com/sunrisepeak/xlings.git` | `https://github.com/d2learn/xlings.git` |

---

### 3.3 macOS 跳过 xmake bundle（修复问题 #3）

**原理**：PR #151 新增 macOS xmake bundle 打包功能（T26），但 `xmake-bundle-v3.0.7.macos.arm64` 在 Apple Silicon 上运行时 SIGABRT 崩溃（exit code 134 直接调用，exit code 6 通过 `std::system()` 返回原始 waitpid 状态）。

**诊断过程**：
1. 添加 `set +e` 捕获错误输出，发现 `xlings install` 无输出即退出
2. 添加 `"$XLINGS_HOME/bin/xmake" xim ...` 直接调用，确认 exit code 134 = SIGABRT
3. 确认该 bundle 二进制是 PR #151 新增的（原 macOS CI 不打包 xmake）

**改动文件**：
- `.github/workflows/xlings-ci-macos.yml`

**改动内容**：

```yaml
# Before
SKIP_NETWORK_VERIFY=1 ./tools/macos_release.sh

# After
SKIP_NETWORK_VERIFY=1 SKIP_XMAKE_BUNDLE=1 ./tools/macos_release.sh
```

macOS CI 依赖 brew 安装的 xmake（Phase 1 已 `brew install xmake`），无需 bundle。

**后续**：macOS xmake bundle 功能需要单独排查 SIGABRT 原因（可能是签名、沙箱、或 bundle 格式兼容性问题），作为独立 issue 跟进。

---

### 3.4 macOS codesign 重签名（修复问题 #4）

**原理**：Apple Silicon (arm64) 要求所有可执行二进制有有效签名。`fs::copy_file` 复制后签名失效，需重新 ad-hoc 签名。

**改动文件**：
- `core/platform/macos.cppm`

**改动内容**：`make_files_executable()` 中 `chmod` 后增加 `codesign`：

```cpp
export void make_files_executable(const std::filesystem::path& dir) {
    if (!std::filesystem::exists(dir)) return;
    for (auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            ::chmod(entry.path().c_str(), 0755);
            // Apple Silicon 复制后需重签名
            std::string cmd = "codesign -s - -f \"" + entry.path().string() + "\" 2>/dev/null";
            std::system(cmd.c_str());
        }
    }
}
```

---

## 4. 完整改动列表

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| `.github/workflows/xlings-ci-linux.yml` | 修改 | Phase 3: 添加 `self init` + 镜像切换 |
| `.github/workflows/xlings-ci-macos.yml` | 修改 | Phase 3: 添加 `self init` + 镜像切换 + `SKIP_XMAKE_BUNDLE=1` |
| `tests/e2e/linux_usability_test.sh` | 修改 | `prepare_runtime()` 添加镜像切换 |
| `tests/e2e/macos_usability_test.sh` | 修改 | `setup_env()` 添加镜像切换 |
| `core/platform/macos.cppm` | 修改 | `make_files_executable()` 添加 codesign |

---

## 5. 验收标准

```bash
# CI 三平台全绿
# Linux: Phase 2.5 install + shim 验证 ✓, Phase 3 全部测试 ✓, E2E ✓
# macOS: Phase 2.5 install + shim 验证 ✓, Phase 3 全部测试 ✓, E2E ✓
# Windows: Phase 2.5 install + shim 验证 ✓, Phase 3 全部测试 ✓, E2E ✓
```

---

## 6. 遗留问题

| 问题 | 状态 | 建议 |
|------|------|------|
| `xmake-bundle-v3.0.7.macos.arm64` SIGABRT | 未修复 | 单独 issue：排查 xmake bundle macOS 兼容性 |
| `config/xlings.json` 默认 CN 镜像 | 设计如此 | CI 中覆写即可，用户侧保持 CN 默认 |
| `std::system()` 返回原始 waitpid 状态未用 `WEXITSTATUS` | 已知 bug | `platform::exec` 应改为 `WEXITSTATUS(std::system(cmd))` |
