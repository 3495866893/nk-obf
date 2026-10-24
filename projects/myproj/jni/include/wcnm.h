/* 
 * 由繁华and币子 频道@bizinb开源
 * 注入时间: 2026-07-01 14:58:33
 * 联系方式: 交流群：1036218239
 */

//内置了curl的方案
/*
// 回调函数，用于捕获响应
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* userp) 
{
    size_t totalSize = size * nmemb;
    userp->append((char*)contents, totalSize);
    return totalSize;
}

// 判断是否能访问百度，来判断是否有网络
static int checkNetworkConnection()  {
    CURL* curl;
    CURLcode res;
    string readBuffer;

    // 初始化 cURL
    curl = curl_easy_init();
    if (!curl) {
        cerr << "cURL 初始化失败" << endl;
        return -1;  // 返回错误值，表示无法初始化cURL
    }

    // 设置要访问的 URL（百度）
    curl_easy_setopt(curl, CURLOPT_URL, "http://www.baidu.com");

    // 设置写回调函数和数据接收容器
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

    // 执行请求
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        cerr << "curl_easy_perform() 失败: " << curl_easy_strerror(res) << endl;
        curl_easy_cleanup(curl);
        return -1;  // 返回错误值，表示网络连接失败
    }

    // 如果到这里，说明访问百度成功，网络是可用的
    cout << "网络连接正常，能够访问百度！" << endl;

    // 清理 cURL
    curl_easy_cleanup(curl);

    return 0;  // 网络可用，返回 0
}
*/

/*
// 检查是否有网络连接，且能访问百度等网站
static bool checkNetworkConnection() {
    const char* host = "www.baidu.com";  // 用百度进行连接测试
    struct hostent* he = gethostbyname(host);

    if (he == NULL) {
        std::cerr << "无法解析主机名，可能没有网络连接！" << std::endl;
        return false;
    }

    std::cout << "网络连接正常，能够访问: " << host << std::endl;
    return true;
}
*/

// wcnm.h
#pragma once

#ifndef WCNM_H
#define WCNM_H

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <thread>
#include <chrono>
#include <netdb.h>
#include <cstdlib>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <iostream>
#include <malloc.h>
#include <pthread.h>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <vector>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <limits.h>
#include <csignal>
#include <cstdlib>
#include <sys/ptrace.h>
#include <sys/resource.h>

#include <curl/curl.h>


#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))

#define TEST_PORT 18080  // 用于测试的端口
#define MAX_RETRIES 5  // 最大重试次数
#define WAIT_TIME 1  // 每次重试等待时间（秒）

// 强行结束占用127.0.0.1和0.0.0.0所有端口的进程
static void killOccupiedTcpPorts() 
{
    //std::cout << "检查并杀死占用 127.0.0.1 和 0.0.0.0 所有端口的进程..." << std::endl;

    // 使用 netstat 查找占用 127.0.0.1 和 0.0.0.0 上的所有 LISTEN 状态的端口
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "netstat -tuln | grep -E '127.0.0.1|0.0.0.0' | grep 'LISTEN'");  // 查找所有在127.0.0.1和0.0.0.0上的 LISTEN 状态端口
    FILE* fp = popen(cmd, "r");
    if (fp) {
        char result[128];
        while (fgets(result, sizeof(result), fp)) {
            // 输出当前占用的端口信息
            //std::cout << "tcp占用信息: " << result << std::endl;

            // 提取占用端口的进程PID
            snprintf(cmd, sizeof(cmd), "netstat -tulnp | grep -E '127.0.0.1|0.0.0.0' | grep 'LISTEN' | awk '{print $7}'");
            FILE* pid_fp = popen(cmd, "r");
            if (pid_fp) {
                char pid[128];
                while (fgets(pid, sizeof(pid), pid_fp)) {
                    // 去除换行符并提取进程PID
                    pid[strcspn(pid, "\n")] = '\0';  // 去除换行符
                    char* proc_pid = strtok(pid, "/");  // 提取纯数字PID
                    if (proc_pid && strlen(proc_pid) > 0 && strcmp(proc_pid, "-") != 0) {
                        // 输出占用的进程信息
                        //std::cout << "找到tcp占用端口的进程 ID: " << proc_pid << std::endl;

                        // 获取进程所在目录（假设Unix环境）
                        snprintf(cmd, sizeof(cmd), "ls -l /proc/%s/exe", proc_pid);
                        FILE* exe_fp = popen(cmd, "r");
                        if (exe_fp) {
                            char exe_path[256];
                            if (fgets(exe_path, sizeof(exe_path), exe_fp)) {
                                //std::cout << "tcp进程所在目录: " << exe_path << std::endl;
                            }
                            pclose(exe_fp);
                        }

                        // 强制终止占用端口的进程
                        std::string kill_cmd = "kill -9 " + std::string(proc_pid);
                        int result = system(kill_cmd.c_str());
                        if (result == 0) {
                            //std::cout << "tcp进程 " << proc_pid << " 被强制结束。" << std::endl;
                        } else {
                           // std::cerr << "无法终止tcp进程 " << proc_pid << "，错误代码：" << result << std::endl;
                        }
                    }
                }
                pclose(pid_fp);
            } else {
                //std::cerr << "无法获取tcp占用端口的进程 PID！" << std::endl;
            }
        }
        pclose(fp);
    } else {
        //std::cerr << "无法获取tcp占用端口的进程信息！" << std::endl;
    }
}

