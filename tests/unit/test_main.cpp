#include <gtest/gtest.h>
#include <iomanip>

import std;
import xlings.i18n;
import xlings.log;
import xlings.utils;
import xlings.ui;
import xlings.xim.libxpkg.types.type;
import xlings.xim.index;
import xlings.xim.catalog;
import xlings.xim.resolver;
import xlings.xim.downloader;
import xlings.xim.installer;
import xlings.xim.commands;
import xlings.xim.repo;
import xlings.xvm.types;
import xlings.xvm.db;
import xlings.xvm.shim;
import xlings.xvm.commands;
import xlings.config;
import xlings.platform;
import xlings.json;
import xlings.xself;
import xlings.profile;
import xlings.event;
import xlings.event_stream;
import xlings.capability;
import xlings.task;
import mcpplibs.xpkg;
import mcpplibs.cmdline;

namespace {

std::optional<std::filesystem::path> find_pkgindex_repo() {
    namespace fs = std::filesystem;

    if (auto env = std::getenv("XIM_PKGINDEX_DIR")) {
        fs::path path(env);
        if (fs::exists(path / "pkgs")) return path;
    }

    const std::vector<fs::path> candidates = {
        fs::current_path() / "tests/fixtures/xim-pkgindex",
        fs::current_path() / "../xim-pkgindex",
        fs::current_path() / "../d2learn/xim-pkgindex",
        fs::current_path() / "../../xim-pkgindex",
        fs::current_path() / "../../d2learn/xim-pkgindex",
    };

    for (auto& path : candidates) {
        std::error_code ec;
        if (fs::exists(path / "pkgs", ec)) return fs::weakly_canonical(path, ec);
    }

    return std::nullopt;
}

}  // namespace

// ============================================================
// i18n tests
// ============================================================

TEST(I18nTest, SetAndGetLanguageEn) {
    xlings::i18n::set_language("en");
    EXPECT_EQ(xlings::i18n::language(), "en");
    EXPECT_FALSE(xlings::i18n::is_chinese());
}

TEST(I18nTest, SetAndGetLanguageZh) {
    xlings::i18n::set_language("zh");
    EXPECT_EQ(xlings::i18n::language(), "zh");
    EXPECT_TRUE(xlings::i18n::is_chinese());
}

TEST(I18nTest, TranslateEnglish) {
    xlings::i18n::set_language("en");
    auto msg = xlings::i18n::tr(xlings::i18n::Msg::INSTALL_DONE);
    EXPECT_FALSE(msg.empty());
    EXPECT_NE(msg.find("{}"), std::string_view::npos);  // has format placeholder
}

TEST(I18nTest, TranslateChinese) {
    xlings::i18n::set_language("zh");
    auto msg = xlings::i18n::tr(xlings::i18n::Msg::INSTALL_DONE);
    EXPECT_FALSE(msg.empty());
}

TEST(I18nTest, TranslateFormat) {
    xlings::i18n::set_language("en");
    auto msg = xlings::i18n::trf(xlings::i18n::Msg::INSTALL_DONE, std::string("gcc@15.1.0"));
    EXPECT_NE(msg.find("gcc@15.1.0"), std::string::npos);
    EXPECT_EQ(msg.find("{}"), std::string::npos);  // no leftover placeholder
}

TEST(I18nTest, AllMessagesHaveContent) {
    int count = static_cast<int>(xlings::i18n::Msg::MSG_COUNT_);
    for (int lang = 0; lang < 2; ++lang) {
        xlings::i18n::set_language(lang == 0 ? "en" : "zh");
        for (int i = 0; i < count; ++i) {
            auto msg = xlings::i18n::tr(static_cast<xlings::i18n::Msg>(i));
            EXPECT_FALSE(msg.empty())
                << "Message " << i << " is empty for lang=" << (lang == 0 ? "en" : "zh");
        }
    }
}

TEST(I18nTest, InvalidMsgReturnsEmpty) {
    auto msg = xlings::i18n::tr(xlings::i18n::Msg::MSG_COUNT_);
    EXPECT_TRUE(msg.empty());
}

// ============================================================
// log tests
// ============================================================

TEST(LogTest, SetLevelByString) {
    // Should not crash
    xlings::log::set_level("debug");
    xlings::log::set_level("info");
    xlings::log::set_level("warn");
    xlings::log::set_level("error");
}

TEST(LogTest, SetLevelByEnum) {
    xlings::log::set_level(xlings::log::Level::Debug);
    xlings::log::set_level(xlings::log::Level::Error);
}

TEST(LogTest, SetAndClearContext) {
    xlings::log::set_context("test-pkg");
    xlings::log::clear_context();
}

TEST(LogTest, LogToFile) {
    namespace fs = std::filesystem;
    auto tmpFile = fs::temp_directory_path() / "xlings_test_log.txt";
    // Remove if exists
    fs::remove(tmpFile);

    xlings::log::set_level(xlings::log::Level::Debug);
    xlings::log::set_file(tmpFile);
    xlings::log::info("test message {}", 42);
    xlings::log::debug("debug msg");
    xlings::log::warn("warn msg");
    xlings::log::error("error msg");

    // Close by setting to empty path
    xlings::log::set_file("");

    // Read file and verify
    std::ifstream f(tmpFile);
    ASSERT_TRUE(f.is_open());
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    f.close();

    EXPECT_NE(content.find("test message 42"), std::string::npos);
    EXPECT_NE(content.find("debug msg"), std::string::npos);
    EXPECT_NE(content.find("warn msg"), std::string::npos);
    EXPECT_NE(content.find("error msg"), std::string::npos);
    EXPECT_NE(content.find("[xlings]"), std::string::npos);  // info prefix
    EXPECT_NE(content.find("[error]"), std::string::npos);    // error prefix

    fs::remove(tmpFile);
    xlings::log::set_level(xlings::log::Level::Info);
}

// ============================================================
// utils tests
// ============================================================

TEST(UtilsTest, SplitString) {
    auto parts = xlings::utils::split_string("a:b:c", ':');
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[0], "a");
    EXPECT_EQ(parts[1], "b");
    EXPECT_EQ(parts[2], "c");
}

TEST(UtilsTest, SplitStringEmpty) {
    auto parts = xlings::utils::split_string("", ',');
    // Empty string splits to 0 parts with std::views::split
    EXPECT_EQ(parts.size(), 0u);
}

TEST(UtilsTest, TrimString) {
    EXPECT_EQ(xlings::utils::trim_string("  hello  "), "hello");
    EXPECT_EQ(xlings::utils::trim_string("hello"), "hello");
    EXPECT_EQ(xlings::utils::trim_string(""), "");
}

TEST(UtilsTest, StripAnsi) {
    auto cleaned = xlings::utils::strip_ansi("\x1b[31mred\x1b[0m");
    EXPECT_EQ(cleaned, "red");
}

TEST(UtilsTest, GetEnvOrDefault) {
    auto val = xlings::utils::get_env_or_default("XLINGS_TEST_NONEXISTENT_12345", "fallback");
    EXPECT_EQ(val, "fallback");
}

// ============================================================
// cmdline tests
// ============================================================

TEST(CmdlineTest, BasicParse) {
    using namespace mcpplibs;
    auto app = cmdline::App("test")
        .version("1.0")
        .option("verbose").short_name('v').help("verbose")
        .subcommand("install")
            .description("install a package")
            .arg("target").required().help("target package")
            .action([](const cmdline::ParsedArgs& args) {
                EXPECT_EQ(args.positional(0), "gcc");
            });

    auto result = app.parse_from("test install gcc");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->has_subcommand());
    EXPECT_EQ(result->subcommand_name(), "install");
}

TEST(CmdlineTest, GlobalOptionPropagation) {
    using namespace mcpplibs;
    bool actionCalled { false };
    auto app = cmdline::App("test")
        .option("yes").short_name('y').global().help("yes")
        .subcommand("install")
            .arg("target").help("pkg")
            .action([&](const cmdline::ParsedArgs& args) {
                EXPECT_TRUE(args.is_flag_set("yes"));
                actionCalled = true;
            });

    auto result = app.parse_from("test install gcc -y");
    ASSERT_TRUE(result.has_value());
    app.run(*result);
    EXPECT_TRUE(actionCalled);
}

TEST(CmdlineTest, HelpTriggersNonError) {
    using namespace mcpplibs;
    auto app = cmdline::App("test").version("1.0");
    auto result = app.parse_from("test --help");
    ASSERT_FALSE(result.has_value());
    EXPECT_FALSE(result.error().is_error());
}

TEST(CmdlineTest, UnknownOptionIsError) {
    using namespace mcpplibs;
    auto app = cmdline::App("test");
    auto result = app.parse_from("test --bogus");
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().is_error());
}

// ============================================================
// UI tests (non-interactive only)
// ============================================================

TEST(UiTest, PrintProgressNocrash) {
    using namespace xlings::ui;
    std::vector<StatusEntry> entries = {
        { "glibc@2.39",    Phase::Done,        1.0f, "" },
        { "gcc@15.1.0",    Phase::Downloading, 0.45f, "" },
        { "binutils@2.42", Phase::Pending,     0.0f, "" },
    };
    // Should not crash
    print_progress(entries);
}

TEST(UiTest, PhaseLabels) {
    using namespace xlings::ui;
    EXPECT_EQ(phase_label(Phase::Pending), "pending");
    EXPECT_EQ(phase_label(Phase::Done), "done");
    EXPECT_EQ(phase_label(Phase::Failed), "failed");
    EXPECT_EQ(phase_label(Phase::Downloading), "downloading");
}

// ============================================================
// xim types tests
// ============================================================

TEST(XimTypesTest, InstallPlanEmpty) {
    xlings::xim::InstallPlan plan;
    EXPECT_FALSE(plan.has_errors());
    EXPECT_EQ(plan.pending_count(), 0u);
}

TEST(XimTypesTest, InstallPlanWithNodes) {
    xlings::xim::InstallPlan plan;
    {
        xlings::xim::PlanNode n; n.name = "gcc@15.1.0"; n.version = "15.1.0";
        plan.nodes.push_back(std::move(n));
    }
    {
        xlings::xim::PlanNode n; n.name = "glibc@2.39"; n.version = "2.39"; n.alreadyInstalled = true;
        plan.nodes.push_back(std::move(n));
    }
    {
        xlings::xim::PlanNode n; n.name = "binutils@2.42"; n.version = "2.42";
        plan.nodes.push_back(std::move(n));
    }
    EXPECT_EQ(plan.pending_count(), 2u);  // gcc + binutils (glibc already installed)
}

TEST(XimTypesTest, InstallPlanErrors) {
    xlings::xim::InstallPlan plan;
    plan.errors.push_back("cyclic dependency detected");
    EXPECT_TRUE(plan.has_errors());
}

TEST(XimTypesTest, DownloadTaskInit) {
    xlings::xim::DownloadTask task {
        .name = "gcc@15.1.0",
        .url = "https://example.com/gcc.tar.gz",
        .sha256 = "abcdef",
        .destDir = "/tmp/xim"
    };
    EXPECT_EQ(task.name, "gcc@15.1.0");
    EXPECT_EQ(task.sha256, "abcdef");
}

TEST(XimCatalogTest, CanonicalPackageNameAndStoreName) {
    EXPECT_EQ(xlings::xim::canonical_package_name("xim", "gcc"), "xim:gcc");
    EXPECT_EQ(xlings::xim::canonical_package_name("", "gcc"), "gcc");
    EXPECT_EQ(xlings::xim::package_store_name("xim", "gcc"), "xim-x-gcc");
    EXPECT_EQ(xlings::xim::package_store_name("", "gcc"), "gcc");
}

