#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/syscall.h>
#include <sys/uio.h>

namespace Memory {

#if defined(__aarch64__)
    constexpr int SyscallRead = 270;
    constexpr int SyscallWrite = 271;
#elif defined(__arm__)
    constexpr int SyscallRead = 376;
    constexpr int SyscallWrite = 377;
#elif defined(__i386__)
    constexpr int SyscallRead = 347;
    constexpr int SyscallWrite = 348;
#else
    constexpr int SyscallRead = 310;
    constexpr int SyscallWrite = 311;
#endif

    inline bool Transfer(int pid, void *local, uintptr_t remote, size_t size,
                         bool write) {
        if (pid <= 0 || size == 0) return false;

        iovec localIov[1];
        iovec remoteIov[1];
        localIov[0].iov_base = local;
        localIov[0].iov_len = size;
        remoteIov[0].iov_base = reinterpret_cast<void *>(remote);
        remoteIov[0].iov_len = size;

        const long result = syscall(write ? SyscallWrite : SyscallRead, pid,
                                    localIov, 1UL, remoteIov, 1UL, 0UL);
        return result == static_cast<long>(size);
    }

    inline size_t ScatterRead(int pid, void *buffer, uintptr_t remoteFirst,
                              size_t elementSize, size_t elementCount) {
        if (pid <= 0 || elementSize == 0 || elementCount == 0) return 0;

        constexpr size_t MaxSegments = 256;
        if (elementCount <= MaxSegments) {
            iovec localIov[MaxSegments];
            iovec remoteIov[MaxSegments];

            for (size_t i = 0; i < elementCount; ++i) {
                localIov[i].iov_base =
                    static_cast<uint8_t *>(buffer) + i * elementSize;
                localIov[i].iov_len = elementSize;
                remoteIov[i].iov_base =
                    reinterpret_cast<void *>(remoteFirst + i * elementSize);
                remoteIov[i].iov_len = elementSize;
            }

            const long result = syscall(SyscallRead, pid, localIov, elementCount,
                                        remoteIov, elementCount, 0UL);
            if (result <= 0) return 0;
            return static_cast<size_t>(result) / elementSize;
        }

        const size_t total = elementSize * elementCount;
        if (Transfer(pid, buffer, remoteFirst, total, false)) return elementCount;
        return 0;
    }

    inline int FindPid(std::string_view package) {
        DIR *dir = opendir("/proc");
        if (dir == nullptr) return -1;

        int found = -1;
        char path[64];
        char cmdline[256];

        while (dirent *entry = readdir(dir)) {
            const int pid = atoi(entry->d_name);
            if (pid <= 0) continue;

            snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
            const int fd = open(path, O_RDONLY | O_CLOEXEC);
            if (fd < 0) continue;

            const ssize_t n = read(fd, cmdline, sizeof(cmdline) - 1);
            close(fd);
            if (n <= 0) continue;

            cmdline[n] = '\0';
            if (std::string_view(cmdline) == package) {
                found = pid;
                break;
            }
        }

        closedir(dir);
        return found;
    }

    inline uintptr_t FindModuleBase(int pid, std::string_view module) {
        if (pid <= 0) return 0;

        char path[64];
        char line[1024];
        snprintf(path, sizeof(path), "/proc/%d/maps", pid);

        FILE *file = fopen(path, "r");
        if (file == nullptr) return 0;

        const std::string needle(module);
        uintptr_t base = 0;

        while (fgets(line, sizeof(line), file)) {
            if (strstr(line, needle.c_str()) == nullptr) continue;

            char *dash = strchr(line, '-');
            if (dash != nullptr) {
                *dash = '\0';
                base = strtoull(line, nullptr, 16);
                break;
            }
        }

        fclose(file);
        return base;
    }
}

class Reader {
public:
    Reader() = default;

    ~Reader() = default;

    Reader(int, bool = true, bool = true) {}

    bool IsReady() const { return pid > 0; }

    int Pid() const { return pid; }

    void SetPid(int target) {
        if (target > 0) pid = target;
    }

    int Attach(std::string_view package) {
        const int found = Memory::FindPid(package);
        if (found > 0) pid = found;
        return found;
    }

    bool GetModuleBase(std::string_view module, short, uint64_t *out, bool) {
        if (pid <= 0 || out == nullptr) return false;
        const uintptr_t base = Memory::FindModuleBase(pid, module);
        if (base == 0) return false;
        *out = base;
        return true;
    }

    bool Read(uint64_t address, void *buffer, size_t size) {
        if (address == 0 || buffer == nullptr || size == 0) return false;
        return Memory::Transfer(pid, buffer, address, size, false);
    }

    template <typename T>
    bool Read(uint64_t address, T &out) {
        return Read(address, static_cast<void *>(&out), sizeof(T));
    }

    template <typename T>
    T Read(uint64_t address) {
        T value{};
        Read(address, static_cast<void *>(&value), sizeof(T));
        return value;
    }

    int ReadBytes(uint64_t address, void *buffer, size_t size) {
        return Read(address, buffer, size) ? static_cast<int>(size) : 0;
    }

    bool Write(uint64_t address, const void *buffer, size_t size) {
        if (address == 0 || buffer == nullptr || size == 0) return false;
        return Memory::Transfer(pid, const_cast<void *>(buffer), address, size,
                                true);
    }

    template <typename T>
    bool Write(uint64_t address, const T &value) {
        return Write(address, static_cast<const void *>(&value), sizeof(T));
    }

    int WriteBytes(uint64_t address, const void *buffer, size_t size) {
        return Write(address, buffer, size) ? static_cast<int>(size) : 0;
    }

    template <typename T>
    size_t ReadArray(uint64_t firstAddress, T *out, size_t count) {
        return Memory::ScatterRead(pid, out, firstAddress, sizeof(T), count);
    }

    template <typename T>
    size_t ReadArray(uint64_t firstAddress, std::vector<T> &out, size_t count) {
        out.clear();
        out.resize(count);
        if (ReadArray(firstAddress, out.data(), count) != count) {
            out.clear();
            return 0;
        }
        return count;
    }

private:
    int pid = -1;
};

extern Reader *dr;