// 强行结束占用 ::1（IPv6 本地回环地址）端口的进程
static void killOccupiedIpv6TcpPorts() 
{
    //std::cout << "检查并杀死占用 ::1 （IPv6 回环地址） 端口的进程..." << std::endl;

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "netstat -tuln | grep '::1' | grep 'LISTEN'");  // 查找所有在::1上的 LISTEN 状态端口
    FILE* fp = popen(cmd, "r");
    if (fp) {
        char result[128];
        while (fgets(result, sizeof(result), fp)) {
            // 输出当前占用的端口信息
            //std::cout << "ipv6占用信息: " << result << std::endl;

            // 提取占用端口的进程PID
            snprintf(cmd, sizeof(cmd), "netstat -tulnp | grep '::1' | grep 'LISTEN' | awk '{print $7}'");
            FILE* pid_fp = popen(cmd, "r");
            if (pid_fp) {
                char pid[128];
                while (fgets(pid, sizeof(pid), pid_fp)) {
                    pid[strcspn(pid, "\n")] = '\0';  // 去除换行符
                    char* proc_pid = strtok(pid, "/");  // 提取纯数字PID
                    if (proc_pid && strlen(proc_pid) > 0 && strcmp(proc_pid, "-") != 0) {
                        // 强制终止占用端口的进程
                        std::string kill_cmd = "kill -9 " + std::string(proc_pid);
                        int result = system(kill_cmd.c_str());
                        if (result == 0) {
                           // std::cout << "ipv6进程 " << proc_pid << " 被强制结束。" << std::endl;
                        } else {
                           // std::cerr << "无法终止ipv6进程 " << proc_pid << "，错误代码：" << result << std::endl;
                        }
                    }
                }
                pclose(pid_fp);
            }
        }
        pclose(fp);
    } else {
       // std::cerr << "无法获取ipv6占用端口的进程信息！" << std::endl;
    }
}

// 强行结束所有 LISTEN 状态的端口的进程
static void killAllListenPorts() 
{
    //std::cout << "检查并杀死所有处于 LISTEN 状态的端口的进程..." << std::endl;

    // 使用 netstat 查找所有处于 LISTEN 状态的 TCP 端口
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "netstat -tuln | grep 'LISTEN'");  // 查找所有 LISTEN 状态的端口
    FILE* fp = popen(cmd, "r");
    if (fp) {
        char result[128];
        while (fgets(result, sizeof(result), fp)) {
            // 输出当前占用的端口信息
           // std::cout << "占用信息: " << result << std::endl;

            // 提取占用端口的进程PID
            snprintf(cmd, sizeof(cmd), "netstat -tulnp | grep 'LISTEN' | awk '{print $7}'");
            FILE* pid_fp = popen(cmd, "r");
            if (pid_fp) {
                char pid[128];
                while (fgets(pid, sizeof(pid), pid_fp)) {
                    // 去除换行符并提取进程PID
                    pid[strcspn(pid, "\n")] = '\0';  // 去除换行符
                    char* proc_pid = strtok(pid, "/");  // 提取纯数字PID
                    if (proc_pid && strlen(proc_pid) > 0 && strcmp(proc_pid, "-") != 0) {
                        // 输出占用的进程信息
                        //std::cout << "找到占用端口的进程 ID: " << proc_pid << std::endl;

                        // 获取进程所在目录（假设Unix环境）
                        snprintf(cmd, sizeof(cmd), "ls -l /proc/%s/exe", proc_pid);
                        FILE* exe_fp = popen(cmd, "r");
                        if (exe_fp) {
                            char exe_path[256];
                            if (fgets(exe_path, sizeof(exe_path), exe_fp)) {
                                //std::cout << "进程所在目录: " << exe_path << std::endl;
                            }
                            pclose(exe_fp);
                        }

                        // 强制终止占用端口的进程
                        std::string kill_cmd = "kill -9 " + std::string(proc_pid);
                        int result = system(kill_cmd.c_str());
                        if (result == 0) {
                            //std::cout << "进程 " << proc_pid << " 被强制结束。" << std::endl;
                        } else {
                           // std::cerr << "无法终止进程 " << proc_pid << "，错误代码：" << result << std::endl;
                        }
                    }
                }
                pclose(pid_fp);
            } else {
                //std::cerr << "无法获取占用端口的进程 PID！" << std::endl;
            }
        }
        pclose(fp);
    } else {
       // std::cerr << "无法获取占用端口的进程信息！" << std::endl;
    }
}

// 回调函数，用于捕获响应
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* userp)
{
    size_t totalSize = size * nmemb;
    userp->append((char*)contents, totalSize);
    return totalSize;
}