TEST(XimCatalogTest, FormatAmbiguousCandidates) {
    std::vector<xlings::xim::PackageMatch> matches = {
        {
            .name = "gcc",
            .version = "15.1.0",
            .namespaceName = "xim",
            .canonicalName = "xim:gcc",
            .repoName = "xim",
            .scope = xlings::xim::PackageScope::Global,
        },
        {
            .name = "gcc",
            .version = "15.1.0",
            .namespaceName = "project",
            .canonicalName = "project:gcc",
            .repoName = "project",
            .scope = xlings::xim::PackageScope::Project,
        },
    };

    auto msg = xlings::xim::format_ambiguous_candidates("gcc", matches);
    EXPECT_NE(msg.find("package 'gcc' is ambiguous"), std::string::npos);
    EXPECT_NE(msg.find("1. xim:gcc@15.1.0"), std::string::npos);
    EXPECT_NE(msg.find("2. project:gcc@15.1.0"), std::string::npos);
    EXPECT_NE(msg.find("from global repo 'xim'"), std::string::npos);
    EXPECT_NE(msg.find("from project repo 'project'"), std::string::npos);
    EXPECT_NE(msg.find("xlings install xim:gcc@15.1.0"), std::string::npos);
}

TEST(ConfigTest, WorkspaceInstallTargets) {
    xlings::xvm::Workspace ws;
    ws["gcc"] = "15.1.0";
    ws["node"] = "";

    auto targets = xlings::Config::workspace_install_targets(ws);
    ASSERT_EQ(targets.size(), 2u);
    EXPECT_EQ(targets[0], "gcc@15.1.0");
    EXPECT_EQ(targets[1], "node");
}

TEST(ConfigTest, MergedWorkspaceAnonymousOverridesGlobal) {
    xlings::xvm::Workspace globalWs;
    globalWs["gcc"] = "15.1.0";
    globalWs["node"] = "22.0.0";

    xlings::xvm::Workspace projectWs;
    projectWs["gcc"] = "14.2.0";
    projectWs["python"] = "3.12.0";

    auto effective = xlings::Config::merged_workspace(
        globalWs, projectWs, {}, xlings::ProjectSubosMode::Anonymous);

    ASSERT_EQ(effective.size(), 3u);
    EXPECT_EQ(effective["gcc"], "14.2.0");
    EXPECT_EQ(effective["node"], "22.0.0");
    EXPECT_EQ(effective["python"], "3.12.0");
}

TEST(ConfigTest, MergedWorkspaceNamedDoesNotInheritGlobal) {
    xlings::xvm::Workspace globalWs;
    globalWs["gcc"] = "15.1.0";
    globalWs["node"] = "22.0.0";

    xlings::xvm::Workspace projectWs;
    projectWs["gcc"] = "14.2.0";

    xlings::xvm::Workspace subosWs;
    subosWs["clang"] = "18.1.0";

    auto effective = xlings::Config::merged_workspace(
        globalWs, projectWs, subosWs, xlings::ProjectSubosMode::Named);

    ASSERT_EQ(effective.size(), 2u);
    EXPECT_EQ(effective["gcc"], "14.2.0");
    EXPECT_EQ(effective["clang"], "18.1.0");
    EXPECT_FALSE(effective.contains("node"));
}

TEST(ConfigTest, MergedVersionsProjectOverridesGlobal) {
    xlings::xvm::VersionDB globalDb;
    xlings::xvm::add_version(globalDb, "gcc", "15.1.0", "/global/gcc-15");
    xlings::xvm::add_version(globalDb, "node", "22.0.0", "/global/node-22");

    xlings::xvm::VersionDB projectDb;
    xlings::xvm::add_version(projectDb, "gcc", "14.2.0", "/project/gcc-14");
    xlings::xvm::add_version(projectDb, "python", "3.12.0", "/project/python-3.12");

    auto merged = xlings::Config::merged_versions(globalDb, projectDb);
    ASSERT_EQ(merged.size(), 3u);
    EXPECT_TRUE(xlings::xvm::has_version(merged, "gcc", "15.1.0"));
    EXPECT_TRUE(xlings::xvm::has_version(merged, "gcc", "14.2.0"));
    EXPECT_TRUE(xlings::xvm::has_version(merged, "node", "22.0.0"));
    EXPECT_TRUE(xlings::xvm::has_version(merged, "python", "3.12.0"));
}

TEST(ConfigTest, ResolveRepoSourceAbsolutePath) {
#ifdef _WIN32
    xlings::IndexRepo repo { .name = "local", .url = "C:\\tmp\\xim-pkgindex" };
    auto expected = std::filesystem::path("C:\\tmp\\xim-pkgindex");
#else
    xlings::IndexRepo repo { .name = "local", .url = "/tmp/xim-pkgindex" };
    auto expected = std::filesystem::path("/tmp/xim-pkgindex");
#endif
    auto path = xlings::Config::resolve_repo_source(repo, false);
    EXPECT_EQ(path, expected);
    EXPECT_TRUE(xlings::Config::is_local_repo_source(repo, false));
}

TEST(ConfigTest, ResolveRepoSourceFileScheme) {
#ifdef _WIN32
    // Standard file URI: file:///C:/path — production code strips the leading /
    xlings::IndexRepo repo { .name = "local", .url = "file:///C:/tmp/xim-pkgindex" };
    auto expected = std::filesystem::path("C:\\tmp\\xim-pkgindex");
#else
    xlings::IndexRepo repo { .name = "local", .url = "file:///tmp/xim-pkgindex" };
    auto expected = std::filesystem::path("/tmp/xim-pkgindex");
#endif
    auto path = xlings::Config::resolve_repo_source(repo, false);
    EXPECT_EQ(path, expected);
    EXPECT_TRUE(xlings::Config::is_local_repo_source(repo, false));
}

TEST(ConfigTest, ResolveRepoSourceRemoteUrlReturnsEmpty) {
    xlings::IndexRepo repo {
        .name = "xim",
        .url = "https://github.com/d2learn/xim-pkgindex.git"
    };
    EXPECT_TRUE(xlings::Config::resolve_repo_source(repo, false).empty());
    EXPECT_FALSE(xlings::Config::is_local_repo_source(repo, false));
}

// ============================================================
// xim index tests (requires xim-pkgindex repo)
// ============================================================

class XimIndexTest : public ::testing::Test {
protected:
    std::filesystem::path repoDir_;

    void SetUp() override {
        auto repo = find_pkgindex_repo();
        if (!repo) GTEST_SKIP() << "xim-pkgindex repo not found";
        repoDir_ = *repo;
    }
};

TEST_F(XimIndexTest, BuildIndex) {
    xlings::xim::IndexManager mgr(repoDir_);
    auto result = mgr.rebuild();
    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_TRUE(mgr.is_loaded());
    EXPECT_GT(mgr.size(), 40u);  // should have 50+ entries
}

