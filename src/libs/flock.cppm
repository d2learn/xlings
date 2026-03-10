module;

#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#endif
#include <cerrno>
#include <cstring>

export module xlings.libs.flock;

import std;

namespace xlings::libs::flock {

export enum class LockType { Shared, Exclusive };

export class FileLock {
#if defined(_WIN32)
    HANDLE hFile_ { INVALID_HANDLE_VALUE };
#else
    int fd_ { -1 };
#endif
    bool locked_ { false };
    std::string path_;

public:
    FileLock() = default;

    explicit FileLock(std::string_view path, LockType type = LockType::Exclusive)
        : path_(path) {
#if defined(_WIN32)
        hFile_ = ::CreateFileA(path_.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile_ == INVALID_HANDLE_VALUE) return;
        OVERLAPPED ov{};
        DWORD flags = (type == LockType::Shared) ? 0 : LOCKFILE_EXCLUSIVE_LOCK;
        if (::LockFileEx(hFile_, flags, 0, MAXDWORD, MAXDWORD, &ov)) {
            locked_ = true;
        }
#else
        fd_ = ::open(path_.c_str(), O_CREAT | O_RDWR, 0644);
        if (fd_ < 0) return;
        int op = (type == LockType::Shared) ? LOCK_SH : LOCK_EX;
        if (::flock(fd_, op) == 0) {
            locked_ = true;
        }
#endif
    }

    ~FileLock() { unlock(); }

    // Move only
    FileLock(FileLock&& o) noexcept
#if defined(_WIN32)
        : hFile_(o.hFile_), locked_(o.locked_), path_(std::move(o.path_)) {
        o.hFile_ = INVALID_HANDLE_VALUE;
#else
        : fd_(o.fd_), locked_(o.locked_), path_(std::move(o.path_)) {
        o.fd_ = -1;
#endif
        o.locked_ = false;
    }

    FileLock& operator=(FileLock&& o) noexcept {
        if (this != &o) {
            unlock();
#if defined(_WIN32)
            hFile_ = o.hFile_;
            o.hFile_ = INVALID_HANDLE_VALUE;
#else
            fd_ = o.fd_;
            o.fd_ = -1;
#endif
            locked_ = o.locked_;
            path_ = std::move(o.path_);
            o.locked_ = false;
        }
        return *this;
    }

    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;

    [[nodiscard]] bool is_locked() const { return locked_; }

    void unlock() {
#if defined(_WIN32)
        if (hFile_ != INVALID_HANDLE_VALUE) {
            if (locked_) {
                OVERLAPPED ov{};
                ::UnlockFileEx(hFile_, 0, MAXDWORD, MAXDWORD, &ov);
                locked_ = false;
            }
            ::CloseHandle(hFile_);
            hFile_ = INVALID_HANDLE_VALUE;
        }
#else
        if (fd_ >= 0) {
            if (locked_) {
                ::flock(fd_, LOCK_UN);
                locked_ = false;
            }
            ::close(fd_);
            fd_ = -1;
        }
#endif
    }

    // Non-blocking try_lock. Returns a FileLock; check is_locked().
    static auto try_lock(std::string_view path, LockType type = LockType::Exclusive) -> FileLock {
        FileLock lk;
        lk.path_ = std::string(path);
#if defined(_WIN32)
        lk.hFile_ = ::CreateFileA(lk.path_.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (lk.hFile_ == INVALID_HANDLE_VALUE) return lk;
        OVERLAPPED ov{};
        DWORD flags = LOCKFILE_FAIL_IMMEDIATELY;
        if (type == LockType::Exclusive) flags |= LOCKFILE_EXCLUSIVE_LOCK;
        if (::LockFileEx(lk.hFile_, flags, 0, MAXDWORD, MAXDWORD, &ov)) {
            lk.locked_ = true;
        }
#else
        lk.fd_ = ::open(lk.path_.c_str(), O_CREAT | O_RDWR, 0644);
        if (lk.fd_ < 0) return lk;
        int op = (type == LockType::Shared) ? (LOCK_SH | LOCK_NB) : (LOCK_EX | LOCK_NB);
        if (::flock(lk.fd_, op) == 0) {
            lk.locked_ = true;
        }
#endif
        return lk;
    }
};

} // namespace xlings::libs::flock