// 判断是否能访问百度，来判断是否有网络
static int checkNetworkConnection() 
{
    CURL* curl;
    CURLcode res;
    string readBuffer;

    // 初始化 cURL
    curl = curl_easy_init();
    if (!curl) {
        cerr << "cURL 初始化失败" << endl;
        return -1;  // 返回错误值，表示无法初始化cURL
    }

    // 设置要访问的 URL（百度）
    curl_easy_setopt(curl, CURLOPT_URL, "http://www.baidu.com");

    // 设置写回调函数和数据接收容器
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

    // 执行请求
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        cerr << "curl_easy_perform() 失败: " << curl_easy_strerror(res) << endl;
        curl_easy_cleanup(curl);
        return -1;  // 返回错误值，表示网络连接失败
    }

    // 如果到这里，说明访问百度成功，网络是可用的
    //cout << "网络连接正常，能够访问百度！" << endl;

    // 清理 cURL
    curl_easy_cleanup(curl);

    return 0;  // 网络可用，返回 0
}

static void setup_inotify(const char* path) 
{
    int inotify_fd = inotify_init();
    if (inotify_fd == -1) {
        perror("inotify_init");
        exit(EXIT_FAILURE);
    }

    int watch_descriptor = inotify_add_watch(inotify_fd, path, IN_OPEN | IN_ACCESS);
    if (watch_descriptor == -1) {
        perror("inotify_add_watch");
        exit(EXIT_FAILURE);
    }
    close(inotify_fd);
}

//死妈ida附加
static bool is_ida_debugger() 
{
    const char* ida_check_file = "/proc/self/status";
    const char* ida_signature = "IDA";

    int status_fd = open(ida_check_file, O_RDONLY);
    if (status_fd == -1) {
        perror("open");
        return false;
    }

    char buffer[1024];
    ssize_t bytes_read = read(status_fd, buffer, sizeof(buffer));
    if (bytes_read == -1) {
        perror("read");
        close(status_fd);
        return false;
    }

    close(status_fd);

    std::string status_info(buffer, bytes_read);
    return status_info.find(ida_signature) != std::string::npos;
}

//ida
static void detect_and_exit_if_debug() 
{
    // 检测IDA调试环境
    if (is_ida_debugger()) {
        std::cerr << "IDA和你的妈，总有一个会先死" << std::endl;
        exit(EXIT_FAILURE);
    }
    // 设置inotify监控maps和mem
    setup_inotify("/proc/self/maps");
    setup_inotify("/proc/self/mem");

}

// 你dump上了老弟？
static void disableCoreDump() 
{
    // 首先，通过 prctl 禁用进程的可转储性，阻止内核生成该进程的核心转储文件
    if (prctl(PR_SET_DUMPABLE, 0) == -1) {
        std::cerr << "无法禁用进程的可转储性。" << std::endl;
        return; 
    }

    struct rlimit rl;
    rl.rlim_cur = 0;  // 设置当前限制为0，表示不允许生成核心转储
    rl.rlim_max = 0;  // 设置最大限制为0，进一步确保不生成核心转储

    // 调用 setrlimit 来应用上述限制
    if (setrlimit(RLIMIT_CORE, &rl) == -1) {
        std::cerr << "无法禁用核心转储。" << std::endl;  // 如果失败，输出错误信息
    }
}

//注入
static bool check_injected_modules() 
{
    // 直接检查当前进程的 TracerPid
    const char* proc_file = "/proc/self/status";
    std::ifstream status_file(proc_file);
    if (!status_file) {
        perror("打开状态文件失败");
        return false;
    }

    std::string line;
    while (std::getline(status_file, line)) {
        if (line.find("TracerPid") != std::string::npos) {
            // 检查 TracerPid 的值
            if (line.find("0") == std::string::npos) {
                std::cerr << "你妈什么时候死啊？" << std::endl;
                return true; // 发现可疑注入
            }
        }
    }
    return false; // 没有发现注入
}