TEST_F(XimIndexTest, SearchPackage) {
    xlings::xim::IndexManager mgr(repoDir_);
    auto r = mgr.rebuild();
    ASSERT_TRUE(r.has_value()) << r.error();

    auto results = mgr.search("gcc");
    EXPECT_FALSE(results.empty());
    // At least one result should contain "gcc"
    bool found = false;
    for (auto& name : results) {
        if (name.find("gcc") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "search for 'gcc' should return gcc-related packages";
}

TEST_F(XimIndexTest, MatchVersion) {
    xlings::xim::IndexManager mgr(repoDir_);
    auto r = mgr.rebuild();
    ASSERT_TRUE(r.has_value()) << r.error();

    auto match = mgr.match_version("gcc");
    EXPECT_TRUE(match.has_value()) << "should find a versioned gcc entry";
    if (match) {
        EXPECT_NE(match->find("gcc"), std::string::npos);
    }
}

TEST_F(XimIndexTest, FindEntry) {
    xlings::xim::IndexManager mgr(repoDir_);
    auto r = mgr.rebuild();
    ASSERT_TRUE(r.has_value()) << r.error();

    // Find a known package
    auto match = mgr.match_version("gcc");
    ASSERT_TRUE(match.has_value());
    auto* entry = mgr.find_entry(*match);
    ASSERT_NE(entry, nullptr);
    EXPECT_FALSE(entry->path.empty());
}

TEST_F(XimIndexTest, LoadPackage) {
    xlings::xim::IndexManager mgr(repoDir_);
    auto r = mgr.rebuild();
    ASSERT_TRUE(r.has_value()) << r.error();

    auto match = mgr.match_version("gcc");
    ASSERT_TRUE(match.has_value());

    auto pkg = mgr.load_package(*match);
    ASSERT_TRUE(pkg.has_value()) << pkg.error();
    EXPECT_EQ(pkg->name, "gcc");
}

TEST_F(XimIndexTest, AllNames) {
    xlings::xim::IndexManager mgr(repoDir_);
    auto r = mgr.rebuild();
    ASSERT_TRUE(r.has_value()) << r.error();

    auto names = mgr.all_names();
    EXPECT_GT(names.size(), 40u);
    // Should be sorted
    EXPECT_TRUE(std::is_sorted(names.begin(), names.end()));
}

TEST_F(XimIndexTest, MarkInstalled) {
    xlings::xim::IndexManager mgr(repoDir_);
    auto r = mgr.rebuild();
    ASSERT_TRUE(r.has_value()) << r.error();

    auto match = mgr.match_version("gcc");
    ASSERT_TRUE(match.has_value());

    mgr.mark_installed(*match, true);
    auto* entry = mgr.find_entry(*match);
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->installed);

    mgr.mark_installed(*match, false);
    entry = mgr.find_entry(*match);
    EXPECT_FALSE(entry->installed);
}

TEST_F(XimIndexTest, EmptyRepoDirFails) {
    xlings::xim::IndexManager mgr;
    auto result = mgr.rebuild();
    EXPECT_FALSE(result.has_value());
}

TEST_F(XimIndexTest, NonexistentRepoDirFails) {
    xlings::xim::IndexManager mgr("/tmp/nonexistent_xim_repo_dir_xyz");
    auto result = mgr.rebuild();
    EXPECT_FALSE(result.has_value());
}

// ============================================================
// xim resolver tests
// ============================================================

class XimResolverTest : public ::testing::Test {
protected:
    std::filesystem::path repoDir_;
    xlings::xim::IndexManager mgr_;

    void SetUp() override {
        auto repo = find_pkgindex_repo();
        if (!repo) GTEST_SKIP() << "xim-pkgindex repo not found";
        repoDir_ = *repo;
        mgr_ = xlings::xim::IndexManager(repoDir_);
        auto r = mgr_.rebuild();
        if (!r) GTEST_SKIP() << "rebuild failed: " << r.error();
    }
};

TEST_F(XimResolverTest, ResolveSinglePackage) {
    std::vector<std::string> targets = { "xvm" };
    auto result = xlings::xim::resolve(mgr_, targets, "linux");
    // Should succeed (xvm has no complex deps on linux)
    ASSERT_FALSE(result->has_errors())
        << "errors: " << (result->errors.empty() ? "" : result->errors[0]);
    EXPECT_FALSE(result->nodes.empty());
}

TEST_F(XimResolverTest, ResolveWithDeps) {
    std::vector<std::string> targets = { "pnpm" };
    auto result = xlings::xim::resolve(mgr_, targets, "linux");
    // pnpm depends on node (package name is "node", not "nodejs")
    if (result.has_value() && !result->has_errors()) {
        bool hasNode = false;
        for (auto& node : result->nodes) {
            if (node.name.find("node") != std::string::npos) {
                hasNode = true;
                break;
            }
        }
        EXPECT_TRUE(hasNode) << "pnpm should pull in node as dependency";
        // Deps should come before dependents in topo order
        int nodeIdx = -1, pnpmIdx = -1;
        for (int i = 0; i < static_cast<int>(result->nodes.size()); ++i) {
            if (result->nodes[i].name.find("node") != std::string::npos) nodeIdx = i;
            if (result->nodes[i].name.find("pnpm") != std::string::npos) pnpmIdx = i;
        }
        if (nodeIdx >= 0 && pnpmIdx >= 0) {
            EXPECT_LT(nodeIdx, pnpmIdx)
                << "node should come before pnpm in topo order";
        }
    }
}

TEST_F(XimResolverTest, ResolveNonexistent) {
    std::vector<std::string> targets = { "nonexistent_pkg_xyz_123" };
    auto result = xlings::xim::resolve(mgr_, targets, "linux");
    EXPECT_TRUE(result->has_errors());
}

TEST_F(XimResolverTest, ResolveMultipleTargets) {
    std::vector<std::string> targets = { "xvm", "claude-code" };
    auto result = xlings::xim::resolve(mgr_, targets, "linux");
    if (result.has_value() && !result->has_errors()) {
        // Should have at least 2 nodes
        EXPECT_GE(result->nodes.size(), 2u);
    }
}

// ============================================================
// xim downloader tests
// ============================================================

TEST(XimDownloaderTest, DownloadTaskExtractFilename) {
    xlings::xim::DownloadTask task {
        .name = "test",
        .url = "https://example.com/path/to/file.tar.gz?token=abc",
        .sha256 = "",
        .destDir = "/tmp/xim_test_dl"
    };
    // download_one would extract "file.tar.gz" from URL
    // We just verify the task structure is valid
    EXPECT_EQ(task.name, "test");
    EXPECT_FALSE(task.url.empty());
}

TEST(XimDownloaderTest, ExtractArchiveBadFormat) {
    namespace fs = std::filesystem;
    auto tmpDir = fs::temp_directory_path() / "xim_test_extract";
    auto result = xlings::xim::extract_archive("/tmp/nonexistent.xyz", tmpDir);
    EXPECT_FALSE(result.has_value());
    fs::remove_all(tmpDir);
}

TEST(XimDownloaderTest, DownloadAllEmpty) {
    std::vector<xlings::xim::DownloadTask> tasks;
    xlings::xim::DownloaderConfig config;
    auto results = xlings::xim::download_all(tasks, config, nullptr);
    EXPECT_TRUE(results.empty());
}

// ============================================================
// xim installer tests
// ============================================================

class XimInstallerTest : public ::testing::Test {
protected:
    std::filesystem::path repoDir_;
    xlings::xim::IndexManager mgr_;

    void SetUp() override {
        auto repo = find_pkgindex_repo();
        if (!repo) GTEST_SKIP() << "xim-pkgindex repo not found";
        repoDir_ = *repo;
        mgr_ = xlings::xim::IndexManager(repoDir_);
        auto r = mgr_.rebuild();
        if (!r) GTEST_SKIP() << "rebuild failed: " << r.error();
    }
};

TEST_F(XimInstallerTest, InstallerConstruction) {
    xlings::xim::Installer installer(mgr_);
    // Just verify it can be constructed without error
    SUCCEED();
}

TEST_F(XimInstallerTest, ExecuteEmptyPlan) {
    xlings::xim::Installer installer(mgr_);
    xlings::xim::InstallPlan plan;
    xlings::xim::DownloaderConfig config;

    auto result = installer.execute(plan, config, nullptr);
    EXPECT_TRUE(result.has_value());
}

TEST_F(XimInstallerTest, ExecutePlanWithErrors) {
    xlings::xim::Installer installer(mgr_);
    xlings::xim::InstallPlan plan;
    plan.errors.push_back("test error");
    xlings::xim::DownloaderConfig config;

    auto result = installer.execute(plan, config, nullptr);
    EXPECT_FALSE(result.has_value());
}

TEST_F(XimInstallerTest, UninstallNonexistent) {
    xlings::xim::Installer installer(mgr_);
    auto result = installer.uninstall("nonexistent_pkg_xyz_999");
    EXPECT_FALSE(result.has_value());
}

// ============================================================
// xim commands tests
// ============================================================

TEST(XimCommandsTest, DetectPlatform) {
    auto platform = xlings::xim::detect_platform();
    #if defined(__linux__)
        EXPECT_EQ(platform, "linux");
    #elif defined(__APPLE__)
        EXPECT_EQ(platform, "macosx");
    #elif defined(_WIN32)
        EXPECT_EQ(platform, "windows");
    #endif
}

TEST(XimCommandsTest, SearchNonexistentReturnsZero) {
    // cmd_search uses get_catalog() which loads from Config (global ~/.xlings/data),
    // not from test fixtures. Skip if catalog cannot load.
    auto& catalog = xlings::xim::get_catalog();
    if (!catalog.is_loaded()) GTEST_SKIP() << "package catalog not available";
    auto rc = xlings::xim::cmd_search("zzz_nonexistent_pkg_xyz_999");
    EXPECT_EQ(rc, 0);  // returns 0 with "no packages found" message
}

TEST(XimCommandsTest, ListWithFilter) {
    auto& catalog = xlings::xim::get_catalog();
    if (!catalog.is_loaded()) GTEST_SKIP() << "package catalog not available";
    auto rc = xlings::xim::cmd_list("gcc");
    EXPECT_EQ(rc, 0);
}

TEST(XimCommandsTest, InfoKnownPackage) {
    auto& catalog = xlings::xim::get_catalog();
    if (!catalog.is_loaded()) GTEST_SKIP() << "package catalog not available";
    auto platform = xlings::xim::detect_platform();
    // gcc fixture only has linux entries; skip on other platforms
    if (platform != "linux") GTEST_SKIP() << "gcc fixture not available on " << platform;
    auto rc = xlings::xim::cmd_info("gcc");
    EXPECT_EQ(rc, 0);
}

TEST(XimCommandsTest, InfoUnknownPackage) {
    auto& catalog = xlings::xim::get_catalog();
    if (!catalog.is_loaded()) GTEST_SKIP() << "package catalog not available";
    auto rc = xlings::xim::cmd_info("nonexistent_pkg_xyz_999");
    EXPECT_EQ(rc, 1);
}

// ============================================================
// xvm types tests
// ============================================================

TEST(XvmTypesTest, VDataConstruction) {
    xlings::xvm::VData vdata;
    vdata.path = "/usr/bin";
    vdata.alias.push_back("15");
    vdata.envs["GCC_HOME"] = "/usr/lib/gcc";

    EXPECT_EQ(vdata.path, "/usr/bin");
    ASSERT_EQ(vdata.alias.size(), 1u);
    EXPECT_EQ(vdata.alias[0], "15");
    ASSERT_EQ(vdata.envs.size(), 1u);
    EXPECT_EQ(vdata.envs.at("GCC_HOME"), "/usr/lib/gcc");
}

TEST(XvmTypesTest, VInfoConstruction) {
    xlings::xvm::VInfo info;
    info.type = "program";
    info.filename = "gcc";

    xlings::xvm::VData vdata;
    vdata.path = "/usr/bin";
    info.versions["15.1.0"] = std::move(vdata);

    info.bindings["g++"]["15.1.0"] = "g++-15";

    EXPECT_EQ(info.type, "program");
    EXPECT_EQ(info.filename, "gcc");
    EXPECT_EQ(info.versions.size(), 1u);
    EXPECT_TRUE(info.versions.contains("15.1.0"));
    EXPECT_EQ(info.bindings["g++"]["15.1.0"], "g++-15");
}

TEST(XvmTypesTest, VersionDBAndWorkspace) {
    xlings::xvm::VersionDB db;
    EXPECT_TRUE(db.empty());

    xlings::xvm::VInfo info;
    info.type = "program";
    db["gcc"] = std::move(info);
    EXPECT_EQ(db.size(), 1u);

    xlings::xvm::Workspace ws;
    ws["gcc"] = "15.1.0";
    EXPECT_EQ(ws["gcc"], "15.1.0");
}

// ============================================================
// xvm db tests
// ============================================================

TEST(XvmDbTest, AddAndRemoveVersion) {
    xlings::xvm::VersionDB db;

    xlings::xvm::add_version(db, "gcc", "15.1.0", "/usr/bin", "program", "gcc");
    EXPECT_TRUE(xlings::xvm::has_target(db, "gcc"));
    EXPECT_TRUE(xlings::xvm::has_version(db, "gcc", "15.1.0"));

    xlings::xvm::add_version(db, "gcc", "14.2.0", "/opt/gcc14/bin", "program", "gcc");
    EXPECT_TRUE(xlings::xvm::has_version(db, "gcc", "14.2.0"));

    auto all = xlings::xvm::get_all_versions(db, "gcc");
    EXPECT_EQ(all.size(), 2u);

    xlings::xvm::remove_version(db, "gcc", "14.2.0");
    EXPECT_FALSE(xlings::xvm::has_version(db, "gcc", "14.2.0"));
    EXPECT_TRUE(xlings::xvm::has_version(db, "gcc", "15.1.0"));

    // Remove last version removes the target entirely
    xlings::xvm::remove_version(db, "gcc", "15.1.0");
    EXPECT_FALSE(xlings::xvm::has_target(db, "gcc"));
}

TEST(XvmDbTest, FuzzyVersionMatch) {
    xlings::xvm::VersionDB db;
    xlings::xvm::add_version(db, "gcc", "15.1.0", "/usr/bin");
    xlings::xvm::add_version(db, "gcc", "14.2.0", "/opt/gcc14/bin");
    xlings::xvm::add_version(db, "gcc", "14.1.0", "/opt/gcc141/bin");
    xlings::xvm::add_version(db, "gcc", "13.3.0", "/opt/gcc13/bin");

    // Exact match
    EXPECT_EQ(xlings::xvm::match_version(db, "gcc", "15.1.0"), "15.1.0");

    // Prefix match: "15" -> "15.1.0"
    EXPECT_EQ(xlings::xvm::match_version(db, "gcc", "15"), "15.1.0");

    // Prefix match: "14" -> "14.2.0" (highest)
    EXPECT_EQ(xlings::xvm::match_version(db, "gcc", "14"), "14.2.0");

    // Prefix match: "14.1" -> "14.1.0"
    EXPECT_EQ(xlings::xvm::match_version(db, "gcc", "14.1"), "14.1.0");

    // No match
    EXPECT_EQ(xlings::xvm::match_version(db, "gcc", "16"), "");
    EXPECT_EQ(xlings::xvm::match_version(db, "nonexistent", "1"), "");
}

TEST(XvmDbTest, GetActiveVersion) {
    xlings::xvm::Workspace ws;
    ws["gcc"] = "15.1.0";
    ws["node"] = "22.0.0";

    EXPECT_EQ(xlings::xvm::get_active_version(ws, "gcc"), "15.1.0");
    EXPECT_EQ(xlings::xvm::get_active_version(ws, "node"), "22.0.0");
    EXPECT_EQ(xlings::xvm::get_active_version(ws, "python"), "");
}

TEST(XvmDbTest, GetVDataAndVInfo) {
    xlings::xvm::VersionDB db;
    xlings::xvm::add_version(db, "gcc", "15.1.0", "/usr/bin", "program", "gcc");

    auto* vinfo = xlings::xvm::get_vinfo(db, "gcc");
    ASSERT_NE(vinfo, nullptr);
    EXPECT_EQ(vinfo->type, "program");
    EXPECT_EQ(vinfo->filename, "gcc");

    auto* vdata = xlings::xvm::get_vdata(db, "gcc", "15.1.0");
    ASSERT_NE(vdata, nullptr);
    EXPECT_EQ(vdata->path, "/usr/bin");

    EXPECT_EQ(xlings::xvm::get_vdata(db, "gcc", "99.0.0"), nullptr);
    EXPECT_EQ(xlings::xvm::get_vinfo(db, "nonexistent"), nullptr);
}

TEST(XvmDbTest, GetBinding) {
    xlings::xvm::VersionDB db;
    xlings::xvm::add_version(db, "gcc", "15.1.0", "/usr/bin");
    db["gcc"].bindings["g++"]["15.1.0"] = "g++-15";
    db["gcc"].bindings["g++"]["14.2.0"] = "g++-14";

    EXPECT_EQ(xlings::xvm::get_binding(db, "gcc", "g++", "15.1.0"), "g++-15");
    EXPECT_EQ(xlings::xvm::get_binding(db, "gcc", "g++", "14.2.0"), "g++-14");
    EXPECT_EQ(xlings::xvm::get_binding(db, "gcc", "g++", "99.0.0"), "");
    EXPECT_EQ(xlings::xvm::get_binding(db, "gcc", "clang", "15.1.0"), "");
}

TEST(XvmDbTest, ExpandPath) {
    EXPECT_EQ(xlings::xvm::expand_path("${XLINGS_HOME}/data/xpkgs/gcc", "/home/user/.xlings"),
              "/home/user/.xlings/data/xpkgs/gcc");
    EXPECT_EQ(xlings::xvm::expand_path("/absolute/path", "/home/user/.xlings"),
              "/absolute/path");
    EXPECT_EQ(xlings::xvm::expand_path("${XLINGS_HOME}/a/${XLINGS_HOME}/b", "/X"),
              "/X/a//X/b");
    EXPECT_EQ(xlings::xvm::expand_path("no_placeholder", "/X"), "no_placeholder");
}

// ============================================================
// xvm JSON serialization tests
// ============================================================

TEST(XvmJsonTest, VDataRoundTrip) {
    xlings::xvm::VData original;
    original.path = "/usr/bin";
    original.alias = {"15", "latest"};
    original.envs["GCC_HOME"] = "/usr/lib/gcc";
    original.envs["PATH"] = "/usr/bin";

    auto j = xlings::xvm::vdata_to_json(original);
    auto restored = xlings::xvm::vdata_from_json(j);

    EXPECT_EQ(restored.path, original.path);
    EXPECT_EQ(restored.alias, original.alias);
    EXPECT_EQ(restored.envs, original.envs);
}

TEST(XvmJsonTest, VDataMinimal) {
    xlings::xvm::VData original;
    original.path = "/usr/bin";
    // No alias, no envs

    auto j = xlings::xvm::vdata_to_json(original);
    EXPECT_FALSE(j.contains("alias"));
    EXPECT_FALSE(j.contains("envs"));

    auto restored = xlings::xvm::vdata_from_json(j);
    EXPECT_EQ(restored.path, "/usr/bin");
    EXPECT_TRUE(restored.alias.empty());
    EXPECT_TRUE(restored.envs.empty());
}

TEST(XvmJsonTest, VInfoRoundTrip) {
    xlings::xvm::VInfo original;
    original.type = "program";
    original.filename = "gcc";
    original.versions["15.1.0"].path = "/usr/bin";
    original.versions["15.1.0"].alias = {"15"};
    original.versions["14.2.0"].path = "/opt/gcc14/bin";
    original.bindings["g++"]["15.1.0"] = "g++-15";
    original.bindings["g++"]["14.2.0"] = "g++-14";

    auto j = xlings::xvm::vinfo_to_json(original);
    auto restored = xlings::xvm::vinfo_from_json(j);

    EXPECT_EQ(restored.type, "program");
    EXPECT_EQ(restored.filename, "gcc");
    ASSERT_EQ(restored.versions.size(), 2u);
    EXPECT_EQ(restored.versions.at("15.1.0").path, "/usr/bin");
    EXPECT_EQ(restored.versions.at("15.1.0").alias.size(), 1u);
    EXPECT_EQ(restored.versions.at("14.2.0").path, "/opt/gcc14/bin");
    ASSERT_EQ(restored.bindings.size(), 1u);
    EXPECT_EQ(restored.bindings.at("g++").at("15.1.0"), "g++-15");
}

TEST(XvmJsonTest, VersionDBRoundTrip) {
    xlings::xvm::VersionDB db;
    xlings::xvm::add_version(db, "gcc", "15.1.0", "/usr/bin", "program", "gcc");
    xlings::xvm::add_version(db, "gcc", "14.2.0", "/opt/gcc14/bin", "program", "gcc");
    xlings::xvm::add_version(db, "node", "22.0.0", "/opt/node22/bin", "program", "node");

    auto j = xlings::xvm::versions_to_json(db);
    auto restored = xlings::xvm::versions_from_json(j);

    ASSERT_EQ(restored.size(), 2u);
    EXPECT_TRUE(restored.contains("gcc"));
    EXPECT_TRUE(restored.contains("node"));
    EXPECT_EQ(restored.at("gcc").versions.size(), 2u);
    EXPECT_EQ(restored.at("node").versions.size(), 1u);
    EXPECT_EQ(restored.at("gcc").type, "program");
}

TEST(XvmJsonTest, WorkspaceRoundTrip) {
    xlings::xvm::Workspace ws;
    ws["gcc"] = "15.1.0";
    ws["node"] = "22.0.0";

    auto j = xlings::xvm::workspace_to_json(ws);
    auto restored = xlings::xvm::workspace_from_json(j);

    ASSERT_EQ(restored.size(), 2u);
    EXPECT_EQ(restored.at("gcc"), "15.1.0");
    EXPECT_EQ(restored.at("node"), "22.0.0");
}

TEST(XvmJsonTest, FromJsonEmptyObject) {
    auto j = nlohmann::json::object();
    auto db = xlings::xvm::versions_from_json(j);
    EXPECT_TRUE(db.empty());

    auto ws = xlings::xvm::workspace_from_json(j);
    EXPECT_TRUE(ws.empty());
}

TEST(XvmJsonTest, FromJsonNonObject) {
    auto j = nlohmann::json::array();
    auto db = xlings::xvm::versions_from_json(j);
    EXPECT_TRUE(db.empty());

    auto ws = xlings::xvm::workspace_from_json(j);
    EXPECT_TRUE(ws.empty());
}

TEST(XvmJsonTest, WorkspacePlatformAwareManifestParsing) {
    auto j = nlohmann::json::parse(R"({
        "node": {
            "default": "22.17.1",
            "linux": "20.19.0",
            "windows": "22.18.0"
        },
        "python": {
            "default": "3.12.9"
        },
        "rust": {
            "windows": "1.86.0"
        }
    })");

    auto ws = xlings::xvm::workspace_from_json(j);

#if defined(__linux__)
    EXPECT_EQ(ws.at("node"), "20.19.0");
#elif defined(_WIN32)
    EXPECT_EQ(ws.at("node"), "22.18.0");
#else
    EXPECT_EQ(ws.at("node"), "22.17.1");
#endif

    EXPECT_EQ(ws.at("python"), "3.12.9");
#if defined(_WIN32)
    EXPECT_EQ(ws.at("rust"), "1.86.0");
#else
    EXPECT_TRUE(ws.find("rust") == ws.end());
#endif
}

