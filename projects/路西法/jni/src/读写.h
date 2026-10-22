#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <vector>
#include <string>
#include <map>
#include <algorithm>

#ifndef NATIVESURFACE_MEMREAD_H
#define NATIVESURFACE_MEMREAD_H

#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <malloc.h>
#include <math.h>
#include <thread>
#include <iostream>
#include <sys/stat.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <locale>
#include <codecvt>
#include <dlfcn.h>

typedef unsigned int ADDRESS;
typedef char PACKAGENAME;
typedef unsigned short UTF16;
typedef char UTF8;

int pid;
int getPID(char bm[64]);

#include <sys/ptrace.h>
#include <sys/wait.h>
#include <signal.h>

#ifdef __aarch64__
#include <asm/ptrace.h>
typedef struct user_pt_regs pt_regs;
#else
#include <sys/user.h>
typedef struct user_regs_struct pt_regs;
#endif
#ifndef NT_PRSTATUS
#define NT_PRSTATUS 1
#endif

static inline int ptrace_getregs(pid_t pid, pt_regs *regs) {
#ifdef __aarch64__
    struct iovec iov = {regs, sizeof(*regs)};
    return ptrace(PTRACE_GETREGSET, pid, (void*)NT_PRSTATUS, &iov);
#else
    return ptrace(PTRACE_GETREGS, pid, NULL, regs);
#endif
}

static inline int ptrace_setregs(pid_t pid, pt_regs *regs) {
#ifdef __aarch64__
    struct iovec iov = {regs, sizeof(*regs)};
    return ptrace(PTRACE_SETREGSET, pid, (void*)NT_PRSTATUS, &iov);
#else
    return ptrace(PTRACE_SETREGS, pid, NULL, regs);
#endif
}

static inline int ptrace_call_func(pid_t pid, uintptr_t 实地址, long *params, int num_params, pt_regs *regs) {
#ifdef __aarch64__
    if (num_params > 0) regs->regs[0] = params[0];
    if (num_params > 1) regs->regs[1] = params[1];
    if (num_params > 2) regs->regs[2] = params[2];
    if (num_params > 3) regs->regs[3] = params[3];
    if (num_params > 4) regs->regs[4] = params[4];
    if (num_params > 5) regs->regs[5] = params[5];
    if (num_params > 6) regs->regs[6] = params[6];
    if (num_params > 7) regs->regs[7] = params[7];
    regs->regs[30] = 0;
    regs->pc = 实地址;
#else
    if (num_params > 0) regs->ARM_r0 = params[0];
    regs->ARM_pc = 实地址;
#endif
    return ptrace_setregs(pid, regs);
}

static inline long ptrace_getretval(pt_regs *regs) {
#ifdef __aarch64__
    return regs->regs[0];
#else
    return regs->ARM_r0;
#endif
}

static inline int ptraceAttach(pid_t pid) {
    if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) == -1)
        return -1;
    int status;
    waitpid(pid, &status, 0);
    return 0;
}

static inline int ptraceDetach(pid_t pid) {
    return ptrace(PTRACE_DETACH, pid, NULL, NULL);
}

static inline long call_member_func_bool(pid_t pid, uintptr_t 实地址, uintptr_t 自身, int enable) {
    pt_regs regs, backup;
    if (ptrace_getregs(pid, &backup) == -1) return -1;
    memcpy(&regs, &backup, sizeof(regs));
    long params[2];
    params[0] = 自身;
    params[1] = enable ? 1 : 0;
    if (ptrace_call_func(pid, 实地址, params, 2, &regs) == -1) {
        printf("Failed to call function: %s\n", strerror(errno));
        ptrace_setregs(pid, &backup);
        return -1;
    }
    if (ptrace(PTRACE_CONT, pid, NULL, NULL) == -1) {
        ptrace_setregs(pid, &backup);
        return -1;
    }
    int status;
    if (waitpid(pid, &status, WUNTRACED) == -1) {
        printf("Failed to wait: %s\n", strerror(errno));
        ptrace_setregs(pid, &backup);
        return -1;
    }
    if (!WIFSTOPPED(status)) {
        ptrace_setregs(pid, &backup);
        return -1;
    }
    if (ptrace_getregs(pid, &regs) == -1) {
        ptrace_setregs(pid, &backup);
        return -1;
    }
    long ret = ptrace_getretval(&regs);
    ptrace_setregs(pid, &backup);
    return ret;
}