// 该函数用于检查iptables是否存在任何规则
static int checkIptablesRules() 
{
    FILE *fp;
    char path[1035];
    
    // 检查 nat 表的规则
    fp = popen("iptables -t nat -L -n", "r");
    if (fp == NULL) {
        perror("Failed to run iptables command");
        return 1;
    }

    // 检查 nat 表的默认策略
    while (fgets(path, sizeof(path)-1, fp) != NULL) {
        // 检查是否是 nat 表默认策略，如果是就跳过
        if (strstr(path, "Chain PREROUTING") != NULL || 
            strstr(path, "Chain POSTROUTING") != NULL ||
            strstr(path, "Chain OUTPUT") != NULL) {
            // 如果找到默认链策略行，检查是否为 ACCEPT（默认策略）
            if (strstr(path, "policy ACCEPT") != NULL) {
                //printf("nat表: 默认策略是 ACCEPT, 跳过检查\n");
                fclose(fp);
                return 0;  // 如果默认策略为 ACCEPT，则跳过后续检查
            }
        }

        // 如果是非默认规则，进行进一步检查
        if (strstr(path, "DNAT") != NULL || strstr(path, "ACCEPT") != NULL) {
            //printf("nat表规则: %s", path);  // 输出nat表规则
            fclose(fp);
            return 1;  // 如果nat表中有规则，返回1
        }
    }
    fclose(fp);
    
    // 检查 filter 表的规则
    fp = popen("iptables -t filter -L -n", "r");
    if (fp == NULL) {
        perror("Failed to run iptables command");
        return 1;
    }

    // 检查 filter 表的默认策略
    while (fgets(path, sizeof(path)-1, fp) != NULL) {
        if (strstr(path, "Chain INPUT") != NULL || 
            strstr(path, "Chain OUTPUT") != NULL || 
            strstr(path, "Chain FORWARD") != NULL) {
            // 如果找到默认链策略行，检查是否为 ACCEPT（默认策略）
            if (strstr(path, "policy ACCEPT") != NULL) {
                //printf("filter表: 默认策略是 ACCEPT, 跳过检查\n");
                fclose(fp);
                return 0;  // 如果默认策略为 ACCEPT，则跳过后续检查
            }
        }

        // 如果是非默认规则，进行进一步检查
        if (strstr(path, "ACCEPT") != NULL || strstr(path, "DROP") != NULL) {
            //printf("filter表规则: %s", path);  // 输出filter表规则
            fclose(fp);
            return 1;  // 如果filter表中有规则，返回1
        }
    }
    fclose(fp);

    // 检查 mangle 表的规则
    fp = popen("iptables -t mangle -L -n", "r");
    if (fp == NULL) {
        perror("Failed to run iptables command");
        return 1;
    }

    // 检查 mangle 表的默认策略
    while (fgets(path, sizeof(path)-1, fp) != NULL) {
        if (strstr(path, "Chain PREROUTING") != NULL || 
            strstr(path, "Chain POSTROUTING") != NULL || 
            strstr(path, "Chain INPUT") != NULL ||
            strstr(path, "Chain OUTPUT") != NULL || 
            strstr(path, "Chain FORWARD") != NULL) {
            // 如果找到默认链策略行，检查是否为 ACCEPT（默认策略）
            if (strstr(path, "policy ACCEPT") != NULL) {
                //printf("mangle表: 默认策略是 ACCEPT, 跳过检查\n");
                fclose(fp);
                return 0;  // 如果默认策略为 ACCEPT，则跳过后续检查
            }
        }

        // 如果是非默认规则，进行进一步检查
        if (strstr(path, "MARK") != NULL || strstr(path, "ACCEPT") != NULL) {
            //printf("mangle表规则: %s", path);  // 输出mangle表规则
            fclose(fp);
            return 1;  // 如果mangle表中有规则，返回1
        }
    }
    fclose(fp);

    // 检查 raw 表的规则
    fp = popen("iptables -t raw -L -n", "r");
    if (fp == NULL) {
        perror("Failed to run iptables command");
        return 1;
    }

    // 检查 raw 表的默认策略
    while (fgets(path, sizeof(path)-1, fp) != NULL) {
        if (strstr(path, "Chain PREROUTING") != NULL || 
            strstr(path, "Chain OUTPUT") != NULL) {
            // 如果找到默认链策略行，检查是否为 ACCEPT（默认策略）
            if (strstr(path, "policy ACCEPT") != NULL) {
                //printf("raw表: 默认策略是 ACCEPT, 跳过检查\n");
                fclose(fp);
                return 0;  // 如果默认策略为 ACCEPT，则跳过后续检查
            }
        }

        // 如果是非默认规则，进行进一步检查
        if (strstr(path, "ACCEPT") != NULL) {
            //printf("raw表规则: %s", path);  // 输出raw表规则
            fclose(fp);
            return 1;  // 如果raw表中有规则，返回1
        }
    }
    fclose(fp);

    // 检查 security 表的规则
    fp = popen("iptables -t security -L -n", "r");
    if (fp == NULL) {
        perror("Failed to run iptables command");
        return 1;
    }

    // 检查 security 表的默认策略
    while (fgets(path, sizeof(path)-1, fp) != NULL) {
        if (strstr(path, "Chain INPUT") != NULL || 
            strstr(path, "Chain OUTPUT") != NULL) {
            // 如果找到默认链策略行，检查是否为 ACCEPT（默认策略）
            if (strstr(path, "policy ACCEPT") != NULL) {
                //printf("security表: 默认策略是 ACCEPT, 跳过检查\n");
                fclose(fp);
                return 0;  // 如果默认策略为 ACCEPT，则跳过后续检查
            }
        }

        // 如果是非默认规则，进行进一步检查
        if (strstr(path, "ACCEPT") != NULL) {
            //printf("security表规则: %s", path);  // 输出security表规则
            fclose(fp);
            return 1;  // 如果security表中有规则，返回1
        }
    }
    fclose(fp);
    
    return 0;  // 如果所有表中都没有规则，返回0
}

//iptables清理
 void clearIptables()
{
    // 清空所有iptables规则，清空所有表
    system("iptables -Z");  // 重置所有规则的计数器，即清除包和字节计数（不影响规则本身）
    system("iptables -F");                  // 清空所有链的规则
    system("iptables -X");                  // 删除所有用户自定义链
    system("iptables -Z");  // 再次重置所有规则的计数器
    system("iptables -t nat -F");           // 清空 nat 表中所有链的规则
    system("iptables -t mangle -F");        // 清空 mangle 表中所有链的规则
    system("iptables -t raw -F");           // 清空 raw 表中所有链的规则
    system("iptables -t security -F");      // 清空 security 表中所有链的规则
    system("iptables -t nat -F OUTPUT");    // 清空 OUTPUT 链中的所有规则
    //system("iptables -t nat -D OUTPUT -d 127.0.0.1 -p tcp -j DNAT"); // 删除127的DNAT规则
    // 强制刷新iptables状态
    system("iptables-save > /dev/null");
    //printf("已刷新状态")
}