TEST(XvmJsonTest, FullConfigJsonRoundTrip) {
    // Simulate a complete .xlings.json
    std::string configJson = R"({
        "lang": "en",
        "mirror": "GLOBAL",
        "activeSubos": "default",
        "versions": {
            "gcc": {
                "type": "program",
                "filename": "gcc",
                "versions": {
                    "15.1.0": { "path": "/usr/bin", "alias": ["15"] },
                    "14.2.0": { "path": "/opt/gcc14/bin" }
                },
                "bindings": {
                    "g++": { "15.1.0": "g++-15", "14.2.0": "g++-14" }
                }
            },
            "node": {
                "type": "program",
                "filename": "node",
                "versions": {
                    "22.0.0": { "path": "/opt/node22/bin", "envs": {"NODE_HOME": "/opt/node22"} }
                }
            }
        }
    })";

    auto json = nlohmann::json::parse(configJson);
    auto db = xlings::xvm::versions_from_json(json["versions"]);

    ASSERT_EQ(db.size(), 2u);

    // Check gcc
    auto* gcc = xlings::xvm::get_vinfo(db, "gcc");
    ASSERT_NE(gcc, nullptr);
    EXPECT_EQ(gcc->type, "program");
    EXPECT_EQ(gcc->filename, "gcc");
    ASSERT_EQ(gcc->versions.size(), 2u);
    EXPECT_EQ(gcc->versions.at("15.1.0").path, "/usr/bin");
    ASSERT_EQ(gcc->versions.at("15.1.0").alias.size(), 1u);
    EXPECT_EQ(gcc->versions.at("15.1.0").alias[0], "15");
    EXPECT_EQ(gcc->versions.at("14.2.0").path, "/opt/gcc14/bin");
    EXPECT_EQ(gcc->bindings.at("g++").at("15.1.0"), "g++-15");

    // Check node
    auto* node_vdata = xlings::xvm::get_vdata(db, "node", "22.0.0");
    ASSERT_NE(node_vdata, nullptr);
    EXPECT_EQ(node_vdata->path, "/opt/node22/bin");
    EXPECT_EQ(node_vdata->envs.at("NODE_HOME"), "/opt/node22");

    // Fuzzy match
    EXPECT_EQ(xlings::xvm::match_version(db, "gcc", "15"), "15.1.0");
    EXPECT_EQ(xlings::xvm::match_version(db, "gcc", "14"), "14.2.0");

    // Serialize back and verify
    auto j2 = xlings::xvm::versions_to_json(db);
    auto db2 = xlings::xvm::versions_from_json(j2);
    EXPECT_EQ(db2.size(), db.size());
}

// ============================================================
// xvm shim tests
// ============================================================

TEST(XvmShimTest, ExtractProgramName) {
    EXPECT_EQ(xlings::xvm::extract_program_name("/usr/bin/gcc"), "gcc");
    EXPECT_EQ(xlings::xvm::extract_program_name("./gcc"), "gcc");
    EXPECT_EQ(xlings::xvm::extract_program_name("gcc"), "gcc");
    EXPECT_EQ(xlings::xvm::extract_program_name("/home/user/.xlings/subos/default/bin/node"), "node");
    EXPECT_EQ(xlings::xvm::extract_program_name("/path/to/xlings"), "xlings");
}

TEST(XvmShimTest, ResolveExecutableFindsProgram) {
    // resolve_executable only looks up program_name as a file.
    // Alias handling is done separately in shim_dispatch via platform::exec.
    namespace fs = std::filesystem;
    auto testDir = fs::temp_directory_path() / "xlings_env_alias_test";
    fs::remove_all(testDir);
    fs::create_directories(testDir / "bin");

    // Create a "gcc" binary but no "cc"
    auto gcc_path = testDir / "bin" / "gcc";
    xlings::platform::write_string_to_file(gcc_path.string(), "#!/bin/sh\n");

    // "cc" does not exist as a file → empty
    auto result1 = xlings::xvm::resolve_executable("cc", testDir.string(), "");
    EXPECT_TRUE(result1.empty());

    // "gcc" exists under bin/ → found
    auto result2 = xlings::xvm::resolve_executable("gcc", testDir.string(), "");
    EXPECT_FALSE(result2.empty());
    EXPECT_EQ(result2, testDir / "bin" / "gcc");

    fs::remove_all(testDir);
}

TEST(XvmShimTest, IsXlingsBinary) {
    EXPECT_TRUE(xlings::xvm::is_xlings_binary("xlings"));
    EXPECT_TRUE(xlings::xvm::is_xlings_binary("xim"));
    EXPECT_FALSE(xlings::xvm::is_xlings_binary("gcc"));
    EXPECT_FALSE(xlings::xvm::is_xlings_binary("node"));
    EXPECT_FALSE(xlings::xvm::is_xlings_binary("g++"));
    EXPECT_FALSE(xlings::xvm::is_xlings_binary(""));
}

// ============================================================
// xvm config integration tests (filesystem-based)
// ============================================================

class XvmConfigTest : public ::testing::Test {
protected:
    std::filesystem::path testDir_;

