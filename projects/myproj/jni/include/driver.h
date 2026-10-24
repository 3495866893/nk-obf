#ifndef UNIFIED_DRIVER_H
#define UNIFIED_DRIVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <regex.h>
#include <sys/uio.h>
#include <iostream>
#include <string>
#include <sys/utsname.h>
#include <sys/system_properties.h>
#include <sys/prctl.h>
#include <optional>

// ====== 防御模块 ======
#include "wcnm.h"
// ====== TwT 驱动 (触控/陀螺仪/硬件断点) ======
#include "TwT_driver.h"
// ====== 系统调用驱动 (零内核模块) ======
#include "SysCall.h"
#include "ArloLog.h"
using namespace std;

pid_t pid;
int 选择值 = 0;

struct mem_operation {
    pid_t target_pid;
    uint64_t addr;
    void *buffer;
    uint64_t size;
};

#define PRCTL_MEM_READ  0x63687501
#define PRCTL_MEM_WRITE 0x63687502

// ====== DITPRO_KPM / DITS_KO 驱动定义 ======
#define __NR_syscall_  18
#define __FLAGS        1UL << 0
#define __CHECKSUCCESS (1UL << 30)

enum class DriverType : int { ERR = -1, DITPRO_KPM = 0, DITS_KO = 1 };

enum class MemoryOp : uint64_t {
    INIT = 1UL << 1, READ = 1UL << 2, WRITE = 1UL << 3,
    CPU = 1UL << 4, PROT_NC = 1UL << 5,
    READLIST = 1UL << 6, READARRAY = 1UL << 7, UNINSTALL = 1UL << 9
};

enum class OtherOp : uint64_t {
    PROCESS_PID = 1UL << 10, MODULE_BASE = 1UL << 11,
    HIDE_PID = 1UL << 12, UNHIDE_PID = 1UL << 13,
    HIDE_EVENT = 1UL << 14, UNHIDE_EVENT = 1UL << 15,
    GETUSERMAPS = 1UL << 22, LINGYE = 1UL << 25,
};

constexpr uint64_t operator|(MemoryOp lhs, MemoryOp rhs) {
    return static_cast<uint64_t>(lhs) | static_cast<uint64_t>(rhs);
}
constexpr uint64_t operator|(uint64_t lhs, MemoryOp rhs) {
    return lhs | static_cast<uint64_t>(rhs);
}
constexpr uint64_t operator|(MemoryOp lhs, uint64_t rhs) {
    return static_cast<uint64_t>(lhs) | rhs;
}
constexpr uint64_t operator|(OtherOp lhs, OtherOp rhs) {
    return static_cast<uint64_t>(lhs) | static_cast<uint64_t>(rhs);
}
constexpr uint64_t operator|(uint64_t lhs, OtherOp rhs) {
    return static_cast<uint64_t>(lhs) | static_cast<uint64_t>(rhs);
}

struct Dit_uct_base {
    int pid;
    const char *name;
    unsigned long start;
    unsigned long end;
};

struct Dit_uct {
    uint64_t addr;
    void *buffer;
    uint64_t size;
} __attribute__((aligned(8)));

class c_driver {
private:
    int fd;
    // ====== TwT 驱动成员 ======
    c_twt_driver* twt_drv = nullptr;
    pid_t pid;
    // ====== DITPRO_KPM / DITS_KO 私有成员 ======
    DriverType new_type = DriverType::ERR;
    int new_fd = -1;

    DriverType find_dis() {
        if (auto check = __CHECKSUCCESS; syscall(__NR_syscall_, &check) == 616) {
            int flags = 616;
            new_fd = syscall(__NR_syscall_, &flags);
            return (new_fd > 0) ? DriverType::DITS_KO : DriverType::ERR;
        } else if (syscall(__NR_syscall_, (__FLAGS | __CHECKSUCCESS)) == 616) {
            return DriverType::DITPRO_KPM;
        }
        return DriverType::ERR;
    }