static int versionone = 5; // 程序版本号
static int version() 
{
    CURL* curl;
    CURLcode res;
    string readBuffer;

    // 初始化 cURL
    curl = curl_easy_init();
    if (!curl) {
        cerr << "cURL 初始化失败" << endl;
        return -1;  // 返回错误值
    }

    // 设置要访问的 URL
    curl_easy_setopt(curl, CURLOPT_URL, "https://sharechain.qq.com/0501bdf0a8eaf331449ec8b5b8884d30");

    // 设置写回调函数和数据接收容器
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

    // 执行请求
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        cerr << "curl_easy_perform() 失败: " << curl_easy_strerror(res) << endl;
        curl_easy_cleanup(curl);
        return -1;  // 返回错误值
    }

    // 查找包含 "[版本号]" 的部分
    size_t versionPos = readBuffer.find("[版本号]");
    if (versionPos == string::npos) {
        cerr << "未找到版本号。" << endl;
        curl_easy_cleanup(curl);
        return -1;  // 返回错误值
    }

    // 提取版本号的内容
    size_t versionStart = versionPos + strlen("[版本号]");
    size_t versionEnd = readBuffer.find("[版本号]", versionStart);
    if (versionEnd == string::npos) {
        cerr << "未找到结束的版本号标记。" << endl;
        curl_easy_cleanup(curl);
        return -1;  // 返回错误值
    }

    // 获取版本号字符串
    string versionStr = readBuffer.substr(versionStart, versionEnd - versionStart);

    // 转换为整数
    int convertedVersion = atoi(versionStr.c_str());

    // 清理 cURL
    curl_easy_cleanup(curl);

    return convertedVersion;
}

static void check_ld_preload() 
{
   // const char *vars[] = {"LD_PRELOAD", "LD_LIBRARY_PATH", "LD_AUDIT"};
    const char *vars[] = {"LD_PRELOAD"};
    for (char **env = environ; *env != NULL; ++env) {
        for (size_t i = 0; i < sizeof(vars) / sizeof(vars[0]); ++i) {
            if (strncmp(*env, vars[i], strlen(vars[i])) == 0) {
                //printf("发现环境变量 %s 被注入，程序退出。\n", vars[i]);
                puts("你能看见这句话，只是我想让你看见，记住了废物东西，你这辈子都绕不了你爹的验证");
                puts("一天玩个封包玩魔怔了是吗？");
               //system("reboot");
                exit(EXIT_FAILURE);
            }
        }
    }
}

static void check_cmdline() 
{
    DIR* dir = opendir("/proc/");
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir))) {
            if (entry->d_type == DT_DIR) {
                char cmdline_path[256];
                snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%s/cmdline", entry->d_name);
                FILE* file = fopen(cmdline_path, "r");
                if (file) {
                    char cmdline[1024];
                    fread(cmdline, 1, sizeof(cmdline) - 1, file);
                    fclose(file);
                    // 检查是否包含指定的进程名
                    if (strstr(cmdline, "com.guoshi.httpcanary") ||
                        strstr(cmdline, "com.network.proxy") ||
                        strstr(cmdline, "com.reqable.android") ||
                        strstr(cmdline, "com.evbadroid.wicap") ||
                        strstr(cmdline, "com.guoshi.httpcanary.premium") ||
                        strstr(cmdline, "com.httpcanary.pro") ||
                        strstr(cmdline, "cn.iyya.vvv") ||
                        strstr(cmdline, "cn.iyya.vv") ||
                        strstr(cmdline, "com.ikooc.mm") ||
                        strstr(cmdline, "com.Pro")) {
                        // 检测到不需要的进程，重启设备
                        std::cout << "检测到不需要的进程，正在重启设备..." << std::endl;
                        //system("reboot");  // 执行重启命令
                        exit(0);  // 重启后退出程序
                    }
                }
            }
        }
        closedir(dir);
    }
}

class VPNChecker {
public:
    static bool isVPNActive() 
    {
        std::ifstream ifconfig("/proc/net/dev");
        std::string line;

        while (std::getline(ifconfig, line)) {
            if (line.find("tun0") != std::string::npos || 
                line.find("tun1") != std::string::npos || 
                line.find("tun2") != std::string::npos || 
                line.find("ppppp0") != std::string::npos || 
                line.find("ppp0") != std::string::npos) {
                return true; // VPN detected
            }
        }

        return false; // VPN not detected
    }

    static void stopApplication(const std::string& appName) {
        std::cout << "Stopping application: " << appName << std::endl;
    }
};

static bool CheckIptablesRedirection() 
{
  // 执行 `iptables -t nat -L PREROUTING` 命令并读取输出
  const char *command = "iptables -t nat -L PREROUTING";
  FILE *pipe = popen(command, "r");
  if (!pipe) {
    std::cerr << "Failed to run iptables command." << std::endl;
    return false;
  }

  char buffer[128];
  bool redirectionDetected = false;

  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    // 检查是否有重定向规则的迹象
    if (strstr(buffer, "REDIRECT") != nullptr) {
      redirectionDetected = true;
      break;
    }
  }

  pclose(pipe);
  return redirectionDetected;
}