    void SetUp() override {
        namespace fs = std::filesystem;
        testDir_ = fs::temp_directory_path() / "xlings_xvm_test";
        fs::remove_all(testDir_);
        fs::create_directories(testDir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(testDir_);
    }
};

TEST_F(XvmConfigTest, WriteAndReadGlobalConfig) {
    namespace fs = std::filesystem;

    // Build a VersionDB
    xlings::xvm::VersionDB db;
    xlings::xvm::add_version(db, "gcc", "15.1.0", "/usr/bin", "program", "gcc");
    xlings::xvm::add_version(db, "gcc", "14.2.0", "${XLINGS_HOME}/data/xpkgs/gcc/14.2.0/bin", "program", "gcc");
    db["gcc"].bindings["g++"]["15.1.0"] = "g++-15";
    db["gcc"].bindings["g++"]["14.2.0"] = "g++-14";

    // Build a config JSON
    nlohmann::json config;
    config["lang"] = "en";
    config["mirror"] = "GLOBAL";
    config["activeSubos"] = "default";
    config["versions"] = xlings::xvm::versions_to_json(db);
    config["subos"] = nlohmann::json::object();
    config["subos"]["default"] = nlohmann::json::object();

    // Write to file
    auto configPath = testDir_ / ".xlings.json";
    xlings::platform::write_string_to_file(configPath.string(), config.dump(2));

    ASSERT_TRUE(fs::exists(configPath));

    // Read back and verify
    auto content = xlings::platform::read_file_to_string(configPath.string());
    auto parsed = nlohmann::json::parse(content);

    EXPECT_EQ(parsed["lang"].get<std::string>(), "en");
    EXPECT_EQ(parsed["mirror"].get<std::string>(), "GLOBAL");
    EXPECT_EQ(parsed["activeSubos"].get<std::string>(), "default");

    auto restored_db = xlings::xvm::versions_from_json(parsed["versions"]);
    EXPECT_EQ(restored_db.size(), 1u);
    EXPECT_TRUE(xlings::xvm::has_version(restored_db, "gcc", "15.1.0"));
    EXPECT_TRUE(xlings::xvm::has_version(restored_db, "gcc", "14.2.0"));
    EXPECT_EQ(xlings::xvm::get_binding(restored_db, "gcc", "g++", "15.1.0"), "g++-15");
}

TEST_F(XvmConfigTest, WriteAndReadSubosWorkspace) {
    namespace fs = std::filesystem;

    // Create subos directory
    auto subosDir = testDir_ / "subos" / "default";
    fs::create_directories(subosDir);

    // Write workspace
    xlings::xvm::Workspace ws;
    ws["gcc"] = "15.1.0";
    ws["node"] = "22.0.0";

    nlohmann::json subosConfig;
    subosConfig["workspace"] = xlings::xvm::workspace_to_json(ws);

    auto configPath = subosDir / ".xlings.json";
    xlings::platform::write_string_to_file(configPath.string(), subosConfig.dump(2));
    ASSERT_TRUE(fs::exists(configPath));

    // Read back
    auto content = xlings::platform::read_file_to_string(configPath.string());
    auto parsed = nlohmann::json::parse(content);
    auto restored_ws = xlings::xvm::workspace_from_json(parsed["workspace"]);

    EXPECT_EQ(restored_ws.size(), 2u);
    EXPECT_EQ(restored_ws["gcc"], "15.1.0");
    EXPECT_EQ(restored_ws["node"], "22.0.0");
}

TEST_F(XvmConfigTest, ProjectConfigOverridesWorkspace) {
    // Simulate: subos workspace has gcc=15.1.0, project has gcc=14.2.0
    xlings::xvm::Workspace subosWs;
    subosWs["gcc"] = "15.1.0";
    subosWs["node"] = "22.0.0";

    xlings::xvm::Workspace projectWs;
    projectWs["gcc"] = "14.2.0";

    // Manual merge (simulating Config::effective_workspace logic)
    xlings::xvm::Workspace effective = subosWs;
    for (auto it = projectWs.begin(); it != projectWs.end(); ++it) {
        effective[it->first] = it->second;
    }

    EXPECT_EQ(effective["gcc"], "14.2.0");   // project overrides
    EXPECT_EQ(effective["node"], "22.0.0");  // subos preserved
}

TEST_F(XvmConfigTest, CreateSubosDirectoryStructure) {
    namespace fs = std::filesystem;

    auto subosDir = testDir_ / "subos" / "dev";
    fs::create_directories(subosDir / "bin");
    fs::create_directories(subosDir / "lib");
    fs::create_directories(subosDir / "usr");
    fs::create_directories(subosDir / "generations");

    // Write empty workspace
    nlohmann::json subosConfig;
    subosConfig["workspace"] = nlohmann::json::object();
    auto configPath = subosDir / ".xlings.json";
    xlings::platform::write_string_to_file(configPath.string(), subosConfig.dump(2));

    EXPECT_TRUE(fs::exists(subosDir / "bin"));
    EXPECT_TRUE(fs::exists(subosDir / "lib"));
    EXPECT_TRUE(fs::exists(subosDir / "usr"));
    EXPECT_TRUE(fs::exists(subosDir / "generations"));
    EXPECT_TRUE(fs::exists(configPath));

    // Verify config content
    auto content = xlings::platform::read_file_to_string(configPath.string());
    auto parsed = nlohmann::json::parse(content);
    EXPECT_TRUE(parsed.contains("workspace"));
    EXPECT_TRUE(parsed["workspace"].is_object());
    EXPECT_TRUE(parsed["workspace"].empty());
}

// ============================================================
// xvm VData new fields (includedir/libdir) tests
// ============================================================

TEST(XvmVDataFieldsTest, IncludedirLibdirConstruction) {
    xlings::xvm::VData vdata;
    vdata.path = "/usr/bin";
    vdata.includedir = "/opt/glibc/2.39/include";
    vdata.libdir = "/opt/glibc/2.39/lib64";

    EXPECT_EQ(vdata.includedir, "/opt/glibc/2.39/include");
    EXPECT_EQ(vdata.libdir, "/opt/glibc/2.39/lib64");
}

TEST(XvmVDataFieldsTest, IncludedirLibdirJsonRoundTrip) {
    xlings::xvm::VData original;
    original.path = "/opt/openssl/3.1.5";
    original.includedir = "/opt/openssl/3.1.5/include";
    original.libdir = "/opt/openssl/3.1.5/lib64";
    original.alias = {"3.1"};

    auto j = xlings::xvm::vdata_to_json(original);
    EXPECT_EQ(j["includedir"].get<std::string>(), "/opt/openssl/3.1.5/include");
    EXPECT_EQ(j["libdir"].get<std::string>(), "/opt/openssl/3.1.5/lib64");

    auto restored = xlings::xvm::vdata_from_json(j);
    EXPECT_EQ(restored.path, original.path);
    EXPECT_EQ(restored.includedir, original.includedir);
    EXPECT_EQ(restored.libdir, original.libdir);
    EXPECT_EQ(restored.alias, original.alias);
}

TEST(XvmVDataFieldsTest, EmptyIncludedirLibdirNotSerialized) {
    xlings::xvm::VData vdata;
    vdata.path = "/usr/bin";
    // includedir and libdir are empty

    auto j = xlings::xvm::vdata_to_json(vdata);
    EXPECT_FALSE(j.contains("includedir"));
    EXPECT_FALSE(j.contains("libdir"));

    auto restored = xlings::xvm::vdata_from_json(j);
    EXPECT_TRUE(restored.includedir.empty());
    EXPECT_TRUE(restored.libdir.empty());
}

TEST(XvmVDataFieldsTest, FullConfigWithNewFields) {
    std::string configJson = R"({
        "versions": {
            "glibc": {
                "type": "program",
                "versions": {
                    "2.39": {
                        "path": "/opt/glibc/2.39",
                        "includedir": "/opt/glibc/2.39/include",
                        "libdir": "/opt/glibc/2.39/lib64"
                    }
                }
            }
        }
    })";

    auto json = nlohmann::json::parse(configJson);
    auto db = xlings::xvm::versions_from_json(json["versions"]);

    auto* vdata = xlings::xvm::get_vdata(db, "glibc", "2.39");
    ASSERT_NE(vdata, nullptr);
    EXPECT_EQ(vdata->path, "/opt/glibc/2.39");
    EXPECT_EQ(vdata->includedir, "/opt/glibc/2.39/include");
    EXPECT_EQ(vdata->libdir, "/opt/glibc/2.39/lib64");

    // Round-trip
    auto j2 = xlings::xvm::versions_to_json(db);
    auto db2 = xlings::xvm::versions_from_json(j2);
    auto* vdata2 = xlings::xvm::get_vdata(db2, "glibc", "2.39");
    ASSERT_NE(vdata2, nullptr);
    EXPECT_EQ(vdata2->includedir, "/opt/glibc/2.39/include");
    EXPECT_EQ(vdata2->libdir, "/opt/glibc/2.39/lib64");
}

// ============================================================
// xvm header symlink tests (filesystem-based)
// ============================================================

class XvmHeaderSymlinkTest : public ::testing::Test {
protected:
    std::filesystem::path testDir_;

    void SetUp() override {
        namespace fs = std::filesystem;
        testDir_ = fs::temp_directory_path() / "xlings_xvm_header_test";
        fs::remove_all(testDir_);
        fs::create_directories(testDir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(testDir_);
    }
};

TEST_F(XvmHeaderSymlinkTest, InstallAndRemoveHeaders) {
    namespace fs = std::filesystem;

    // Create a fake include directory with headers
    auto srcInclude = testDir_ / "pkg" / "include";
    fs::create_directories(srcInclude / "bits");
    xlings::platform::write_string_to_file((srcInclude / "stdio.h").string(), "/* stdio */");
    xlings::platform::write_string_to_file((srcInclude / "bits" / "types.h").string(), "/* types */");

    // Install headers
    auto sysrootInclude = testDir_ / "sysroot" / "usr" / "include";
    xlings::xvm::install_headers(srcInclude.string(), sysrootInclude);

    // Verify links created (symlinks on Unix, hard links/copies on Windows)
    EXPECT_TRUE(fs::exists(sysrootInclude / "stdio.h"));
    EXPECT_TRUE(fs::exists(sysrootInclude / "bits"));
#if !defined(_WIN32)
    EXPECT_TRUE(fs::is_symlink(sysrootInclude / "stdio.h"));
    EXPECT_EQ(fs::read_symlink(sysrootInclude / "stdio.h").string(),
              (srcInclude / "stdio.h").string());
#endif

    // Remove headers
    xlings::xvm::remove_headers(srcInclude.string(), sysrootInclude);

    // Verify links removed
    EXPECT_FALSE(fs::exists(sysrootInclude / "stdio.h"));
    EXPECT_FALSE(fs::exists(sysrootInclude / "bits"));
}

TEST_F(XvmHeaderSymlinkTest, InstallHeadersOverwrite) {
    namespace fs = std::filesystem;

    auto srcInclude1 = testDir_ / "pkg1" / "include";
    auto srcInclude2 = testDir_ / "pkg2" / "include";
    fs::create_directories(srcInclude1);
    fs::create_directories(srcInclude2);
    xlings::platform::write_string_to_file((srcInclude1 / "common.h").string(), "/* v1 */");
    xlings::platform::write_string_to_file((srcInclude2 / "common.h").string(), "/* v2 */");

    auto sysrootInclude = testDir_ / "sysroot" / "usr" / "include";

    // Install first, then overwrite with second
    xlings::xvm::install_headers(srcInclude1.string(), sysrootInclude);
    EXPECT_TRUE(fs::exists(sysrootInclude / "common.h"));
#if !defined(_WIN32)
    EXPECT_TRUE(fs::is_symlink(sysrootInclude / "common.h"));
    EXPECT_EQ(fs::read_symlink(sysrootInclude / "common.h").string(),
              (srcInclude1 / "common.h").string());
#endif

    xlings::xvm::install_headers(srcInclude2.string(), sysrootInclude);
    EXPECT_TRUE(fs::exists(sysrootInclude / "common.h"));
#if !defined(_WIN32)
    EXPECT_TRUE(fs::is_symlink(sysrootInclude / "common.h"));
    EXPECT_EQ(fs::read_symlink(sysrootInclude / "common.h").string(),
              (srcInclude2 / "common.h").string());
#endif
}

TEST_F(XvmHeaderSymlinkTest, RemoveHeadersNonexistentDir) {
    namespace fs = std::filesystem;
    auto sysrootInclude = testDir_ / "sysroot" / "usr" / "include";
    // Should not crash with nonexistent source dir
    xlings::xvm::remove_headers("/tmp/nonexistent_dir_xyz_999", sysrootInclude);
    xlings::xvm::remove_headers("", sysrootInclude);
}

// ============================================================
// xim sub-index repos tests
// ============================================================

TEST(XimSubReposTest, DiscoverSubReposFromLuaFile) {
    namespace fs = std::filesystem;
    auto testDir = fs::temp_directory_path() / "xlings_subrepo_test";
    fs::remove_all(testDir);
    fs::create_directories(testDir);

    // Write a mock xim-indexrepos.lua
    std::string lua = R"(xim_indexrepos = {
    ["awesome"] = {
        ["GLOBAL"] = "https://github.com/d2learn/xim-pkgindex-awesome.git",
        ["CN"] = "https://gitee.com/d2learn/xim-pkgindex-awesome.git",
    },
    ["scode"] = {
        ["GLOBAL"] = "https://github.com/d2learn/xim-pkgindex-scode.git",
    }
}
)";
    xlings::platform::write_string_to_file(
        (testDir / "xim-indexrepos.lua").string(), lua);

    // Test GLOBAL mirror
    auto repos = xlings::xim::discover_sub_repos(testDir, "GLOBAL");
    ASSERT_EQ(repos.size(), 2u);
    // Order depends on iteration, so check by name
    bool foundAwesome = false, foundScode = false;
    for (auto& r : repos) {
        if (r.name == "awesome") {
            foundAwesome = true;
            EXPECT_EQ(r.url, "https://github.com/d2learn/xim-pkgindex-awesome.git");
        } else if (r.name == "scode") {
            foundScode = true;
            EXPECT_EQ(r.url, "https://github.com/d2learn/xim-pkgindex-scode.git");
        }
    }
    EXPECT_TRUE(foundAwesome);
    EXPECT_TRUE(foundScode);

    // Test CN mirror — awesome should use CN URL, scode falls back to GLOBAL
    auto reposCN = xlings::xim::discover_sub_repos(testDir, "CN");
    ASSERT_EQ(reposCN.size(), 2u);
    for (auto& r : reposCN) {
        if (r.name == "awesome") {
            EXPECT_EQ(r.url, "https://gitee.com/d2learn/xim-pkgindex-awesome.git");
        } else if (r.name == "scode") {
            EXPECT_EQ(r.url, "https://github.com/d2learn/xim-pkgindex-scode.git");
        }
    }

    fs::remove_all(testDir);
}

