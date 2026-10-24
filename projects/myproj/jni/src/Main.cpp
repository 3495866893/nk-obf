#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>
#include <fstream>
#include <string.h>
#include <time.h>
#include <malloc.h>
#include <iostream>
#include <fstream>
#include<iostream>
#include<ctime>
using namespace std;
#include "Draw.h"
#include "VulkanUtils.h"
#include <iostream>
#include <chrono>
#include <ctime>
#include <string>
#define MAX_LENGTH 1024
#include <iostream>
#include <cstring>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <thread>
#include "隐藏功能.h"
#include <chrono>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <poll.h>
#include "draw.cpp"
#include <cstdlib>
#include <unistd.h>
#include <string>

// ====== 添加云验证头文件 ======
#include "res/pz.h"
#include "popup_window.cpp"

// ====== 定义云验证所需的全局变量（密钥） ======
static char *APPID = (char *)"11054";
static const char *APPKEY = "y4sH2w11imYIx1K4";
static const char *RC4KEY = "lGbBBeeFgdo11054";
static const bool RC4_ON = true;
static const char *LOCAL_VERSION = "版本";

bool main_thread_flag = true;
bool 过录屏 = false;
int Physx = 0;

 void config()
{
    NumIos[13] = 2;
    NumIo[1] = 300.0f;//雷达X位置
    NumIo[2] = 400.0f;//雷达Y位置
    NumIo[3] = 150.0f;//自瞄范围
    NumIo[4] = 1.4f;//压枪力度
    NumIo[5] = 670.0f;//触摸Y位置
    NumIo[6] = 1400.0f;//触摸X位置
    NumIo[7] = 300.0f;//触摸范围
    NumIo[8] = 1.0f;//自瞄部位
    NumIo[9] = 9.0f;//平滑力度
    NumIo[10] = 0.0f;//充电口位置
    NumIo[11] = 600.0f;//子弹速度
    NumIo[12] = 60;//帧率
    NumIo[14] = 800;//
    NumIo[15] = 200;//
    NumIo[16] = 15.0f;//自瞄速度
    NumIo[17] = 150.0f;//雷达大小
    NumIo[18] = 0.0f;//瞄准方式
    NumIo[30] = 300.0f;//自瞄速度
    NumIo[32] = 1.0f;//骨骼粗细
	NumIo[34] = 0.0f;
	NumIo[36] = 1.4f;
}

 void NumIoSave(const char *name) // 保存配置
{
    if (numSave == nullptr)
    {
        string SaveFile = "/data";
        SaveFile += "/";
        SaveFile += name;
        numSave = fopen(SaveFile.c_str(), "wb+");
    }
    fseek(numSave, 0, SEEK_SET);
    fwrite(NumIo, sizeof(float) * 200, 1, numSave);
    fwrite(DrawIo, sizeof(bool) * 200, 1, numSave);
    fwrite(NumIos, sizeof(float) * 200, 1, numSave);
    fwrite(intIo, sizeof(int) * 200, 1, numSave);
    fflush(numSave);
    fsync(fileno(numSave));
}

 void NumIoLoad(const char *name)//加载配置
{
    if (numSave == nullptr)
    {
        string SaveFile = "/data";
        SaveFile += "/";
        SaveFile += name;
        numSave = fopen(SaveFile.c_str(), "rb+");
    }
    if (numSave != nullptr)
    {
        fseek(numSave, 0, SEEK_SET);
        fread(NumIo, sizeof(float) * 200, 1, numSave);
        fread(DrawIo, sizeof(bool) * 200, 1, numSave);
        fread(NumIos, sizeof(float) * 200, 1, numSave);
        fread(intIo, sizeof(int) * 200, 1, numSave);
    }
    else
    {
        config();
    }
}

 int main(int argc, char *argv[])
{
    // ====== 建卡密目录（云验证 / 悬浮窗验证共用） ======
    system("mkdir -p /data/local/tmp/.arlo_log");
    // ====== 云验证（必须在最前） ======
    int wyz_ret = wyz();
    g_need_verify = (wyz_ret != 0);
    // ====== 失败时不退出，由悬浮窗接管验证 ======
    // ==================================

    system("rm -rf /data/physx_cache/");
    char cmd[256];
    sprintf(cmd, "pgrep -f \"Arlo.sh\" | grep -v %d | xargs kill -9", getpid());
    system(cmd);
    DrawInt();
    FuckMi8();
    vk_start();
    LoadWeaponConfig();
    printf("\033[1;36m[+] 1. 启动完整PhysX模型\033[0m\n");
    printf("\033[1;36m[+] 2. 启动残缺PhysX模型(不支持动态物体)\033[0m\n");
    printf("\033[1;36m[+] 3. 关闭PhysX模型\033[0m\n");
    scanf("%d", &Physx); 
    if (Physx == 1) {
                try {
                    if (LineTrace::initPhysX()) {
                        try {
                            // 现在启动线程，主循环已经稳定运行
                            thread (&VisibleCheck::UpdateSceneByRange).detach();
                            thread (&VisibleCheck::UpdateDynamicHeightField).detach();
                            thread (&VisibleCheck::UpdateDynamicRigid).detach();
                            printf("\033[1;36mPhysX模型初始化成功\033[0m\n");
                        } catch (...) {
                            printf("\033[1;31mPhysX线程创建失败\033[0m\n");
                        }
                    }
                } catch (...) {
                    printf("\033[1;31mPhysX初始化失败\033[0m\n");
                }
    }else if (Physx == 2) {
                try {
                    if (LineTrace::initPhysX()) {
                        try {
                            // 现在启动线程，主循环已经稳定运行
                            thread (&VisibleCheck::UpdateSceneByRange).detach();
                            thread (&VisibleCheck::UpdateDynamicHeightField).detach();
                            printf("\033[1;36mPhysX模型初始化成功\033[0m\n");
                        } catch (...) {
                            printf("\033[1;31mPhysX线程创建失败\033[0m\n");
                        }
                    }
                } catch (...) {
                    printf("\033[1;31mPhysX初始化失败\033[0m\n");
                }
    }

    screen_config(); 
    ::abs_ScreenX = (displayInfo.height > displayInfo.width ? displayInfo.height : displayInfo.width);
    ::abs_ScreenY = (displayInfo.height < displayInfo.width ? displayInfo.height : displayInfo.width);
    ::native_window_screen_x = ::abs_ScreenX;
    ::native_window_screen_y = ::abs_ScreenY;
    // 初始化imgui
    if (!initGUI_draw(native_window_screen_x, native_window_screen_y, false)) {
        return -1;
    }        
    NumIoLoad("PUBG");
    timer FPS限制;    
    FPS限制.AotuFPS_init(); 
    FPS限制.setAffinity();
    
    Touch_Init(displayInfo.width, displayInfo.height, displayInfo.orientation,false);
    new std::thread(AimBotAuto,ImGui::GetForegroundDrawList());
    LoadTextureFromMemory((const void *)&a枫埜, sizeof(a枫埜), &七八); 
    while (main_thread_flag)
    {           
        // ====== 卡密超时检测 ======
        static int timeout_check_frame = 0;
        if (++timeout_check_frame >= 60) {
            timeout_check_frame = 0;
            if (g_vip_time > 0 && time(NULL) > g_vip_time) {
                printf("\033[31;1m[卡密] 已到期！悬浮窗退出中...\033[0m\n");
                main_thread_flag = false;
                break;
            }
        }
        drawBegin(); 
        if (BallSwitch)
            win1();
        else
        {
            // 隐藏时不绘制任何内容（菜单/弹窗/透视全部跳过）
        }
        if (BallSwitch) ShowExpirePopup();
        if (过录屏)
        {
        android::ANativeWindowCreator::ProcessMirrorDisplay();
        }
        if (BallSwitch) DrawPlayer(ImGui::GetForegroundDrawList());
        drawEnd();// imgui画图结束
        FPS = NumIo[12];
        FPS限制.SetFps(FPS);
        FPS限制.AotuFPS();
    }
    Touch_Close();
    shutdown();
    return 1;
}
#include "Ui.h"