// ============================================================
// ====== 新增防御模块 (①~⑩) ======
// ============================================================

// ① 反 Frida — 扫描 maps 中的 frida/gadget 特征 + 检测默认端口 27042
static bool check_frida() 
{
    // 1. 扫描 /proc/self/maps 中的 frida 特征字符串
    std::ifstream maps("/proc/self/maps");
    if (maps.is_open()) {
        std::string line;
        while (std::getline(maps, line)) {
            if (line.find("frida") != std::string::npos ||
                line.find("gadget") != std::string::npos ||
                line.find("gum-js-loop") != std::string::npos ||
                line.find("linjector") != std::string::npos) {
                return true;
            }
        }
        maps.close();
    }
    // 2. 检测 frida 默认端口 27042
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock >= 0) {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(27042);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            close(sock);
            return true;
        }
        close(sock);
    }
    return false;
}

// ② 反 GDB/LLDB — 检测 gdbserver/lldb-server 进程
static bool check_gdb_lldb() 
{
    DIR* dir = opendir("/proc/");
    if (!dir) return false;
    struct dirent* entry;
    while ((entry = readdir(dir))) {
        if (entry->d_type == DT_DIR) {
            char cmdline_path[256];
            snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%s/cmdline", entry->d_name);
            FILE* file = fopen(cmdline_path, "r");
            if (file) {
                char cmdline[512] = {0};
                fread(cmdline, 1, sizeof(cmdline) - 1, file);
                fclose(file);
                if (strstr(cmdline, "gdbserver") || strstr(cmdline, "gdb") ||
                    strstr(cmdline, "lldb-server") || strstr(cmdline, "lldb")) {
                    closedir(dir);
                    return true;
                }
            }
        }
    }
    closedir(dir);
    return false;
}

// ③ 反 strace — 扫描进程列表
static bool check_strace() 
{
    DIR* dir = opendir("/proc/");
    if (!dir) return false;
    struct dirent* entry;
    while ((entry = readdir(dir))) {
        if (entry->d_type == DT_DIR) {
            char cmdline_path[256];
            snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%s/cmdline", entry->d_name);
            FILE* file = fopen(cmdline_path, "r");
            if (file) {
                char cmdline[512] = {0};
                fread(cmdline, 1, sizeof(cmdline) - 1, file);
                fclose(file);
                if (strstr(cmdline, "strace")) {
                    closedir(dir);
                    return true;
                }
            }
        }
    }
    closedir(dir);
    return false;
}

// ④ 反 GameGuardian — 扫描进程名 + maps 中的 gg 特征
static bool check_gameguardian() 
{
    // 1. 扫描进程列表
    DIR* dir = opendir("/proc/");
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir))) {
            if (entry->d_type == DT_DIR) {
                char cmdline_path[256];
                snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%s/cmdline", entry->d_name);
                FILE* file = fopen(cmdline_path, "r");
                if (file) {
                    char cmdline[512] = {0};
                    fread(cmdline, 1, sizeof(cmdline) - 1, file);
                    fclose(file);
                    if (strstr(cmdline, "gameguardian") || strstr(cmdline, "catch_.me_.if_.you_.can_")) {
                        closedir(dir);
                        return true;
                    }
                }
            }
        }
        closedir(dir);
    }
    // 2. 扫描自身 maps 中的 gg 相关映射
    std::ifstream maps("/proc/self/maps");
    if (maps.is_open()) {
        std::string line;
        while (std::getline(maps, line)) {
            if (line.find("/gg") != std::string::npos ||
                line.find("gameguardian") != std::string::npos) {
                return true;
            }
        }
        maps.close();
    }
    return false;
}

// ⑤ 反 Xposed/LSPosed — 检测 maps 中的 xposed/lsposed 模块和 CLASSPATH 环境变量
static bool check_xposed() 
{
    // 1. 扫描 maps 中的 xposed/lsposed 特征
    std::ifstream maps("/proc/self/maps");
    if (maps.is_open()) {
        std::string line;
        while (std::getline(maps, line)) {
            if (line.find("xposed") != std::string::npos ||
                line.find("Xposed") != std::string::npos ||
                line.find("lsposed") != std::string::npos ||
                line.find("LSPosed") != std::string::npos ||
                line.find("edxposed") != std::string::npos ||
                line.find("riru") != std::string::npos) {
                return true;
            }
        }
        maps.close();
    }
    // 2. 检查 CLASSPATH 环境变量中是否有 xposed 的 jar
    const char* classpath = getenv("CLASSPATH");
    if (classpath) {
        if (strstr(classpath, "xposed") || strstr(classpath, "Xposed") ||
            strstr(classpath, "lsposed") || strstr(classpath, "edxposed")) {
            return true;
        }
    }
    return false;
}

// ⑧ Self-ptrace 防附加 — 占住 ptrace 不让其他调试器附加

// ⑨ 反内存修改工具 — 检测 Cheat Engine / 烧饼 / 八门神器 等进程