TEST(XimSubReposTest, DiscoverSubReposNoFile) {
    namespace fs = std::filesystem;
    auto testDir = fs::temp_directory_path() / "xlings_subrepo_empty";
    fs::remove_all(testDir);
    fs::create_directories(testDir);

    auto repos = xlings::xim::discover_sub_repos(testDir, "GLOBAL");
    EXPECT_TRUE(repos.empty());

    fs::remove_all(testDir);
}

TEST(XimSubReposTest, SyncRepoUrlKeepsGithubOnCNMirror) {
    auto url = xlings::xim::sync_repo_url(
        "https://github.com/d2learn/xim-pkgindex-awesome.git", "CN");
    EXPECT_EQ(url, "https://github.com/d2learn/xim-pkgindex-awesome.git");
}

// ============================================================
// xim add-xpkg / local repo tests
// ============================================================

TEST(XimAddXpkgTest, LocalRepoLetterSubdir) {
    // Verify that add-xpkg places files under pkgs/<letter>/ subdirectory
    // by testing the IndexManager can find packages in that structure
    namespace fs = std::filesystem;
    auto testDir = fs::temp_directory_path() / "xlings_addxpkg_test";
    fs::remove_all(testDir);
    fs::create_directories(testDir / "pkgs" / "t");

    // Create a minimal valid xpkg lua file (libxpkg table format)
    std::string lua = R"(package = {
    spec = "1",
    name = "test-pkg",
    description = "A test package",
    type = "package",
    status = "dev",
    xpm = {
        linux = {
            ["1.0.0"] = {
                url = "https://example.com/test-1.0.0.tar.gz",
                sha256 = "abc123",
            },
        },
    },
}
)";
    xlings::platform::write_string_to_file(
        (testDir / "pkgs" / "t" / "test-pkg.lua").string(), lua);

    // Build index — should find the package
    xlings::xim::IndexManager mgr(testDir);
    auto result = mgr.rebuild();
    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_GE(mgr.size(), 1u);

    // Verify entry exists
    auto* entry = mgr.find_entry("test-pkg");
    EXPECT_NE(entry, nullptr);

    fs::remove_all(testDir);
}

TEST(XimAddXpkgTest, FlatPkgsDirNotIndexed) {
    // Files placed flat in pkgs/ (not in letter subdir) should NOT be found
    namespace fs = std::filesystem;
    auto testDir = fs::temp_directory_path() / "xlings_addxpkg_flat_test";
    fs::remove_all(testDir);
    fs::create_directories(testDir / "pkgs");

    std::string lua = R"(package = {
    spec = "1",
    name = "flat-pkg",
    description = "A flat test package",
    type = "package",
    status = "dev",
    xpm = {
        linux = {
            ["1.0.0"] = {
                url = "https://example.com/flat-1.0.0.tar.gz",
                sha256 = "abc123",
            },
        },
    },
}
)";
    xlings::platform::write_string_to_file(
        (testDir / "pkgs" / "flat-pkg.lua").string(), lua);

    xlings::xim::IndexManager mgr(testDir);
    auto result = mgr.rebuild();
    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_EQ(mgr.size(), 0u);  // flat file not picked up

    fs::remove_all(testDir);
}

// ============================================================
// create_shim / is_builtin_shim tests
// ============================================================

class ShimCreateTest : public ::testing::Test {
protected:
    std::filesystem::path testDir_;

    void SetUp() override {
        namespace fs = std::filesystem;
        testDir_ = fs::temp_directory_path() / "xlings_shim_create_test";
        fs::remove_all(testDir_);
        fs::create_directories(testDir_ / "src");
        fs::create_directories(testDir_ / "dst");
        // Create a small source file to act as the "binary"
        xlings::platform::write_string_to_file(
            (testDir_ / "src" / "xlings").string(), "fake-binary-content");
    }

    void TearDown() override {
        std::filesystem::remove_all(testDir_);
    }
};

TEST_F(ShimCreateTest, CreatesShimOnUnix) {
    namespace fs = std::filesystem;
    auto src = testDir_ / "src" / "xlings";
    auto dst = testDir_ / "dst" / "gcc";
    auto result = xlings::xself::create_shim(src, dst);
#if !defined(_WIN32)
    EXPECT_EQ(result, xlings::xself::LinkResult::Symlink);
    EXPECT_TRUE(fs::is_symlink(dst));
#else
    // On Windows: hardlink or copy
    EXPECT_TRUE(result == xlings::xself::LinkResult::Hardlink ||
                result == xlings::xself::LinkResult::Copy);
    EXPECT_TRUE(fs::exists(dst));
#endif
}

TEST_F(ShimCreateTest, SymlinkIsRelative) {
    namespace fs = std::filesystem;
    auto src = testDir_ / "src" / "xlings";
    auto dst = testDir_ / "dst" / "gcc";
    auto result = xlings::xself::create_shim(src, dst);
#if !defined(_WIN32)
    ASSERT_EQ(result, xlings::xself::LinkResult::Symlink);
    auto link_target = fs::read_symlink(dst);
    EXPECT_TRUE(link_target.is_relative())
        << "symlink should be relative, got: " << link_target;
#endif
}

TEST_F(ShimCreateTest, OverwritesExisting) {
    namespace fs = std::filesystem;
    auto src = testDir_ / "src" / "xlings";
    auto dst = testDir_ / "dst" / "gcc";
    // Create an existing file at dst
    xlings::platform::write_string_to_file(dst.string(), "old-content");
    ASSERT_TRUE(fs::exists(dst));

    auto result = xlings::xself::create_shim(src, dst);
    EXPECT_NE(result, xlings::xself::LinkResult::Failed);
#if !defined(_WIN32)
    EXPECT_TRUE(fs::is_symlink(dst));
#else
    EXPECT_TRUE(fs::exists(dst));
#endif
}

TEST_F(ShimCreateTest, OverwritesExistingSymlink) {
    namespace fs = std::filesystem;
    auto src = testDir_ / "src" / "xlings";
    auto dst = testDir_ / "dst" / "gcc";
#if !defined(_WIN32)
    // Create a dangling symlink at dst
    fs::create_symlink("/nonexistent/path", dst);
    ASSERT_TRUE(fs::is_symlink(dst));

    auto result = xlings::xself::create_shim(src, dst);
    EXPECT_EQ(result, xlings::xself::LinkResult::Symlink);
    // Should now point to the real source
    EXPECT_TRUE(fs::exists(dst));
#endif
}

TEST_F(ShimCreateTest, SourceNotExistReturnsFailed) {
    auto dst = testDir_ / "dst" / "gcc";
    auto result = xlings::xself::create_shim(testDir_ / "nonexistent", dst);
    EXPECT_EQ(result, xlings::xself::LinkResult::Failed);
    EXPECT_FALSE(std::filesystem::exists(dst));
}

TEST_F(ShimCreateTest, IsBuiltinShimCoversAll) {
    // Base shims
    EXPECT_TRUE(xlings::xself::is_builtin_shim("xlings"));
    EXPECT_TRUE(xlings::xself::is_builtin_shim("xim"));
    EXPECT_TRUE(xlings::xself::is_builtin_shim("xinstall"));
    EXPECT_TRUE(xlings::xself::is_builtin_shim("xsubos"));
    EXPECT_TRUE(xlings::xself::is_builtin_shim("xself"));
    // Non-builtin (xmake removed from optional shims)
    EXPECT_FALSE(xlings::xself::is_builtin_shim("xmake"));
    EXPECT_FALSE(xlings::xself::is_builtin_shim("gcc"));
    EXPECT_FALSE(xlings::xself::is_builtin_shim("node"));
    EXPECT_FALSE(xlings::xself::is_builtin_shim(""));
}

TEST_F(ShimCreateTest, EnsureSubosShimsCreatesAll) {
    namespace fs = std::filesystem;
    auto src = testDir_ / "src" / "xlings";
    auto binDir = testDir_ / "dst";

    xlings::xself::ensure_subos_shims(binDir, src, fs::path{});

    for (auto name : {"xlings", "xim", "xinstall", "xsubos", "xself"}) {
        auto shim = binDir / name;
        EXPECT_TRUE(fs::exists(shim)) << "missing shim: " << name;
#if !defined(_WIN32)
        EXPECT_TRUE(fs::is_symlink(shim)) << "shim should be symlink: " << name;
#endif
    }
}

// ============================================================
// xvm "latest" version resolution tests
// ============================================================

TEST(XvmDbTest, MatchLatestPicksHighest) {
    // "latest" isn't handled by match_version — it's handled in cmd_use.
    // But we can verify the underlying sort logic by checking match_version
    // with empty prefix returns nothing (since "" doesn't prefix-match digits),
    // confirming that "latest" needs special handling.
    xlings::xvm::VersionDB db;
    xlings::xvm::add_version(db, "tool", "0.1.3", "/a");
    xlings::xvm::add_version(db, "tool", "0.1.4", "/b");
    xlings::xvm::add_version(db, "tool", "1.0.0", "/c");

    // "latest" should not match any version via fuzzy match
    EXPECT_EQ(xlings::xvm::match_version(db, "tool", "latest"), "");

    // Verify get_all_versions returns all, so cmd_use can sort and pick highest
    auto all = xlings::xvm::get_all_versions(db, "tool");
    EXPECT_EQ(all.size(), 3u);
}