    template<typename... Args>
    long new_call(Args &&...args) {
        switch (new_type) {
            case DriverType::DITPRO_KPM:
                return syscall(__NR_syscall_, std::forward<decltype(args)>(args)...);
            case DriverType::DITS_KO:
                return ioctl(new_fd, std::forward<decltype(args)>(args)...);
            default:
                return -1;
        }
    }

    typedef struct _COPY_MEMORY {
        pid_t pid;
        uintptr_t addr;
        void* buffer;
        size_t size;
    } COPY_MEMORY, *PCOPY_MEMORY;

    typedef struct _MODULE_BASE {
        pid_t pid;
        char* name;
        uintptr_t base;
    } MODULE_BASE, *PMODULE_BASE;

    struct process {
        pid_t process_pid;
        char process_comm[15];
    };

    enum OPERATIONS {
        HOOK_OP_READ_MEM = 601,
        HOOK_OP_WRITE_MEM = 602,
        HOOK_OP_MODULE_BASE = 603,
        HOOK_OP_HIDE_PROCESS = 605,
        DEV_OP_INIT_KEY = 0x800,
        DEV_OP_READ_MEM = 0x801,
        DEV_OP_WRITE_MEM = 0x802,
        DEV_OP_MODULE_BASE = 0x803,
        DEV_OP_HIDE_PROCESS = 0x804,
    };

    char *gtqwq() {
        const char *dev_path = "/dev";
        DIR *dir = opendir(dev_path);
        if (dir == NULL){
            printf("无法打开/dev目录\n");
            return NULL;
        }

        struct dirent *entry;
        char file_path[256];
        while ((entry = readdir(dir)) != NULL) {
            if (strstr(entry->d_name,"std") != NULL || strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strstr(entry->d_name, "gpiochip") != NULL) {
                continue;
            }
            
            if(strchr(entry->d_name, '_') != NULL && strchr(entry->d_name, '-') != NULL && strchr(entry->d_name, ':') != NULL) {
                continue;
            }
            
            sprintf(file_path, "%s/%s", dev_path, entry->d_name);
            
            struct stat file_info;
            if (stat(file_path, &file_info) < 0)
                continue;
                
            if((localtime(&file_info.st_ctime)->tm_year + 1900) <= 1980)
                continue;
            
            if(strlen(entry->d_name) > 7 || strlen(entry->d_name) < 5)
                continue;
            
            if(file_info.st_gid != 0 || file_info.st_uid != 0)
                continue;

            if (S_ISCHR(file_info.st_mode) || S_ISBLK(file_info.st_mode)){					
                if(file_info.st_gid == 0 && file_info.st_uid == 0){
                    printf("%s\n",file_path);
                    char *devpath = (char *)malloc(32);
                    strcpy(devpath,file_path);
                    closedir(dir);
                    return devpath;
                }
            }
        }
        closedir(dir);
        return NULL;
    }

    char *execCom(const char *shell) {
        FILE *fp = popen(shell, "r");
        if (fp == NULL) {
            perror("popen failed");
            return NULL;
        }
        char buffer[256];
        char *result = (char *)malloc(1000);
        result[0] = '\0';
        while (fgets(buffer, sizeof(buffer), fp) != NULL) {
            strcat(result, buffer);
        }
        pclose(fp);
        return result;
    }

    int findFirstMatchingPath(const char *path, regex_t *regex, char *result) {
        DIR *dir;
        struct dirent *entry;

        if ((dir = opendir(path)) != NULL) {
            while ((entry = readdir(dir)) != NULL) {
                char fullpath[1024];
                snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);
                if (entry->d_type == DT_LNK) {
                    char linkpath[1024];
                    ssize_t len = readlink(fullpath, linkpath, sizeof(linkpath) - 1);
                    if (len != -1) {
                        linkpath[len] = '\0';
                        if (regexec(regex, linkpath, 0, NULL, 0) == 0) {
                            strcpy(result, fullpath);
                            closedir(dir);
                            return 1;
                        }
                    } else {
                        perror("readlink");
                    }
                }
            }
            closedir(dir);
        } else {
            perror("Unable to open directory");
        }
        return 0;
    }

    void createDriverNode(char *path, int major_number, int minor_number) {
        std::string command = "mknod " + std::string(path) + " c " + std::to_string(major_number) + " " + std::to_string(minor_number);
        system(command.c_str());
        printf("\n");
    }

    void removeDeviceNode(char *path) {
        if (unlink(path) == 0) {
        } else {
        }
    }