// ⑩ 代码段完整性校验 — 对自身 .text 段做 CRC32 检测 inline hook
static uint32_t crc32_table[256];
static bool crc32_init = false;
static void crc32_init_table() {
    if (crc32_init) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
        crc32_table[i] = crc;
    }
    crc32_init = true;
}
static uint32_t crc32_calc(const uint8_t* data, size_t len) {
    crc32_init_table();
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

static bool check_text_integrity() 
{
    // 读取自身 maps 找到可执行段
    std::ifstream maps("/proc/self/maps");
    if (!maps.is_open()) return false;
    std::string line;
    while (std::getline(maps, line)) {
        // 查找 r-xp (可执行) 且是自身二进制文件的行
        if (line.find("r-xp") != std::string::npos &&
            line.find("/data") != std::string::npos &&
            line.find(".sh") != std::string::npos) {
            // 解析地址范围
            uintptr_t start = 0, end = 0;
            sscanf(line.c_str(), "%lx-%lx", &start, &end);
            if (start > 0 && end > start) {
                size_t size = end - start;
                // 取前 4KB 和后 4KB 做采样校验
                uint8_t buf[4096];
                uint32_t calc_crc = 0;
                // 读头部
                int mem_fd = open("/proc/self/mem", O_RDONLY);
                if (mem_fd >= 0) {
                    lseek(mem_fd, start, SEEK_SET);
                    ssize_t n = read(mem_fd, buf, size < 4096 ? size : 4096);
                    if (n > 0) calc_crc = crc32_calc(buf, n);
                    close(mem_fd);
                }
                // 这里只做采样校验，可在正常运行时预先计算好 CRC 存下来对比
                // 如果 calc_crc 与预期不符，说明 .text 被篡改
                (void)calc_crc;
                break;
            }
        }
    }
    maps.close();
    return false; // 默认不做强制退出，预留接口
}

// ⑪ 反 dumpsys 包名泄露 — 伪装进程名为系统进程
static void hide_process_name() 
{
    // 伪装成系统进程，防止 dumpsys / ps 暴露包名
    const char* fake_names[] = {
        "system_server", "surfaceflinger", "servicemanager",
        "netd", "logd", "lmkd", "installd"
    };
    srand(time(NULL) ^ getpid());
    int idx = rand() % (sizeof(fake_names) / sizeof(fake_names[0]));
    prctl(PR_SET_NAME, fake_names[idx], 0, 0, 0);
}

// ============================================================
// 🔍 防御触发日志 — 记录触发原因，方便排查 + 对外展示
// ============================================================
#define DEFENSE_LOG_PATH "/data/local/tmp/defense_triggered.log"

static void log_defense_trigger(const char* name) 
{
    FILE* fp = fopen(DEFENSE_LOG_PATH, "w");
    if (fp) {
        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        char timebuf[64];
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm_info);
        fprintf(fp, "time=%s\n", timebuf);
        fprintf(fp, "defense=%s\n", name);
        fprintf(fp, "pid=%d\n", getpid());
        fclose(fp);
    }
}

static void show_defense_log() 
{
    FILE* fp = fopen(DEFENSE_LOG_PATH, "r");
    if (fp) {
        char line[256];
        char defense_name[128] = "未知";
        char time_str[64] = "未知时间";
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "defense=", 8) == 0) {
                strncpy(defense_name, line + 8, sizeof(defense_name) - 1);
                defense_name[strcspn(defense_name, "\r\n")] = 0;
            }
            if (strncmp(line, "time=", 5) == 0) {
                strncpy(time_str, line + 5, sizeof(time_str) - 1);
                time_str[strcspn(time_str, "\r\n")] = 0;
            }
        }
        fclose(fp);
        printf("\033[31;1m⚠ 上次退出原因：触发防御 [%s]（%s）\033[0m\n", defense_name, time_str);
        remove(DEFENSE_LOG_PATH); // 读过就删，下次重新记录
    }
}

// ====== ⑧ self-ptrace 防附加 ======
static void self_ptrace() 
{
    long ret = ptrace(PTRACE_TRACEME, 0, NULL, NULL);
    if (ret == -1) {
        log_defense_trigger("⑧self-ptrace(已被追踪)");
        return;
    }
}
// ============================================================
// 批量检测入口 — 带日志记录
// ============================================================
static void defense_new_all()
{
    detect_and_exit_if_debug();    // 反IDA
    disableCoreDump();             // 反dump
    if (check_frida())             { log_defense_trigger("①反Frida"); exit(EXIT_FAILURE); }
    if (check_gdb_lldb())          { log_defense_trigger("②反GDB/LLDB"); exit(EXIT_FAILURE); }
    if (check_strace())            { log_defense_trigger("③反strace"); exit(EXIT_FAILURE); }
    if (check_gameguardian())      { log_defense_trigger("④反GameGuardian"); exit(EXIT_FAILURE); }
    if (check_xposed())            { log_defense_trigger("⑤反Xposed/LSPosed"); exit(EXIT_FAILURE); }
    check_text_integrity();        // 预留 CRC 校验
    hide_process_name();           // 伪装进程名
}

#endif  // WCNM_H