static inline long call_func_one_arg(pid_t pid, uintptr_t 实地址, uintptr_t arg1) {
    pt_regs regs, backup;
    if (ptrace_getregs(pid, &backup) == -1) return -1;
    memcpy(&regs, &backup, sizeof(regs));
    long params[1];
    params[0] = arg1;
    if (ptrace_call_func(pid, 实地址, params, 1, &regs) == -1) {
        ptrace_setregs(pid, &backup);
        return -1;
    }
    if (ptrace(PTRACE_CONT, pid, NULL, NULL) == -1) {
        ptrace_setregs(pid, &backup);
        return -1;
    }
    int status;
    if (waitpid(pid, &status, WUNTRACED) == -1) {
        ptrace_setregs(pid, &backup);
        return -1;
    }
    if (!WIFSTOPPED(status)) {
        ptrace_setregs(pid, &backup);
        return -1;
    }
    if (ptrace_getregs(pid, &regs) == -1) {
        ptrace_setregs(pid, &backup);
        return -1;
    }
    long ret = ptrace_getretval(&regs);
    ptrace_setregs(pid, &backup);
    return ret;
}

static inline long 衣服call(pid_t pid, uint64_t 基址, uint64_t 自身, int 开关) {
    uint64_t 衣服call地址 = 基址 + 0xEA5AC54;
    return call_member_func_bool(pid, 衣服call地址, 自身, 开关);
}

static inline long 枪械call(pid_t pid, uint64_t 基址, uint64_t 枪械实例, int 开关) {
    uint64_t 枪械call地址 = 基址 + 0xEB72170;
    return call_member_func_bool(pid, 枪械call地址, 枪械实例, 开关);
}

static inline long 载具call(pid_t pid, uint64_t 基址, uint64_t 载具组件) {
    uint64_t 载具call地址 = 基址 + 0xEB65B90;
    return call_func_one_arg(pid, 载具call地址, 载具组件);
}

static inline long 金身(pid_t pid, uint64_t 基址, uint64_t 自身, int 开关) {
    uint64_t 金身地址 = 基址 + 0xa1108b4;
    return call_member_func_bool(pid, 金身地址, 自身, 开关);
}

static inline long 下雨(pid_t pid, uint64_t 基址, uint64_t 自身, int 开关) {
    uint64_t 下雨地址 = 基址 + 0xAEA790C;
    return call_member_func_bool(pid, 下雨地址, 自身, 开关);
}

static inline long 下雪(pid_t pid, uint64_t 基址, uint64_t 自身, int 开关) {
    uint64_t 下雪地址 = 基址 + 0xaea7980;
    return call_member_func_bool(pid, 下雪地址, 自身, 开关);
}

static inline long 暴雪(pid_t pid, uint64_t 基址, uint64_t 自身, int 开关) {
    uint64_t 暴雪地址 = 基址 + 0xaea79f4;
    return call_member_func_bool(pid, 暴雪地址, 自身, 开关);
}

#if defined(__arm__)
int process_vm_readv_syscall = 376;
int process_vm_writev_syscall = 377;
#elif defined(__aarch64__)
int process_vm_readv_syscall = 270;
int process_vm_writev_syscall = 271;
#elif defined(__i386__)
int process_vm_readv_syscall = 347;
int process_vm_writev_syscall = 348;
#else
int process_vm_readv_syscall = 310;
int process_vm_writev_syscall = 311;
#endif

