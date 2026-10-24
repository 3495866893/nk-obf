#ifndef TIMER_H
#define TIMER_H

#include <time.h>
#include <string>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include <errno.h>
#include <thread>
#include <iostream>
#include <sys/sysinfo.h>
#include <fstream>

struct timer {
    bool fpsio;
    int events;
    void* data;
    struct timespec str, end, old, now, sleep, vsync, test, strloop, endloop;
    long long lodtime;
    long long looptime;
    long long runtime;
    std::string name;
    unsigned long Fps;
    long SleepTime;
    long vsyncSleepTime;
    unsigned long old_time;
    unsigned long now_time;
    long vsynctiemr[2];
    
    // 添加帧率稳定性相关变量
    long long lastFrameTime;
    long long frameTimeAvg;
    int frameCount;
    long long totalFrameTime;
    
    // 用户配置变量
    int userSelectedCPU;
    bool cpuAffinityConfigured;
    
    void setname(const char * name_){
        name = name_;
    }
    
    timer(char *in_name){
        name = in_name;
        SleepTime = 0;
        old_time = 0;
        lodtime = 0;
        now_time = 0;
        lastFrameTime = 0;
        frameTimeAvg = 16666667; // 初始60FPS
        frameCount = 0;
        totalFrameTime = 0;
        userSelectedCPU = -2;        // 默认-2：绑定所有CPU
        cpuAffinityConfigured = false;
        memset(&str,0, sizeof(str));
        memset(&end,0, sizeof(end));
        memset(&old,0, sizeof(old));
        memset(&now,0, sizeof(now));
        memset(&sleep,0, sizeof(sleep));
        memset(&vsync,0, sizeof(vsync));
    }
    
    timer(unsigned int fps){
        Fps = 1000000000/fps;
        SleepTime = 0;
        old_time = 0;
        lodtime = 0;
        now_time = 0;
        lastFrameTime = 0;
        frameTimeAvg = 1000000000/fps;
        frameCount = 0;
        totalFrameTime = 0;
        userSelectedCPU = -2;        // 默认-2：绑定所有CPU
        cpuAffinityConfigured = false;
        memset(&str,0, sizeof(str));
        memset(&end,0, sizeof(end));
        memset(&old,0, sizeof(old));
        memset(&now,0, sizeof(now));
        memset(&sleep,0, sizeof(sleep));
        memset(&vsync,0, sizeof(vsync));
    }
    
    timer(){
        SleepTime = 0;
        old_time = 0;
        lodtime = 0;
        now_time = 0;
        lastFrameTime = 0;
        frameTimeAvg = 16666667; // 初始60FPS
        frameCount = 0;
        totalFrameTime = 0;
        userSelectedCPU = -2;        // 默认-2：绑定所有CPU
        cpuAffinityConfigured = false;
        memset(&str,0, sizeof(str));
        memset(&end,0, sizeof(end));
        memset(&old,0, sizeof(old));
        memset(&now,0, sizeof(now));
        memset(&sleep,0, sizeof(sleep));
        memset(&vsync,0, sizeof(vsync));
    }
    
    // 获取CPU数量（多种方法尝试）
    int32_t getNumCpus()
    {
        // 方法1: 读取/proc/cpuinfo
        std::ifstream cpuinfo("/proc/cpuinfo");
        if (cpuinfo.is_open()) {
            std::string line;
            int cpu_count = 0;
            while (std::getline(cpuinfo, line)) {
                if (line.find("processor") == 0) {
                    cpu_count++;
                }
            }
            cpuinfo.close();
            if (cpu_count > 0) {
                return cpu_count;
            }
        }
        
    }
    
   
void askUserForCPUSetting() {
    if (cpuAffinityConfigured) {
        return;
    }
    std::cout << "════════════════════════════════════════════════════════" << std::endl;
    std::cout << "请选择CPU绑定方式：" << std::endl;
    std::cout << std::endl;
    std::cout << "  【0】绑定到所有CPU核心" << std::endl;
    std::cout << "  【1】绑定到大核CPU核心" << std::endl;
    std::cout << std::endl;
    std::cout << "请输入选择编号 (0/1): ";
    
    // 添加这行来读取用户输入
    int choice;
    std::cin >> choice;
    
    if (choice == 1) {
        userSelectedCPU = -1;  // 或者其他值表示选择大核
    } else {
        userSelectedCPU = -2;  // 所有核
    }
    
    cpuAffinityConfigured = true;
    std::cout << std::endl;
}
    
    //计时器开始
    inline void start(){
        clock_gettime(CLOCK_MONOTONIC,&str);
    }
    
    //计时器结束
    inline float stop(bool show){
        clock_gettime(CLOCK_MONOTONIC,&end);
        runtime = (((1000000000LL*end.tv_sec)+(end.tv_nsec))-((1000000000LL*str.tv_sec)+(str.tv_nsec)));
        if (show){
        }
        return runtime/1000000.0f;
    }
    
    // 使用 AotuFPS 之前 需调用 SetFps ，参数 单位 每秒循环次数
    inline void SetFps(unsigned int fps){
        Fps = 1000000000LL/fps;
        frameTimeAvg = Fps; // 同时更新平均帧时间
    }
    