/*
使用：
int main() 
{
    check_ld_preload();  //检测动态库注入

    // 1. 检查网络连接
    if (checkNetworkConnection() == 0) {
        //cout << "网络连接测试通过！" << endl;
    } else {
        cerr << "网络连接测试失败！" << endl;
        std::cout << "没网络你用你妈了个b，杂种东西" << std::endl;
        system("reboot");
    }


      // 2. 强行结束占用 127.0.0.1 和 0.0.0.0 所有端口的进程
    killOccupiedTcpPorts();

    // 3. 强行结束占用 ::1（IPv6 回环地址）端口的进程
    killOccupiedIpv6TcpPorts();

    // 4. 强行结束所有 LISTEN 状态的端口的进程
    killAllListenPorts();
    
    int versionResult = version();
	if (versionResult == versionone)   // 判断程序版本和目标版本是否一致
	{

	 //是否存在注入线程
    if (check_injected_modules()) {
        //std::cerr << "注入和你的妈，总有一个会先死" << std::endl;
		system("reboot");
        exit(EXIT_FAILURE);
    }

	disableCoreDump();      // 禁用dump小子
    
	detect_and_exit_if_debug();     // 执行检测并在异常情况下退出程序

    // 检查 iptables 重定向，如果存在就退出，解锁机可能会有问题，因为内置了vpn只是不显示而已
    if (CheckIptablesRedirection()) {
        std::cerr << "Iptables之你死去的亲妈" << std::endl;
        return 1; // 退出程序
    }

	// 检查是否有 VPN
	if (VPNChecker::isVPNActive())
	{
		std::cout << "检测到VPN，你妈死了？" << std::endl;
		exit(1);
	}
	std::cout << "网络环境正常" << std::endl;

    //  检查所有表中的规则是否已清空
    if (checkIptablesRules() != 0) {
        //printf("你给我滚你妈的，杂种东西\n");
        system("reboot");
        return 1;  // 程序退出
    }

    //  清除所有iptables规则
    clearIptables();

    sleep(2);

    // 验证参数是否被篡改
    if (verifyParams()) {
        //std::cout << "所有参数均未被篡改。" << std::endl;
    } else {
        //std::cerr << "检测到参数被篡改！" << std::endl;
        system("reboot");
    }
	
	int yes = -1;
	char ErrStr[256] = {0};
	all_WIG *local_wig_verify = get_wigData();
	snprintf(local_wig_verify->APPID, sizeof(local_wig_verify->APPID), "%d",28375);
	snprintf(local_wig_verify->RC4KEY, sizeof(local_wig_verify->RC4KEY), "%s", "Bmad1c00Nu4cfmG");
	snprintf(local_wig_verify->NEWCODE, sizeof(local_wig_verify->NEWCODE), "%s", "f0351c9962c64de343b33607873fd8b0");

    // 1. 检查网络连接
    if (checkNetworkConnection() == 0) {
        //cout << "网络连接测试通过！" << endl;
    } else {
        cerr << "网络连接测试失败！" << endl;
        std::cout << "没网络你用你妈了个b，杂种东西" << std::endl;
        system("reboot");
    }

    // 2. 强行结束占用 127.0.0.1 和 0.0.0.0 所有端口的进程
    killOccupiedTcpPorts();

    // 3. 强行结束占用 ::1（IPv6 回环地址）端口的进程
    killOccupiedIpv6TcpPorts();

    // 4. 强行结束所有 LISTEN 状态的端口的进程
    killAllListenPorts();

    // 5.检查所有表中的规则是否已清空
    if (checkIptablesRules() != 0) {
        //printf("你给我滚你妈的，杂种东西\n");
        system("reboot");
        return 1;  // 程序退出
    }

    // 版本号验证
    Version_information app_version_info;
    get_app_version(local_wig_verify, &app_version_info);
    printf("最新版本号 : %s\n", app_version_info.Latest_version);
    printf("最新版本链接 : %s\n", app_version_info.Latest_version_link);  //可以设置你自己的
    if (app_version_info.is_tforce_update) {
        printf("更新一下，更新链接：www.baidu.com\n");
        exit(1);  //如果要更新那么退出去
    } else {
        printf("不需要强制更新\n");
    }

    // 检查所有表中的规则是否已清空
    if (checkIptablesRules() != 0) {
        //printf("你给我滚你妈的，杂种东西\n");
        system("reboot");
        return 1;  // 程序退出
    }

    wigAuto(&yes, (char *)&ErrStr); //里面有路径
    char *str = get_wig_announcement(::get_wigData());
    if (yes == 8610010) { //成功
        printf("到期时间 : %s\n", ErrStr); 
        printf("公告内容 : %s\n", str);              
    } else {
        printf("卡密错误 : %s\n", ErrStr);
        printf("exit\n");   
        exit(1);     
    }   
    
    // 检查公告内容是否符合要求
    const char *expected_announcement = "wig公告内容"; 
    if (str != NULL && strcmp(str, expected_announcement) != 0) {  
    // printf("你给我滚你妈的，杂种东西\n");
        system("reboot");
        exit(EXIT_FAILURE);
    } else {
        //printf("公告通过\n");
    }


     // 验证参数是否被篡改
    if (verifyParams()) {
        //std::cout << "所有参数均未被篡改。" << std::endl;
    } else {
        //std::cerr << "检测到参数被篡改！" << std::endl;
        system("reboot");
    }
}
else{
	//system("reboot");
	std::cerr << "滚你妈的" << std::endl;
	}
}
*/