export module xlings.libs.resource_lock;

import std;
import xlings.libs.flock;
import xlings.platform;

namespace xlings::libs::resource_lock {

// Named resource lock using flock underneath
// Supports shared/exclusive with stale PID detection

export class ResourceLock {
    std::filesystem::path lock_dir_;

    auto lock_path_(std::string_view name) const -> std::filesystem::path {
        std::string safe(name);
        for (auto& c : safe) {
            if (c == '/' || c == ':' || c == '\\') c = '_';
        }
        return lock_dir_ / (safe + ".lock");
    }

    auto pid_path_(std::string_view name) const -> std::filesystem::path {
        std::string safe(name);
        for (auto& c : safe) {
            if (c == '/' || c == ':' || c == '\\') c = '_';
        }
        return lock_dir_ / (safe + ".pid");
    }

public:
    explicit ResourceLock(std::filesystem::path lock_dir)
        : lock_dir_(std::move(lock_dir)) {
        std::filesystem::create_directories(lock_dir_);
    }

    // Acquire exclusive lock (blocking)
    auto acquire(std::string_view name) -> flock::FileLock {
        auto path = lock_path_(name);
        flock::FileLock lk(path.string(), flock::LockType::Exclusive);
        if (lk.is_locked()) write_pid_(name);
        return lk;
    }

    // Try acquire exclusive lock (non-blocking)
    auto try_acquire(std::string_view name) -> flock::FileLock {
        auto path = lock_path_(name);
        auto lk = flock::FileLock::try_lock(path.string(), flock::LockType::Exclusive);
        if (lk.is_locked()) write_pid_(name);
        return lk;
    }

    // Acquire shared lock (blocking)
    auto acquire_shared(std::string_view name) -> flock::FileLock {
        auto path = lock_path_(name);
        return flock::FileLock(path.string(), flock::LockType::Shared);
    }

    // Check if a resource lock is stale (PID no longer exists)
    bool is_stale(std::string_view name) const {
        namespace fs = std::filesystem;
        auto pp = pid_path_(name);
        if (!fs::exists(pp)) return false;

        std::ifstream in(pp);
        int pid = 0;
        in >> pid;
        if (pid <= 0) return true;

        return !platform::is_process_alive(pid);
    }

    // Clean stale lock
    bool clean_stale(std::string_view name) {
        if (!is_stale(name)) return false;
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::remove(lock_path_(name), ec);
        fs::remove(pid_path_(name), ec);
        return true;
    }

private:
    void write_pid_(std::string_view name) {
        auto pp = pid_path_(name);
        std::ofstream out(pp);
        out << platform::get_pid();
    }
};

} // namespace xlings::libs::resource_lock