ssize_t process_v(pid_t __pid, const struct iovec *__local_iov, unsigned long __local_iov_count,
                  const struct iovec *__remote_iov, unsigned long __remote_iov_count,
                  unsigned long __flags, bool iswrite) {
    return syscall((iswrite ? process_vm_writev_syscall : process_vm_readv_syscall), __pid,
                   __local_iov, __local_iov_count, __remote_iov, __remote_iov_count, __flags);
}

int getProcessID(const char *packageName) {
    int id = -1;
    DIR *dir;
    FILE *fp;
    char filename[64];
    char cmdline[64];
    struct dirent *entry;
    dir = opendir("/proc");
    while ((entry = readdir(dir)) != NULL) {
        id = atoi(entry->d_name);
        if (id != 0) {
            sprintf(filename, "/proc/%d/cmdline", id);
            fp = fopen(filename, "r");
            if (fp) {
                fgets(cmdline, sizeof(cmdline), fp);
                fclose(fp);
                if (strcmp(packageName, cmdline) == 0) {
                    closedir(dir);
                    return id;
                }
            }
        }
    }
    closedir(dir);
    return -1;
}

bool mem_addr_virtophy(unsigned long vaddr) {
    int pageSize = getpagesize();
    unsigned long v_pageIndex = vaddr / pageSize;
    unsigned long pfn_item_offset = v_pageIndex * sizeof(uint64_t);
    uint64_t item = 0;
    char filename[32];
    snprintf(filename, sizeof(filename), "/proc/%d/pagemap", pid);
    int fd = open(filename, O_RDONLY);
    if (fd < 0) return false;
    if (lseek(fd, pfn_item_offset, SEEK_SET) < 0) { close(fd); return false; }
    if (read(fd, &item, sizeof(uint64_t)) != sizeof(uint64_t)) { close(fd); return false; }
    close(fd);
    return (item & (((uint64_t)1) << 63)) != 0;
}

bool pvm(void *address, void *buffer, size_t size, bool iswrite) {
    struct iovec local[1];
    struct iovec remote[1];
    local[0].iov_base = buffer;
    local[0].iov_len = size;
    remote[0].iov_base = address;
    remote[0].iov_len = size;
    if (pid < 0) return false;
    ssize_t bytes = process_v(pid, local, 1, remote, 1, 0, iswrite);
    return bytes == size;
}

bool vm_readv(unsigned long address, void *buffer, size_t size) {
    return pvm(reinterpret_cast<void *>(address), buffer, size, false);
}

bool vm_writev(unsigned long address, void *buffer, size_t size) {
    return pvm(reinterpret_cast<void *>(address), buffer, size, true);
}

float getfloat(unsigned long addr) { float var = 0; vm_readv(addr, &var, 4); return var; }
float getFloat(unsigned long addr) { float var = 0; vm_readv(addr, &var, 4); return var; }
int getdword(unsigned long addr) { int var = 0; vm_readv(addr, &var, 4); return var; }
int getDword(unsigned long addr) { int var = 0; vm_readv(addr, &var, 4); return var; }

unsigned int getPtr32(unsigned int addr) {
    if (!mem_addr_virtophy(addr) || addr == 0) return 0;
    unsigned int var = 0; vm_readv(addr, &var, 4); return var;
}

unsigned long getPtr64(unsigned long addr) {
    if (!mem_addr_virtophy(addr) || addr == 0) return 0;
    unsigned long var = 0; vm_readv(addr, &var, 8); return var;
}

unsigned long getPtr641(unsigned long addr) {
    unsigned long var = 0; vm_readv(addr, &var, 8); return var;
}

void writefloat(unsigned long addr, float data) { vm_writev(addr, &data, 4); }
void writedword(unsigned long addr, int data) { vm_writev(addr, &data, 4); }

int getPID(const char *packageName) { return getProcessID(packageName); }

unsigned long get_module_base(int pid, const char *module_name) {
    FILE *fp;
    unsigned long addr = 0;
    char *pch;
    char filename[64];
    char line[1024];
    snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);
    fp = fopen(filename, "r");
    if (fp != NULL) {
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, module_name)) {
                pch = strtok(line, "-");
                addr = strtoul(pch, NULL, 16);
                if (addr == 0x8000) addr = 0;
                break;
            }
        }
        fclose(fp);
    }
    return addr;
}