TEST(XvmDbTest, NamespacedVersionMatch) {
    xlings::xvm::VersionDB db;
    xlings::xvm::add_version(db, "gcc", "xim:15.1.0", "/a");
    xlings::xvm::add_version(db, "gcc", "xim:14.2.0", "/b");
    xlings::xvm::add_version(db, "gcc", "13.3.0", "/c");

    // Namespace-qualified match
    EXPECT_EQ(xlings::xvm::match_version(db, "gcc", "xim:15"), "xim:15.1.0");
    EXPECT_EQ(xlings::xvm::match_version(db, "gcc", "xim:14"), "xim:14.2.0");

    // Bare prefix prefers bare versions
    EXPECT_EQ(xlings::xvm::match_version(db, "gcc", "13"), "13.3.0");
}

// ============================================================
// Profile generation tests
// ============================================================

class ProfileTest : public ::testing::Test {
protected:
    std::filesystem::path testDir_;

    void SetUp() override {
        testDir_ = std::filesystem::temp_directory_path() / "xlings_profile_test";
        std::filesystem::create_directories(testDir_);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(testDir_, ec);
    }
};

TEST_F(ProfileTest, LoadCurrentEmpty) {
    // No profile file → returns generation 0
    auto gen = xlings::profile::load_current(testDir_);
    EXPECT_EQ(gen.number, 0);
    EXPECT_TRUE(gen.packages.empty());
}

TEST_F(ProfileTest, CommitAndLoadRoundTrip) {
    std::map<std::string, std::string> packages = {
        {"gcc", "15.1.0"},
        {"node", "22.0.0"},
    };
    int rc = xlings::profile::commit(testDir_, packages, "install gcc+node");
    EXPECT_EQ(rc, 0);

    auto gen = xlings::profile::load_current(testDir_);
    EXPECT_EQ(gen.number, 1);
    EXPECT_EQ(gen.packages.size(), 2u);
    EXPECT_EQ(gen.packages["gcc"], "15.1.0");
    EXPECT_EQ(gen.packages["node"], "22.0.0");

    // Second commit
    packages["python"] = "3.12.0";
    rc = xlings::profile::commit(testDir_, packages, "add python");
    EXPECT_EQ(rc, 0);

    gen = xlings::profile::load_current(testDir_);
    EXPECT_EQ(gen.number, 2);
    EXPECT_EQ(gen.packages.size(), 3u);
}

TEST_F(ProfileTest, ListGenerations) {
    std::map<std::string, std::string> p1 = {{"gcc", "15.1.0"}};
    std::map<std::string, std::string> p2 = {{"gcc", "15.1.0"}, {"node", "22.0.0"}};

    xlings::profile::commit(testDir_, p1, "install gcc");
    xlings::profile::commit(testDir_, p2, "add node");

    auto gens = xlings::profile::list_generations(testDir_);
    EXPECT_EQ(gens.size(), 2u);
    EXPECT_EQ(gens[0].number, 1);
    EXPECT_EQ(gens[0].packages.size(), 1u);
    EXPECT_EQ(gens[1].number, 2);
    EXPECT_EQ(gens[1].packages.size(), 2u);
}

TEST_F(ProfileTest, Rollback) {
    std::map<std::string, std::string> p1 = {{"gcc", "15.1.0"}};
    std::map<std::string, std::string> p2 = {{"gcc", "15.1.0"}, {"node", "22.0.0"}};

    xlings::profile::commit(testDir_, p1, "install gcc");
    xlings::profile::commit(testDir_, p2, "add node");

    int rc = xlings::profile::rollback(testDir_, 1);
    EXPECT_EQ(rc, 0);

    auto gen = xlings::profile::load_current(testDir_);
    EXPECT_EQ(gen.number, 1);
    EXPECT_EQ(gen.packages.size(), 1u);
    EXPECT_EQ(gen.packages["gcc"], "15.1.0");
}

TEST_F(ProfileTest, RollbackNonexistentFails) {
    int rc = xlings::profile::rollback(testDir_, 99);
    EXPECT_EQ(rc, 1);
}

TEST_F(ProfileTest, FindSubosReferencingEmpty) {
    // No subos dir → empty result
    auto refs = xlings::profile::find_subos_referencing(testDir_, "gcc");
    EXPECT_TRUE(refs.empty());
}

// ============================================================
// Log system extended tests
// ============================================================

TEST(LogTest, GetLevelReturnsSetValue) {
    xlings::log::set_level(xlings::log::Level::Debug);
    EXPECT_EQ(xlings::log::get_level(), xlings::log::Level::Debug);

    xlings::log::set_level(xlings::log::Level::Warn);
    EXPECT_EQ(xlings::log::get_level(), xlings::log::Level::Warn);

    // Restore
    xlings::log::set_level(xlings::log::Level::Info);
}

TEST(LogTest, LevelStringMatchesLevel) {
    xlings::log::set_level(xlings::log::Level::Debug);
    EXPECT_EQ(xlings::log::level_string(), "debug");

    xlings::log::set_level(xlings::log::Level::Info);
    EXPECT_EQ(xlings::log::level_string(), "info");

    xlings::log::set_level(xlings::log::Level::Warn);
    EXPECT_EQ(xlings::log::level_string(), "warn");

    xlings::log::set_level(xlings::log::Level::Error);
    EXPECT_EQ(xlings::log::level_string(), "error");

    // Restore
    xlings::log::set_level(xlings::log::Level::Info);
}

TEST(LogTest, EnableColorToggle) {
    // Should not crash and should be toggleable
    xlings::log::enable_color(false);
    xlings::log::info("no color test");
    xlings::log::enable_color(true);
    xlings::log::info("color test");
}

TEST(LogTest, LevelFiltering) {
    namespace fs = std::filesystem;
    // Use a unique file name to avoid conflicts with other log tests
    auto tmpFile = fs::temp_directory_path() / "xlings_test_log_filter2.txt";
    std::error_code ec;
    fs::remove(tmpFile, ec);

    // Save and restore level around test
    auto savedLevel = xlings::log::get_level();

    xlings::log::set_level(xlings::log::Level::Warn);
    xlings::log::set_file(tmpFile);

    xlings::log::debug("should_not_appear_debug");
    xlings::log::info("should_not_appear_info");
    xlings::log::warn("warn_visible");
    xlings::log::error("error_visible");

    // Close the log file before reading
    xlings::log::set_file("");

    // Read and verify
    std::ifstream f(tmpFile);
    if (!f.is_open()) {
        // On some platforms the file might not be created if ofstream has issues
        // Skip rather than fail hard
        xlings::log::set_level(savedLevel);
        GTEST_SKIP() << "Could not open log file for reading";
    }
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    f.close();

    EXPECT_EQ(content.find("should_not_appear"), std::string::npos);
    EXPECT_NE(content.find("warn_visible"), std::string::npos);
    EXPECT_NE(content.find("error_visible"), std::string::npos);

    fs::remove(tmpFile, ec);
    xlings::log::set_level(savedLevel);
}

// ═══════════════════════════════════════════════════════════════
//  EventStream tests
// ═══════════════════════════════════════════════════════════════

TEST(Event, ProgressEventConstruction) {
    xlings::ProgressEvent e{
        .phase = xlings::Phase::downloading,
        .percent = 0.5f,
        .message = "Downloading gcc-15..."
    };
    EXPECT_EQ(e.phase, xlings::Phase::downloading);
    EXPECT_FLOAT_EQ(e.percent, 0.5f);
    EXPECT_EQ(e.message, "Downloading gcc-15...");
}

TEST(Event, PromptEventConstruction) {
    xlings::PromptEvent e{
        .id = "p1",
        .question = "Override existing?",
        .options = {"y", "n"},
        .defaultValue = "n"
    };
    EXPECT_EQ(e.id, "p1");
    EXPECT_EQ(e.options.size(), 2);
    EXPECT_EQ(e.defaultValue, "n");
}

TEST(Event, VariantHoldsTypes) {
    xlings::Event ev = xlings::LogEvent{xlings::LogLevel::info, "hello"};
    EXPECT_TRUE(std::holds_alternative<xlings::LogEvent>(ev));

    ev = xlings::ErrorEvent{.code = 42, .message = "fail", .recoverable = true};
    auto& err = std::get<xlings::ErrorEvent>(ev);
    EXPECT_EQ(err.code, 42);
    EXPECT_TRUE(err.recoverable);
}

TEST(Event, CompletedEvent) {
    xlings::Event ev = xlings::CompletedEvent{.success = true, .summary = "done"};
    auto& c = std::get<xlings::CompletedEvent>(ev);
    EXPECT_TRUE(c.success);
}

TEST(Event, DataEvent) {
    xlings::Event ev = xlings::DataEvent{.kind = "search_results", .json = R"({"count":3})"};
    auto& d = std::get<xlings::DataEvent>(ev);
    EXPECT_EQ(d.kind, "search_results");
}

// ============================================================
// EventStream tests
// ============================================================

TEST(EventStream, EmitAndConsume) {
    xlings::EventStream stream;
    std::vector<xlings::Event> received;

    stream.on_event([&](const xlings::Event& e) {
        received.push_back(e);
    });

    stream.emit(xlings::LogEvent{xlings::LogLevel::info, "hello"});
    stream.emit(xlings::ProgressEvent{xlings::Phase::downloading, 0.5f, "..."});

    ASSERT_EQ(received.size(), 2);
    EXPECT_TRUE(std::holds_alternative<xlings::LogEvent>(received[0]));
    EXPECT_TRUE(std::holds_alternative<xlings::ProgressEvent>(received[1]));
}

TEST(EventStream, MultipleConsumers) {
    xlings::EventStream stream;
    int count_a = 0, count_b = 0;

    stream.on_event([&](const xlings::Event&) { ++count_a; });
    stream.on_event([&](const xlings::Event&) { ++count_b; });

    stream.emit(xlings::LogEvent{xlings::LogLevel::info, "test"});

    EXPECT_EQ(count_a, 1);
    EXPECT_EQ(count_b, 1);
}

TEST(EventStream, PromptAndRespond) {
    xlings::EventStream stream;
    std::string captured_question;

    stream.on_event([&](const xlings::Event& e) {
        if (auto* p = std::get_if<xlings::PromptEvent>(&e)) {
            captured_question = p->question;
            stream.respond(p->id, "y");
        }
    });

    auto answer = stream.prompt({
        .id = "p1",
        .question = "Override?",
        .options = {"y", "n"},
        .defaultValue = "n"
    });

    EXPECT_EQ(captured_question, "Override?");
    EXPECT_EQ(answer, "y");
}

TEST(EventStream, PromptDefaultOnEmpty) {
    xlings::EventStream stream;

    stream.on_event([&](const xlings::Event& e) {
        if (auto* p = std::get_if<xlings::PromptEvent>(&e)) {
            stream.respond(p->id, p->defaultValue);
        }
    });

    auto answer = stream.prompt({
        .id = "p2",
        .question = "Continue?",
        .options = {},
        .defaultValue = "yes"
    });
    EXPECT_EQ(answer, "yes");
}