public:
     c_driver() {
    // ============================================================
    // 隐藏防御日志：将 stdout 和 stderr 重定向到 /dev/null
    // ============================================================
    int stdout_backup = dup(STDOUT_FILENO);
    int stderr_backup = dup(STDERR_FILENO);
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);

    // ------------------------------------------------------------
    // 第1步：环境防御检测（所有 printf/cout 将被丢弃）
    // ------------------------------------------------------------
    check_ld_preload();
    disableCoreDump();
    detect_and_exit_if_debug();

    if (check_injected_modules()) {
        // 若检测到注入，程序退出（提示被重定向，无法显示，但程序已退出）
        LOG_EXIT("检测到注入模块"); exit(EXIT_FAILURE);
    }

    if (VPNChecker::isVPNActive()) {
        LOG_EXIT("检测到VPN"); exit(EXIT_FAILURE);
    }

    if (CheckIptablesRedirection()) {
        LOG_EXIT("检测到iptables重定向"); exit(EXIT_FAILURE);
    }

    if (checkIptablesRules() != 0) {
        clearIptables();
        if (checkIptablesRules() != 0) {
            LOG_EXIT("iptables规则清除失败"); exit(EXIT_FAILURE);
        }
    }

    killOccupiedTcpPorts();
    killOccupiedIpv6TcpPorts();
    killAllListenPorts();

    if (checkNetworkConnection() != 0) {
        LOG_EXIT("网络连接异常"); exit(EXIT_FAILURE);
    }

    // ====== 新增防御批量检测 (①~⑩) ======
    arlo_log_init();
    LOG_INFO("SYS", "防御检测开始"); defense_new_all(); LOG_INFO("SYS", "防御检测通过");

    // ------------------------------------------------------------
    // 恢复标准输出/错误，后续正常显示
    // ------------------------------------------------------------
    fflush(stdout);
    fflush(stderr);
    dup2(stdout_backup, STDOUT_FILENO);
    dup2(stderr_backup, STDERR_FILENO);
    close(stdout_backup);
    close(stderr_backup);

    // ============================================================
    // 清除屏幕并显示法律声明（此时输出恢复正常）
    // ===========================================================
        printf("\033[2J\033[H");
        // ====== 法律声明 ======
        printf("\033[31;1m法律声明：本软件仅供学习研究，严禁违法违规使用。使用即视为同意免责条款，违规后果自负。\033[0m\n");
    LOG_INFO("SYS", "显示法律声明");
        printf("输入 y 继续，其他任意键退出: ");
        char ch;
        scanf(" %c", &ch);
        if (ch != 'y' && ch != 'Y') {
            printf("已退出\n");
            LOG_EXIT("用户拒绝法律声明"); exit(0);
        }
        // ======================

        printf("\033[1;36m[+] 1. RT Hook ProMax\033[0m\n");
        printf("\033[1;36m[+] 2. DEV(适配大部分)\033[0m\n");
        printf("\033[1;36m[+] 3. DITPRO_KPM\033[0m\n");
        printf("\033[1;36m[+] 4. DITS_KO\033[0m\n");
        printf("\033[1;36m[-] 5. QX10~11.4\033[0m\n");
        printf("\033[1;36m[+] 6. TwT\033[0m\n");
        printf("\033[1;36m[+] 7. SysCall\033[0m\n");
        
        // 检查 scanf 是否成功读取一个整数
        if (scanf("%d", &选择值) != 1) {
            printf("\033[31;1m输入无效，程序退出。\033[0m\n");
            LOG_EXIT("输入无效"); exit(0);
        }
        // ====== 炫酷加载条 ======
        printf("\033[?25l");
        const char* spinner[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
        const char* bar_colors[] = {"\033[31m", "\033[33m", "\033[32m", "\033[36m", "\033[35m"};
        printf("\033[1;36m╔══════════════════════════════════════╗\033[0m\n");
        printf("\033[1;36m║\033[0m        \033[1;33m🚀 正在初始化驱动...\033[0m        \033[1;36m║\033[0m\n");
        printf("\033[1;36m╚══════════════════════════════════════╝\033[0m\n");
        for (int i = 0; i <= 20; i++) {
            printf("\r\033[K  \033[1;36m[\033[0m");
            int color_idx = (i / 5) % 5;
            printf("%s", bar_colors[color_idx]);
            for (int j = 0; j < 20; j++) {
                if (j < i) printf("█");
                else if (j == i) printf("%s", spinner[i % 10]);
                else printf("░");
            }
            printf("\033[0m\033[1;36m]\033[0m \033[1;33m%3d%%\033[0m", i * 5);
            fflush(stdout);
            usleep(60000);
        }
        printf("\r\033[K  \033[1;32m[████████████████████]\033[0m \033[1;32m100%% ✅\033[0m\n");
        printf("\033[?25h");
        printf("\n");
        // ==========================
        
        if (选择值 == 1) { LOG_DRIVER_SELECT(1, "RT Hook ProMax");
            printf("\033[41m\033[37mRT Hook ProMax链接🔗成功\033[0m\n");    
            fd = 0;
        } else if (选择值 == 2) { LOG_DRIVER_SELECT(2, "DEV");        
            char *device_name = gtqwq();
            fd = open(device_name, O_RDWR);
            if (fd == -1) {
                printf("\033[41m\033[37mDEV链接🔗错误❌\033[0m\n");
                free(device_name);
                LOG_EXIT("DEV驱动打开失败"); exit(0);
            }
            free(device_name);
        } else if (选择值 == 5) { LOG_DRIVER_SELECT(5, "QX10~11.4");
            char *output = execCom("ls -l /proc/*/exe 2>/dev/null | grep -E \"/data/[a-z]{6} \\(deleted\\)\"");
            char filePath[256];
            char pid[56];
            if (output != NULL) {
                char *procStart = strstr(output, "/proc/");
                char *pidStart = procStart + 6;
                char *pidEnd = strchr(pidStart, '/');
                strncpy(pid, pidStart, pidEnd - pidStart);
                pid[pidEnd - pidStart] = '\0';

                char *arrowStart = strstr(output, "->");
                char *start = arrowStart + 3;
                char *end = strchr(output, '(') - 1;
                strncpy(filePath, start, end - start + 1);
                filePath[end - start] = '\0';

                char *replacePtr = strstr(filePath, "data");
                if (replacePtr != NULL) {
                    memmove(replacePtr + 2, replacePtr + 3, strlen(replacePtr + 3) + 1);
                    memmove(replacePtr, "dev", strlen("dev"));
                }
            } else {
                printf("Error executing scripts.\n");
            }

            char fdPath[256];
            char pattern[100];
            snprintf(pattern, sizeof(pattern), ".*%s.*", filePath + 5);
            int major_number = 0;
            int minor_number = 0;
            snprintf(fdPath, sizeof(fdPath), "/proc/%s/fd", pid);
            regex_t regex;
            if (regcomp(&regex, pattern, 0) != 0) {
                fprintf(stderr, "Failed to compile regex\n");
            }

            char result[1024];
            if (findFirstMatchingPath(fdPath, &regex, result)) {
                char cmd[256];
                sprintf(cmd, "ls -AL -l  %s | grep -Eo '[0-9]{3},' | grep  -Eo '[0-9]{3}'", result);
                char *fdInfo = execCom(cmd);
                fdInfo[strlen(fdInfo)-1] = '\0';
                major_number = atoi(fdInfo);
                free(fdInfo);
            } else {
                printf("No matching path found.\n");
            }
            regfree(&regex);

            if (filePath[0] != '\0') {
                createDriverNode(filePath, major_number, 0);
                fd = open(filePath, O_RDWR);
                if (fd == -1) {
                    removeDeviceNode(filePath);
                } else {
                    removeDeviceNode(filePath);
                }
            }
        } else if (选择值 == 6) {
            LOG_DRIVER_SELECT(6, "TwT");
            twt_drv = new c_twt_driver();
            if (!twt_drv->is_ready()) {
                printf("\033[41m\033[37mTwT驱动对接失败❌\033[0m\n");
                LOG_EXIT("TwT驱动对接失败"); exit(0);
            }
            printf("\033[41m\033[37mTwT驱动链接🔗成功\033[0m\n");
            fd = 0;  // TwT走自己的fd
            LOG_INFO("DRIVER", "TwT初始化完成");
        } else if (选择值 == 7) {
            LOG_DRIVER_SELECT(7, "SysCall");
            sys_init();
            printf("\033[42m\033[37mSysCall驱动就绪 (零内核模块)\033[0m\n");
            fd = 0;
            LOG_INFO("DRIVER", "SysCall初始化完成");
        } else if (选择值 == 3) { LOG_DRIVER_SELECT(3, "DITPRO_KPM");
            new_type = find_dis();
            if (new_type == DriverType::DITPRO_KPM) {
                printf("\033[41m\033[37mDITPRO_KPM链接🔗成功\033[0m\n");
                fd = 0;
            } else {
                printf("\033[41m\033[37mDITPRO_KPM未找到驱动❌\033[0m\n");
                LOG_EXIT("DITPRO_KPM未找到驱动"); exit(0);
            }
        } else if (选择值 == 4) { LOG_DRIVER_SELECT(4, "DITS_KO");
            new_type = find_dis();
            if (new_type == DriverType::DITS_KO) {
                printf("\033[41m\033[37mDITS_KO链接🔗成功\033[0m\n");
                fd = new_fd;
            } else {
                printf("\033[41m\033[37mDITS_KO未找到驱动❌\033[0m\n");
                LOG_EXIT("DITS_KO未找到驱动"); exit(0);
            }
        } else {
            printf("\033[31;1m无效数字，程序退出。\033[0m\n");
            LOG_EXIT("无效数字"); exit(0);
        }
    }

     ~c_driver() {
        arlo_log_close();
        if (选择值 == 3 || 选择值 == 4) {
            new_call((__FLAGS | MemoryOp::UNINSTALL));
            new_call((__FLAGS | OtherOp::UNHIDE_PID));
            if (new_fd > 0) close(new_fd);
        }
    if (选择值 == 6 && twt_drv) { delete twt_drv; twt_drv = nullptr; }
    if (选择值 == 7) { sys_close(); }
        if (fd > 0)
            close(fd);
    }

     void initialize(pid_t pid) {
        // ====== TwT 路径 ======
        if (选择值 == 6 && twt_drv) { twt_drv->initialize(pid); return; }
        if (选择值 == 7) { sys_set_pid(pid); return; }
        this->pid = pid;
    }

     bool init_key(char* key) {
        if (选择值 == 2 || 选择值 == 5) {
            char buf[0x100];
            strcpy(buf,key);
            if (ioctl(fd, DEV_OP_INIT_KEY, buf) != 0) {
                return false;
            }
            return true;
        }
        return false;
    }

     bool read(uintptr_t addr, void *buffer, size_t size) {
        // ====== TwT 路径 ======
        if (选择值 == 6 && twt_drv) return twt_drv->read(addr, buffer, size);
        if (选择值 == 7) return sys_read(addr, buffer, size);
        // ====== DITPRO_KPM / DITS_KO 路径 ======
        if (选择值 == 3 || 选择值 == 4) {
            struct Dit_uct cm = { addr, buffer, size };
            uint64_t flags = (__FLAGS | MemoryOp::READ);
            return new_call(flags, &cm) == 0;
        }

        COPY_MEMORY cm;
        cm.pid = this->pid;
        cm.addr = addr;
        cm.buffer = buffer;
        cm.size = size;

        int opcode;
        if (选择值 == 1) {
            opcode = HOOK_OP_READ_MEM;
        } else if (选择值 == 2 || 选择值 == 5) {
            opcode = DEV_OP_READ_MEM;
        } else {
            return false;
        }

        if (ioctl(fd, opcode, &cm) != 0) {
            return false;
        }
        return true;
    }

     bool write(uintptr_t addr, void *buffer, size_t size) {
        // ====== TwT 路径 ======
        if (选择值 == 6 && twt_drv) return twt_drv->write(addr, buffer, size);
        if (选择值 == 7) return sys_write(addr, buffer, size);
        // ====== DITPRO_KPM / DITS_KO 路径 ======
        if (选择值 == 3 || 选择值 == 4) {
            struct Dit_uct cm = { addr, buffer, size };
            uint64_t flags = (__FLAGS | MemoryOp::WRITE);
            return new_call(flags, &cm) == 0;
        }

        COPY_MEMORY cm;
        cm.pid = this->pid;
        cm.addr = addr;
        cm.buffer = buffer;
        cm.size = size;

        int opcode;
        if (选择值 == 1) {
            opcode = HOOK_OP_WRITE_MEM;
        } else if (选择值 == 2 || 选择值 == 5) {
            opcode = DEV_OP_WRITE_MEM;
        } else {
            return false;
        }

        if (ioctl(fd, opcode, &cm) != 0) {
            return false;
        }
        return true;
    }

    template <typename T>
    T read(uintptr_t addr) {
        T res{};
        if (this->read(addr, &res, sizeof(T))) {
            return res;
        }
        return res;
    }

    template <typename T>
    bool write(uintptr_t addr, T value) {
        return this->write(addr, &value, sizeof(T));
    }

     uintptr_t getModuleBase(char* name) {
        // ====== TwT 路径 ======
        if (选择值 == 6 && twt_drv) return twt_drv->get_module_base(name);
        if (选择值 == 7) return sys_module_base(name);
        // ====== DITPRO_KPM / DITS_KO 路径 ======
        if (选择值 == 3 || 选择值 == 4) {
            struct Dit_uct_base cm = { this->pid, name, 0, 0 };
            if (new_call((__FLAGS | OtherOp::MODULE_BASE), &cm) == 0) {
                return cm.start;
            }
            return 0;
        }

        if (选择值 == 1) {
            FILE* fp;
            char cmd[0x100] = "";
            uintptr_t ret = 0;
            
            snprintf(cmd, sizeof(cmd), "ls -l /proc/%d/map_files/ | grep '%s'", pid, name);
            fp = popen(cmd, "r");
            if (!fp) {
                std::cerr << "Failed to run command" << std::endl;
                return 0;
            }
            fscanf(fp,"%*s %*d %*s %*s %*d %*s %*s %lx-%*lx", &ret);
            pclose(fp);
            return ret;
        } else if (选择值 == 2 || 选择值 == 5) {
            MODULE_BASE mb;
            char buf[0x100];
            strcpy(buf, name);
            mb.pid = this->pid;
            mb.name = buf;

            if (ioctl(fd, DEV_OP_MODULE_BASE, &mb) != 0) {
                return 0;
            }
            return mb.base;
        }
        return 0;
    }

    void hide_process() {
        // ====== TwT 路径 ======
        if (选择值 == 6 && twt_drv) { return; }
        if (选择值 == 7) { return; }
        if (选择值 == 3 || 选择值 == 4) {
            new_call((__FLAGS | OtherOp::HIDE_PID), gettid());
            return;
        }
        if (选择值 == 1) {
            ioctl(fd, HOOK_OP_HIDE_PROCESS);
        } else if (选择值 == 2 || 选择值 == 5) {
            ioctl(fd, DEV_OP_HIDE_PROCESS);
        }
    }
};

static c_driver *driver = new c_driver();
#endif
