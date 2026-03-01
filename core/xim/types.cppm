export module xlings.xim.types;

import std;

export namespace xlings::xim {

// Install phase for UI status tracking
enum class InstallPhase {
    Pending,
    Downloading,
    Extracting,
    Building,
    Installing,
    Configuring,
    Done,
    Failed
};

// Status of a single package during install
struct InstallStatus {
    std::string name;
    InstallPhase phase { InstallPhase::Pending };
    float progress { 0.0f };
    std::string message;
};

// A node in the dependency-resolved install plan
struct PlanNode {
    std::string name;
    std::string version;
    std::filesystem::path pkgFile;
    std::vector<std::string> deps;
    bool alreadyInstalled { false };
    bool isSystemPM { false };

    // Explicit special members to work around GCC 15 module linker bug
    PlanNode() = default;
#if defined(_MSC_VER)
    ~PlanNode() = default;
    PlanNode(const PlanNode&) = default;
    PlanNode& operator=(const PlanNode&) = default;
    PlanNode(PlanNode&&) noexcept = default;
    PlanNode& operator=(PlanNode&&) noexcept = default;
#else
    ~PlanNode();
    PlanNode(const PlanNode&);
    PlanNode& operator=(const PlanNode&);
    PlanNode(PlanNode&&) noexcept;
    PlanNode& operator=(PlanNode&&) noexcept;
#endif
};

#if !defined(_MSC_VER)
// Out-of-line definitions force GCC to emit symbols in this TU
PlanNode::~PlanNode() = default;
PlanNode::PlanNode(const PlanNode&) = default;
PlanNode& PlanNode::operator=(const PlanNode&) = default;
PlanNode::PlanNode(PlanNode&&) noexcept = default;
PlanNode& PlanNode::operator=(PlanNode&&) noexcept = default;
#endif

// DAG install plan (topologically sorted)
struct InstallPlan {
    std::vector<PlanNode> nodes;
    std::vector<std::string> errors;

    bool has_errors() const { return !errors.empty(); }
    std::size_t pending_count() const {
        std::size_t n = 0;
        for (auto& nd : nodes)
            if (!nd.alreadyInstalled) ++n;
        return n;
    }

    InstallPlan() = default;
#if defined(_MSC_VER)
    ~InstallPlan() = default;
    InstallPlan(const InstallPlan&) = default;
    InstallPlan& operator=(const InstallPlan&) = default;
    InstallPlan(InstallPlan&&) noexcept = default;
    InstallPlan& operator=(InstallPlan&&) noexcept = default;
#else
    ~InstallPlan();
    InstallPlan(const InstallPlan&);
    InstallPlan& operator=(const InstallPlan&);
    InstallPlan(InstallPlan&&) noexcept;
    InstallPlan& operator=(InstallPlan&&) noexcept;
#endif
};

#if !defined(_MSC_VER)
InstallPlan::~InstallPlan() = default;
InstallPlan::InstallPlan(const InstallPlan&) = default;
InstallPlan& InstallPlan::operator=(const InstallPlan&) = default;
InstallPlan::InstallPlan(InstallPlan&&) noexcept = default;
InstallPlan& InstallPlan::operator=(InstallPlan&&) noexcept = default;
#endif

// Download task for a single resource
struct DownloadTask {
    std::string name;
    std::string url;
    std::string sha256;
    std::filesystem::path destDir;
};

// Result of a download
struct DownloadResult {
    std::string name;
    bool success { false };
    std::string error;
    std::filesystem::path localFile;
};

// Downloader configuration
struct DownloaderConfig {
    int maxConcurrency { 4 };
    std::string preferredMirror;    // "GLOBAL" | "CN"
    bool autoDetectMirror { true };
};

} // namespace xlings::xim