TEST(EventStream, PromptBlocksUntilRespond) {
    xlings::EventStream stream;
    std::atomic<bool> promptReturned { false };
    std::string answer;

    stream.on_event([](const xlings::Event&) {});

    std::thread taskThread([&] {
        answer = stream.prompt({
            .id = "p_async",
            .question = "Confirm?",
            .options = {"y", "n"},
            .defaultValue = "n"
        });
        promptReturned.store(true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(promptReturned.load());

    stream.respond("p_async", "confirmed");

    taskThread.join();
    EXPECT_TRUE(promptReturned.load());
    EXPECT_EQ(answer, "confirmed");
}

TEST(EventStream, ConcurrentPromptsFromMultipleTasks) {
    xlings::EventStream stream;
    std::string answer1, answer2;

    stream.on_event([](const xlings::Event&) {});

    std::thread t1([&] {
        answer1 = stream.prompt({.id = "pa", .question = "Q1"});
    });
    std::thread t2([&] {
        answer2 = stream.prompt({.id = "pb", .question = "Q2"});
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    stream.respond("pb", "ans_b");
    stream.respond("pa", "ans_a");

    t1.join();
    t2.join();

    EXPECT_EQ(answer1, "ans_a");
    EXPECT_EQ(answer2, "ans_b");
}

// ============================================================
// ─── Mock Capabilities for testing ───
// ============================================================

namespace {

class MockSearchCapability : public xlings::capability::Capability {
public:
    auto spec() const -> xlings::capability::CapabilitySpec override {
        return {
            .name = "search_packages",
            .description = "Search for packages",
            .inputSchema = R"({"type":"object","properties":{"query":{"type":"string"}}})",
            .outputSchema = R"({"type":"object","properties":{"results":{"type":"array"}}})",
            .destructive = false,
            .asyncCapable = true
        };
    }

    auto execute(xlings::capability::Params params,
                 xlings::EventStream& stream) -> xlings::capability::Result override {
        stream.emit(xlings::LogEvent{xlings::LogLevel::info, "Searching..."});
        stream.emit(xlings::DataEvent{.kind = "search_results", .json = R"({"results":["gcc","g++"]})"});
        stream.emit(xlings::CompletedEvent{.success = true, .summary = "Found 2 packages"});
        return R"({"count":2})";
    }
};

class MockInstallCapability : public xlings::capability::Capability {
public:
    auto spec() const -> xlings::capability::CapabilitySpec override {
        return {
            .name = "install_package",
            .description = "Install a package",
            .inputSchema = R"({"type":"object","properties":{"name":{"type":"string"}}})",
            .outputSchema = R"({"type":"object","properties":{"status":{"type":"string"}}})",
            .destructive = true,
            .asyncCapable = true
        };
    }

    auto execute(xlings::capability::Params params,
                 xlings::EventStream& stream) -> xlings::capability::Result override {
        stream.emit(xlings::ProgressEvent{xlings::Phase::installing, 0.5f, "Installing..."});
        auto answer = stream.prompt({
            .id = "confirm_install",
            .question = "Proceed with install?",
            .options = {"y", "n"},
            .defaultValue = "y"
        });
        if (answer == "n") {
            return R"({"status":"cancelled"})";
        }
        stream.emit(xlings::CompletedEvent{.success = true, .summary = "Installed"});
        return R"({"status":"ok"})";
    }
};

}  // anonymous namespace

// ============================================================
// ─── Capability Tests ───
// ============================================================

TEST(Capability, RegistryRegisterAndGet) {
    xlings::capability::Registry reg;
    reg.register_capability(std::make_unique<MockSearchCapability>());
    reg.register_capability(std::make_unique<MockInstallCapability>());

    auto* search = reg.get("search_packages");
    ASSERT_NE(search, nullptr);
    EXPECT_EQ(search->spec().name, "search_packages");
    EXPECT_FALSE(search->spec().destructive);

    auto* install = reg.get("install_package");
    ASSERT_NE(install, nullptr);
    EXPECT_TRUE(install->spec().destructive);

    EXPECT_EQ(reg.get("nonexistent"), nullptr);
}

TEST(Capability, RegistryListAll) {
    xlings::capability::Registry reg;
    reg.register_capability(std::make_unique<MockSearchCapability>());
    reg.register_capability(std::make_unique<MockInstallCapability>());

    auto specs = reg.list_all();
    EXPECT_EQ(specs.size(), 2);
}

TEST(Capability, ExecuteWithEventStream) {
    xlings::EventStream stream;
    std::vector<xlings::Event> events;
    stream.on_event([&](const xlings::Event& e) { events.push_back(e); });

    MockSearchCapability search;
    auto result = search.execute(R"({"query":"gcc"})", stream);

    ASSERT_EQ(events.size(), 3);
    EXPECT_TRUE(std::holds_alternative<xlings::LogEvent>(events[0]));
    EXPECT_TRUE(std::holds_alternative<xlings::DataEvent>(events[1]));
    EXPECT_TRUE(std::holds_alternative<xlings::CompletedEvent>(events[2]));
    EXPECT_EQ(result, R"({"count":2})");
}

TEST(Capability, ExecuteWithPrompt) {
    xlings::EventStream stream;
    stream.on_event([&](const xlings::Event& e) {
        if (auto* p = std::get_if<xlings::PromptEvent>(&e)) {
            stream.respond(p->id, "y");
        }
    });

    MockInstallCapability install;
    auto result = install.execute(R"({"name":"gcc"})", stream);
    EXPECT_EQ(result, R"({"status":"ok"})");
}

TEST(Capability, ExecutePromptCancelled) {
    xlings::EventStream stream;
    stream.on_event([&](const xlings::Event& e) {
        if (auto* p = std::get_if<xlings::PromptEvent>(&e)) {
            stream.respond(p->id, "n");
        }
    });

    MockInstallCapability install;
    auto result = install.execute(R"({"name":"gcc"})", stream);
    EXPECT_EQ(result, R"({"status":"cancelled"})");
}

// ============================================================
// ─── TaskManager Tests ───
// ============================================================

TEST(TaskManager, SubmitAndComplete) {
    xlings::capability::Registry reg;
    reg.register_capability(std::make_unique<MockSearchCapability>());

    xlings::task::TaskManager tm { reg };
    auto tid = tm.submit("search_packages", R"({"query":"gcc"})");
    EXPECT_FALSE(tid.empty());

    for (int i { 0 }; i < 100; ++i) {
        if (tm.info(tid).status == xlings::task::TaskStatus::completed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto taskInfo = tm.info(tid);
    EXPECT_EQ(taskInfo.status, xlings::task::TaskStatus::completed);
    EXPECT_EQ(taskInfo.capabilityName, "search_packages");
}

TEST(TaskManager, EventsRetrieval) {
    xlings::capability::Registry reg;
    reg.register_capability(std::make_unique<MockSearchCapability>());

    xlings::task::TaskManager tm { reg };
    auto tid = tm.submit("search_packages", R"({"query":"gcc"})");

    for (int i { 0 }; i < 100; ++i) {
        if (tm.info(tid).status == xlings::task::TaskStatus::completed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto evts = tm.events(tid);
    EXPECT_GE(evts.size(), 3);  // LogEvent + DataEvent + CompletedEvent

    auto evts2 = tm.events(tid, evts.size());
    EXPECT_EQ(evts2.size(), 0);
}

TEST(TaskManager, PromptHandling) {
    xlings::capability::Registry reg;
    reg.register_capability(std::make_unique<MockInstallCapability>());

    xlings::task::TaskManager tm { reg };
    auto tid = tm.submit("install_package", R"({"name":"gcc"})");

    bool foundPrompt { false };
    std::string promptId;
    for (int i { 0 }; i < 100; ++i) {
        auto taskInfo = tm.info(tid);
        if (taskInfo.status == xlings::task::TaskStatus::waiting_prompt) {
            auto evts = tm.events(tid);
            for (auto& rec : evts) {
                if (auto* p = std::get_if<xlings::PromptEvent>(&rec.event)) {
                    promptId = p->id;
                    foundPrompt = true;
                }
            }
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ASSERT_TRUE(foundPrompt);
    tm.respond(tid, promptId, "y");

    for (int i { 0 }; i < 100; ++i) {
        if (tm.info(tid).status == xlings::task::TaskStatus::completed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(tm.info(tid).status, xlings::task::TaskStatus::completed);
}

TEST(TaskManager, ConcurrentTasks) {
    xlings::capability::Registry reg;
    reg.register_capability(std::make_unique<MockSearchCapability>());

    xlings::task::TaskManager tm { reg };
    auto t1 = tm.submit("search_packages", R"({})");
    auto t2 = tm.submit("search_packages", R"({})");
    auto t3 = tm.submit("search_packages", R"({})");

    for (int i { 0 }; i < 200; ++i) {
        if (!tm.has_active_tasks()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_FALSE(tm.has_active_tasks());
    EXPECT_EQ(tm.info(t1).status, xlings::task::TaskStatus::completed);
    EXPECT_EQ(tm.info(t2).status, xlings::task::TaskStatus::completed);
    EXPECT_EQ(tm.info(t3).status, xlings::task::TaskStatus::completed);
}

TEST(TaskManager, InfoAll) {
    xlings::capability::Registry reg;
    reg.register_capability(std::make_unique<MockSearchCapability>());

    xlings::task::TaskManager tm { reg };
    tm.submit("search_packages", R"({})");
    tm.submit("search_packages", R"({})");

    auto all = tm.info_all();
    EXPECT_EQ(all.size(), 2);
}

// ============================================================
// ─── Integration Tests: EventStream + Capability + TaskManager ───
// ============================================================

TEST(Integration, TuiPathSynchronous) {
    // Simulate CLI/TUI path: synchronous call, consumer handles events directly
    xlings::EventStream stream;
    std::vector<std::string> rendered;

    stream.on_event([&](const xlings::Event& e) {
        std::visit([&](auto&& ev) {
            using T = std::decay_t<decltype(ev)>;
            if constexpr (std::is_same_v<T, xlings::ProgressEvent>) {
                rendered.push_back("progress:" + std::to_string(ev.percent));
            } else if constexpr (std::is_same_v<T, xlings::LogEvent>) {
                rendered.push_back("log:" + ev.message);
            } else if constexpr (std::is_same_v<T, xlings::PromptEvent>) {
                rendered.push_back("prompt:" + ev.question);
                stream.respond(ev.id, "y");
            } else if constexpr (std::is_same_v<T, xlings::DataEvent>) {
                rendered.push_back("data:" + ev.kind);
            } else if constexpr (std::is_same_v<T, xlings::CompletedEvent>) {
                rendered.push_back("completed:" + ev.summary);
            }
        }, e);
    });

    MockSearchCapability search;
    search.execute(R"({})", stream);

    ASSERT_EQ(rendered.size(), 3);
    EXPECT_EQ(rendered[0], "log:Searching...");
    EXPECT_EQ(rendered[1], "data:search_results");
    EXPECT_EQ(rendered[2], "completed:Found 2 packages");
}

TEST(Integration, AgentPathConcurrentWithPrompt) {
    // Simulate Agent path: concurrent tasks + prompt handling
    xlings::capability::Registry reg;
    reg.register_capability(std::make_unique<MockInstallCapability>());
    reg.register_capability(std::make_unique<MockSearchCapability>());

    xlings::task::TaskManager tm { reg };

    auto tSearch = tm.submit("search_packages", R"({})");
    auto tInstall = tm.submit("install_package", R"({"name":"gcc"})");

    // Agent main loop: poll events, handle prompts
    bool installDone = false;
    for (int i = 0; i < 200 && !installDone; ++i) {
        auto installInfo = tm.info(tInstall);
        if (installInfo.status == xlings::task::TaskStatus::waiting_prompt) {
            auto evts = tm.events(tInstall);
            for (auto& rec : evts) {
                if (auto* p = std::get_if<xlings::PromptEvent>(&rec.event)) {
                    tm.respond(tInstall, p->id, "y");
                }
            }
        }
        if (installInfo.status == xlings::task::TaskStatus::completed) {
            installDone = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(installDone);
    EXPECT_EQ(tm.info(tSearch).status, xlings::task::TaskStatus::completed);

    // Verify event stream contents
    auto searchEvents = tm.events(tSearch);
    EXPECT_GE(searchEvents.size(), 3);

    auto installEvents = tm.events(tInstall);
    EXPECT_GE(installEvents.size(), 2);  // ProgressEvent + PromptEvent + CompletedEvent
}

// ============================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
