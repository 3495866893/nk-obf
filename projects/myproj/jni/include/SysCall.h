/*
 * SysCall 纯系统调用驱动 — 零内核模块，process_vm_readv/writev
 * 用法: sys_init() → sys_read()/sys_write() → sys_close()
 * 无需任何驱动，Android 内核自带
 */
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/uio.h>       // process_vm_readv / writev
#include <sys/syscall.h>   // __NR_process_vm_readv

// arm64: __NR_process_vm_readv=310, __NR_process_vm_writev=311
#ifndef __NR_process_vm_readv
#define __NR_process_vm_readv  310
#endif
#ifndef __NR_process_vm_writev
#define __NR_process_vm_writev 311
#endif

// ====== 全局状态 ======
static pid_t sys_pid = -1;

// ====== 跨进程读 ======
static inline ssize_t sys_read_raw(pid_t pid, uintptr_t addr, void *buf, size_t size) {
    struct iovec local  = { .iov_base = buf,  .iov_len = size };
    struct iovec remote = { .iov_base = (void*)(addr & 0xFFFFFFFFFFFF), .iov_len = size };
    return syscall(__NR_process_vm_readv, pid, &local, 1, &remote, 1, 0);
}

// ====== 跨进程写 ======
static inline ssize_t sys_write_raw(pid_t pid, uintptr_t addr, const void *buf, size_t size) {
    struct iovec local  = { .iov_base = (void*)buf, .iov_len = size };
    struct iovec remote = { .iov_base = (void*)(addr & 0xFFFFFFFFFFFF), .iov_len = size };
    return syscall(__NR_process_vm_writev, pid, &local, 1, &remote, 1, 0);
}

// ====== 拼接读（处理部分读取） ======
static inline int sys_read(uintptr_t addr, void *buf, size_t size) {
    if (sys_pid <= 0) return 0;
    size_t off = 0;
    while (off < size) {
        ssize_t n = sys_read_raw(sys_pid, addr + off, (char*)buf + off, size - off);
        if (n <= 0) return 0;
        off += n;
    }
    return 1;
}

static inline int sys_write(uintptr_t addr, void *buf, size_t size) {
    if (sys_pid <= 0) return 0;
    size_t off = 0;
    while (off < size) {
        ssize_t n = sys_write_raw(sys_pid, addr + off, (char*)buf + off, size - off);
        if (n <= 0) return 0;
        off += n;
    }
    return 1;
}

// ====== 模板读宏 ======
#define sys_read_val(addr, type) ({ \
    type _v = {0}; \
    sys_read((addr), &_v, sizeof(type)); \
    _v; \
})

// ====== 进程名 → PID ======
static inline pid_t sys_get_pid(const char *name) {
    DIR *dir = opendir("/proc");
    if (!dir) return -1;
    struct dirent *entry;
    char path[256], cmdline[256];
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;
        snprintf(path, sizeof(path), "/proc/%s/cmdline", entry->d_name);
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;
        ssize_t n = read(fd, cmdline, sizeof(cmdline) - 1);
        close(fd);
        if (n <= 0) continue;
        cmdline[n] = '\0';
        if (strstr(cmdline, name)) {
            closedir(dir);
            return atoi(entry->d_name);
        }
    }
    closedir(dir);
    return -1;
}

// ====== 模块基址 ======
static inline uintptr_t sys_module_base(const char *name) {
    if (sys_pid <= 0 || !name) return 0;
    char path[256], line[512];
    snprintf(path, sizeof(path), "/proc/%d/maps", sys_pid);
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, name) && strstr(line, "r-x") && (strstr(line, ".so") || strstr(line, name))) {
            fclose(fp);
            return strtoull(line, NULL, 16);
        }
    }
    // 再找一次，不要求 r-x
    rewind(fp);
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, name)) {
            fclose(fp);
            return strtoull(line, NULL, 16);
        }
    }
    fclose(fp);
    return 0;
}

// ====== 初始化 ======
static inline int sys_init(void) {
    printf("SysCall 驱动就绪 (无需内核模块)\n");
    return 0;
}

static inline void sys_set_pid(pid_t pid) { sys_pid = pid; }

static inline int sys_is_ready(void) { return 1; }

static inline void sys_close(void) { sys_pid = -1; }
