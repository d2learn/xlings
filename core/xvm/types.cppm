export module xlings.xvm.types;

import std;

export namespace xlings::xvm {

struct VData {
    std::string path;
    std::string includedir;  // source header directory (e.g., xpkgs/openssl/3.1.5/include)
    std::string libdir;      // source library directory (e.g., xpkgs/glibc/2.39/lib64)
    std::vector<std::string> alias;
    std::map<std::string, std::string> envs;

#if defined(_MSC_VER)
    VData() = default;
    ~VData() = default;
    VData(const VData&) = default;
    VData& operator=(const VData&) = default;
    VData(VData&&) = default;
    VData& operator=(VData&&) = default;
#else
    VData();
    ~VData();
    VData(const VData&);
    VData& operator=(const VData&);
    VData(VData&&);
    VData& operator=(VData&&);
#endif
};

struct VInfo {
    std::string type;      // "program" | "lib"
    std::string filename;
    std::map<std::string, VData> versions;
    std::map<std::string, std::map<std::string, std::string>> bindings;

#if defined(_MSC_VER)
    VInfo() = default;
    ~VInfo() = default;
    VInfo(const VInfo&) = default;
    VInfo& operator=(const VInfo&) = default;
    VInfo(VInfo&&) = default;
    VInfo& operator=(VInfo&&) = default;
#else
    VInfo();
    ~VInfo();
    VInfo(const VInfo&);
    VInfo& operator=(const VInfo&);
    VInfo(VInfo&&);
    VInfo& operator=(VInfo&&);
#endif
};

using VersionDB = std::map<std::string, VInfo>;
using Workspace = std::map<std::string, std::string>;  // target -> active version

} // namespace xlings::xvm

#if !defined(_MSC_VER)
// Out-of-line special members to work around GCC module boundary issues
xlings::xvm::VData::VData() = default;
xlings::xvm::VData::~VData() = default;
xlings::xvm::VData::VData(const VData&) = default;
xlings::xvm::VData& xlings::xvm::VData::operator=(const VData&) = default;
xlings::xvm::VData::VData(VData&&) = default;
xlings::xvm::VData& xlings::xvm::VData::operator=(VData&&) = default;

xlings::xvm::VInfo::VInfo() = default;
xlings::xvm::VInfo::~VInfo() = default;
xlings::xvm::VInfo::VInfo(const VInfo&) = default;
xlings::xvm::VInfo& xlings::xvm::VInfo::operator=(const VInfo&) = default;
xlings::xvm::VInfo::VInfo(VInfo&&) = default;
xlings::xvm::VInfo& xlings::xvm::VInfo::operator=(VInfo&&) = default;
#endif