    inline void FpsEnd(){
        fpsio = false;
    }
    
    //初始化 AotuFPS ， 必须在 AotuFPS 之前调用
    inline void AotuFPS_init()
    {
        clock_gettime(CLOCK_MONOTONIC,&old);
        SleepTime = Fps;
        frameCount = 0;
        totalFrameTime = 0;
        start();
    }
    
    long long oldtimer;
    
    // 改进的 AotuFPS - 修复帧率稳定性问题
    inline float AotuFPS()
    {
        clock_gettime(CLOCK_MONOTONIC,&now);
        
        // 精确计算上一帧耗时（纳秒）
        long long current_time_ns = now.tv_sec * 1000000000LL + now.tv_nsec;
        long long old_time_ns = old.tv_sec * 1000000000LL + old.tv_nsec;
        long long frame_time_ns = current_time_ns - old_time_ns;
        
        // 更新帧时间统计（用于稳定性）
        frameCount++;
        totalFrameTime += frame_time_ns;
        if (frameCount >= 10) { // 每10帧更新一次平均值
            frameTimeAvg = totalFrameTime / frameCount;
            frameCount = 0;
            totalFrameTime = 0;
        }
        
        // 计算需要sleep的时间
        SleepTime = Fps - frame_time_ns;
        
        // 动态调整sleep时间，考虑帧时间波动
        if (frameTimeAvg > 0) {
            float stabilityFactor = (float)Fps / frameTimeAvg;
            if (stabilityFactor < 0.8f || stabilityFactor > 1.2f) {
                // 帧率不稳定，使用更保守的sleep策略
                SleepTime = (Fps - frame_time_ns) * 0.7f;
            }
        }
        
        if (SleepTime > 0) {
            // 限制最大sleep时间，避免过度sleep
            if (SleepTime > Fps * 2) {
                SleepTime = Fps;
            }
            nsleep(SleepTime);
            
            // 更新当前时间
            clock_gettime(CLOCK_MONOTONIC,&now);
            current_time_ns = now.tv_sec * 1000000000LL + now.tv_nsec;
        } else {
            SleepTime = 0;
        }
        
        // 计算实际帧时间
        long long actual_frame_time = current_time_ns - old_time_ns;
        
        // 更新时间戳为当前帧结束时间
        old = now;
        
        // 返回毫秒
        return actual_frame_time / 1000000.0f;
    }

    //定时器单位毫秒，到达定时时间返回true，并且再次进入定时
    inline bool cktime(unsigned int ms)
    {
        if ( !lodtime ){
            start();
        }
        clock_gettime(CLOCK_MONOTONIC,&end);
        lodtime = (((1000000000LL*end.tv_sec)+(end.tv_nsec))-((1000000000LL*str.tv_sec)+(str.tv_nsec)))/1000;
        if ( lodtime >= ms ){
            lodtime = 0;
            return true;
        } else{
            return false;
        }
    }
    
    bool islooptimestart;
    inline void looptimestart()
    {
        islooptimestart = true;
        clock_gettime(CLOCK_MONOTONIC,&strloop);
    }
    
    inline void looptimeend()
    {
        islooptimestart = false;
    }
    
    inline long getlooptime()
    {
        clock_gettime(CLOCK_MONOTONIC,&endloop);
        looptime = (((1000000000LL*endloop.tv_sec)+(endloop.tv_nsec))-((1000000000LL*strloop.tv_sec)+(strloop.tv_nsec)));
        return looptime;
    }

//设置CPU亲和，平稳循环时间 , 需要在循环同线程调用。
void setAffinity()
{
    // 如果是第一次调用，询问用户配置
    if (!cpuAffinityConfigured) {
        askUserForCPUSetting();
    }
    
    // 根据用户选择设置CPU亲和性
    cpu_set_t cpuSet;
    CPU_ZERO(&cpuSet);
    
    if (userSelectedCPU == -2) {
        // -2: 绑定所有和尚（所有核）
        const int32_t numCpus = getNumCpus();
        for (int32_t cpu = 0; cpu < numCpus; ++cpu) {
            CPU_SET(cpu, &cpuSet);
        }
        std::cout << "✅所有核绑定成功！" << std::endl;
    } else {
        // 绑定到所有大核上（CPU 4-7）
        const int32_t numCpus = getNumCpus();
        for (int32_t cpu = 4; cpu < numCpus && cpu < 8; ++cpu) {
            CPU_SET(cpu, &cpuSet);
        }
        std::cout << "✅ 大核绑定成功！" << std::endl;
    }
    
    // 应用CPU亲和性设置
    sched_setaffinity(gettid(), sizeof(cpuSet), &cpuSet);
}
    
    //高精度sleep,单位纳秒
    inline void nsleep(long delay)
    {
        if (delay <= 0) return;
        
        struct timespec req, rem;
        req.tv_sec = delay / 1000000000LL;
        req.tv_nsec = delay % 1000000000LL;
        
        while (nanosleep(&req, &rem) == -1) {
            if (errno == EINTR) {
                req = rem;
            } else {
                break;
            }
        }
    }
};

#endif 