int isapkrunning(PACKAGENAME *bm) {
    DIR *dir = opendir("/proc/");
    if (dir != NULL) {
        struct dirent *ptr;
        FILE *fp;
        char filepath[64], filetext[64];
        while ((ptr = readdir(dir)) != NULL) {
            if ((strcmp(ptr->d_name, ".") == 0) || (strcmp(ptr->d_name, "..") == 0)) continue;
            if (ptr->d_type != DT_DIR) continue;
            sprintf(filepath, "/proc/%s/cmdline", ptr->d_name);
            fp = fopen(filepath, "r");
            if (fp) {
                fgets(filetext, sizeof(filetext), fp);
                fclose(fp);
                if (strcmp(filetext, bm) == 0) { closedir(dir); return 1; }
            }
        }
        closedir(dir);
    }
    return 0;
}

void getUTF8(UTF8 *buf, unsigned long namepy) {
    UTF16 buf16[16] = {0};
    vm_readv(namepy, buf16, 28);
    UTF16 *pTempUTF16 = buf16;
    UTF8 *pTempUTF8 = buf;
    UTF8 *pUTF8End = pTempUTF8 + 32;
    while (pTempUTF16 < buf16 + 14) {
        if (*pTempUTF16 == 0) break;
        if (*pTempUTF16 <= 0x007F && pTempUTF8 + 1 < pUTF8End) {
            *pTempUTF8++ = (UTF8)*pTempUTF16;
        } else if (*pTempUTF16 >= 0x0080 && *pTempUTF16 <= 0x07FF && pTempUTF8 + 2 < pUTF8End) {
            *pTempUTF8++ = (*pTempUTF16 >> 6) | 0xC0;
            *pTempUTF8++ = (*pTempUTF16 & 0x3F) | 0x80;
        } else if (*pTempUTF16 >= 0x0800 && *pTempUTF16 <= 0xFFFF && pTempUTF8 + 3 < pUTF8End) {
            *pTempUTF8++ = (*pTempUTF16 >> 12) | 0xE0;
            *pTempUTF8++ = ((*pTempUTF16 >> 6) & 0x3F) | 0x80;
            *pTempUTF8++ = (*pTempUTF16 & 0x3F) | 0x80;
        } else break;
        pTempUTF16++;
    }
}

void getUTF8Long(UTF8 *buf, unsigned long namepy) {
    UTF16 buf16[128] = {0};
    vm_readv(namepy, buf16, 256);
    UTF16 *pTempUTF16 = buf16;
    UTF8 *pTempUTF8 = buf;
    UTF8 *pUTF8End = pTempUTF8 + 256;
    while (pTempUTF16 < buf16 + 128) {
        if (*pTempUTF16 == 0) break;
        if (*pTempUTF16 <= 0x007F && pTempUTF8 + 1 < pUTF8End) {
            *pTempUTF8++ = (UTF8)*pTempUTF16;
        } else if (*pTempUTF16 >= 0x0080 && *pTempUTF16 <= 0x07FF && pTempUTF8 + 2 < pUTF8End) {
            *pTempUTF8++ = (*pTempUTF16 >> 6) | 0xC0;
            *pTempUTF8++ = (*pTempUTF16 & 0x3F) | 0x80;
        } else if (*pTempUTF16 >= 0x0800 && *pTempUTF16 <= 0xFFFF && pTempUTF8 + 3 < pUTF8End) {
            *pTempUTF8++ = (*pTempUTF16 >> 12) | 0xE0;
            *pTempUTF8++ = ((*pTempUTF16 >> 6) & 0x3F) | 0x80;
            *pTempUTF8++ = (*pTempUTF16 & 0x3F) | 0x80;
        } else break;
        pTempUTF16++;
    }
}

#endif