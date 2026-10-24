#include "卡密验证UI.h"
// ====== 雪花粒子（悬浮窗内） ======
struct SnowParticle {
    float x, y;
    float speed;
    float size;
    float opacity;
    float drift;
};
static SnowParticle snow[50];
static bool snow_init = false;

 static void InitSnow(const ImVec2& winPos, const ImVec2& winSize) {
    for (int i = 0; i < 50; i++) {
        snow[i].x = winPos.x + (rand() % (int)(winSize.x > 0 ? winSize.x : 400));
        snow[i].y = winPos.y + (rand() % (int)(winSize.y > 0 ? winSize.y : 600));
        snow[i].speed = 0.25f + (rand() % 100) / 100.0f * 0.55f;
        snow[i].size = 0.8f + (rand() % 100) / 100.0f * 2.0f;
        snow[i].opacity = 0.12f + (rand() % 100) / 100.0f * 0.35f;
        snow[i].drift = -0.25f + (rand() % 100) / 100.0f * 0.5f;
    }
    snow_init = true;
}

 static void UpdateSnow(ImDrawList* winDraw, const ImVec2& winPos, const ImVec2& winSize) {
    if (!snow_init) InitSnow(winPos, winSize);
    float dt = ImGui::GetIO().DeltaTime * 60.0f;
    float minX = winPos.x, maxX = winPos.x + winSize.x;
    float minY = winPos.y, maxY = winPos.y + winSize.y;
    for (int i = 0; i < 50; i++) {
        snow[i].y += snow[i].speed * dt;
        snow[i].x += snow[i].drift * dt;
        if (snow[i].y > maxY + 5) { snow[i].y = minY - 5; snow[i].x = minX + (rand() % (int)(winSize.x > 0 ? winSize.x : 400)); }
        if (snow[i].x > maxX + 10) snow[i].x = minX - 5;
        if (snow[i].x < minX - 10) snow[i].x = maxX + 5;
        ImU32 col = ImColor(1.0f, 1.0f, 1.0f, snow[i].opacity);
        winDraw->AddCircleFilled(ImVec2(snow[i].x, snow[i].y), snow[i].size, col, 6);
    }
}

static char bg_input_buf[512] = "";
static bool bg_input_init = false;

 void win1()
{
    static bool lastCollapsedState = false;
    static ImVec2 lastWinSize = ImVec2(0, 0);
    Io = &ImGui::GetIO();
    ImGuiStyle & style = ImGui::GetStyle();

    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 6.0f;
    style.ScrollbarSize = 40.0f;
    style.GrabMinSize = 35.0f;
    style.FramePadding = ImVec2(6, 4);
    style.WindowBorderSize = 1.0f;
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.Alpha = 1.0f;

    ImVec4 cyan    = ImVec4(0.00f, 0.78f, 1.00f, 1.00f);
    ImVec4 cyanDim = ImVec4(0.00f, 0.50f, 0.70f, 1.00f);
    ImVec4 darkBg  = ImVec4(0.03f, 0.05f, 0.08f, 0.94f);
    ImVec4 frameBg = ImVec4(0.06f, 0.10f, 0.16f, 1.00f);
    ImVec4 textCol = ImVec4(0.78f, 0.88f, 0.98f, 1.00f);
    ImVec4 border  = ImVec4(0.00f, 0.55f, 0.80f, 0.50f);

    style.Colors[ImGuiCol_Text] = textCol;
    style.Colors[ImGuiCol_WindowBg] = darkBg;
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.05f, 0.08f, 0.14f, 0.95f);
    style.Colors[ImGuiCol_Border] = border;
    style.Colors[ImGuiCol_FrameBg] = frameBg;
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.08f, 0.14f, 0.22f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.10f, 0.18f, 0.28f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.03f, 0.05f, 0.08f, 0.6f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.00f, 0.40f, 0.60f, 0.6f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.00f, 0.55f, 0.80f, 0.8f);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = cyan;
    style.Colors[ImGuiCol_CheckMark] = cyan;
    style.Colors[ImGuiCol_SliderGrab] = cyan;
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.30f, 0.90f, 1.00f, 1.0f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.00f, 0.40f, 0.60f, 0.4f);
    style.Colors[ImGuiCol_SeparatorHovered] = cyanDim;
    style.Colors[ImGuiCol_SeparatorActive] = cyan;
    style.Colors[ImGuiCol_Button] = ImVec4(0.00f, 0.45f, 0.70f, 0.6f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.00f, 0.60f, 0.90f, 0.8f);
    style.Colors[ImGuiCol_ButtonActive] = cyan;
    style.Colors[ImGuiCol_Header] = ImVec4(0.00f, 0.40f, 0.65f, 0.5f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 0.55f, 0.85f, 0.7f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.00f, 0.65f, 0.95f, 0.9f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.02f, 0.04f, 0.07f, 0.9f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.00f, 0.30f, 0.55f, 0.95f);
    style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.02f, 0.04f, 0.07f, 0.8f);
    style.Colors[ImGuiCol_ResizeGrip] = cyan;
    style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.30f, 0.90f, 1.00f, 1.0f);
    style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.50f, 1.00f, 1.00f, 1.0f);
    style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.00f, 0.50f, 0.80f, 0.5f);

    if (!MemuSwitch) { exit_decryption(); kill(getpid(), SIGKILL); }
    ImGui::Begin("Arlo公益模型漏打", &MemuSwitch, ImGuiItemFlags_NoNav|ImGuiWindowFlags_AlwaysVerticalScrollbar);

    // ====== [测试] Vulkan 纹理贴图验证 ======
    {
        static MyTextureData g_test_tex;
        static bool g_test_loaded = false;
        if (!g_test_loaded) {
            g_test_loaded = LoadTextureFromFile("/storage/emulated/0/遗产/内核源码/巴巴托斯UI/UI.jpg", &g_test_tex);
        }
        if (g_test_loaded) {
            ImGui::Text("Vulkan 纹理测试: %dx%d", g_test_tex.Width, g_test_tex.Height);
            ImGui::Image((ImTextureID)g_test_tex.DS, ImVec2(320, 180));
        } else {
            ImGui::Text("纹理加载失败");
        }
    }
    // ====== [测试结束] ======
    ImVec2 winPos = ImGui::GetWindowPos();
    ImVec2 winSize = ImGui::GetWindowSize();
    ImDrawList* winDraw = ImGui::GetWindowDrawList();
    // ====== 验证未通过时：自动/手动模式处理 ======
    if (g_need_verify) {
        static bool g_mode_loaded = false;
        static bool g_auto_checked = false;
        if (!g_mode_loaded) {
            g_mode_loaded = true;
            load_verify_mode();   // 读取持久化的自动/手动模式
        }
        if (g_auto_login && !g_auto_checked) {
            g_auto_checked = true;   // 只自动检查一次
            if (auto_login_if_saved()) {
                g_need_verify = false;   // 自动验证通过，进入主界面
                ImGui::End();
                return;
            }
        }
        draw_verify_ui(winDraw, winPos, winSize);
        ImGui::End();
        return;
    }
    if (winSize.x != lastWinSize.x || winSize.y != lastWinSize.y) { snow_init = false; lastWinSize = winSize; }
    UpdateSnow(winDraw, winPos, winSize);

    if (bg_loaded && bg_tex.DS) {
        winDraw->AddImage((ImTextureID)bg_tex.DS, winPos, ImVec2(winPos.x + winSize.x, winPos.y + winSize.y),
            ImVec2(0, 0), ImVec2(1, 1), ImColor(1.0f, 1.0f, 1.0f, 0.12f));
    }

    bool isCollapsed = ImGui::IsWindowCollapsed();

    {
        float titleH = ImGui::GetFontSize() + style.FramePadding.y * 2 + 4;
        float lineY = winPos.y + titleH;
        winDraw->AddLine(ImVec2(winPos.x + 8, lineY), ImVec2(winPos.x + winSize.x - 8, lineY),
            ImColor(0.0f, 0.6f, 0.9f, 0.5f), 1.5f);
        float t = fmod(ImGui::GetTime() * 0.5f, 1.0f);
        float dotX = winPos.x + 8 + (winSize.x - 16) * t;
        winDraw->AddCircleFilled(ImVec2(dotX, lineY), 3.0f, ImColor(0.0f, 0.9f, 1.0f, 0.8f), 8);
    }

    if (g_vip_time > 0) {
        time_t now = time(NULL);
        time_t remaining = g_vip_time - now;
        if (remaining > 0) {
            int days = remaining / 86400;
            int hours = (remaining % 86400) / 3600;
            int mins = (remaining % 3600) / 60;
            int secs = remaining % 60;
            char countdown[64];
            sprintf(countdown, "[!] 到期倒计时: %d天 %02d时 %02d分 %02d秒", days, hours, mins, secs);
            if (remaining < 3600)
                ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "%s", countdown);
            else if (remaining < 86400)
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "%s", countdown);
            else
                ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.4f, 1.0f), "%s", countdown);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "[!] 卡密已到期！即将退出...");
        }
        ImGui::Separator();
    }

    if (ImGui::CollapsingHeader("背景图设置")) {
        if (bg_loaded)
            ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.4f, 1.0f), "背景图已加载");
        else
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "未加载背景图");
        if (ImGui::Button("清除背景图", ImVec2(-1, 50))) {
            if (bg_loaded && bg_tex.DS) { RemoveTexture(&bg_tex); bg_loaded = false; }
        }
        ImGui::Separator();
    }

    if (isCollapsed != lastCollapsedState) {
        if (isCollapsed) ImGui::SetWindowSize(ImVec2(350, 50), ImGuiCond_Always);
        else ImGui::SetWindowSize(ImVec2(900, 800), ImGuiCond_Always);
        lastCollapsedState = isCollapsed;
    }

    ImGui::SliderFloat("FPS调整", &NumIo[12], 60, 144);
    if (ImGui::Button("无后台", ImVec2(-1, 90))) signal(SIGHUP, SIG_IGN);
    if (ImGui::Button("保存配置", ImVec2(-1, 90))) { SaveWeaponConfig(); NumIoSave("PUBG"); }
    if (ImGui::Button("初始化", ImVec2(-1, 90))) DrawInt();
    ImGui::Checkbox("过录屏", &过录屏); ImGui::SameLine();
    ImGui::Checkbox("显示PhysX", &DrawIo[86]); ImGui::SameLine();
    ImGui::Checkbox("漏打模式", &DrawIo[89]);
    ImGui::Checkbox("坐标解密", &DrawIo[90]);

    if (ImGui::CollapsingHeader("人物绘制")) {
        ImGui::Checkbox("人物射线", &DrawIo[2]); ImGui::SameLine();
        ImGui::Checkbox("人物骨骼", &DrawIo[3]); ImGui::SameLine();
        ImGui::Checkbox("人物血量", &DrawIo[6]); ImGui::SameLine();
        ImGui::Checkbox("人物手持", &DrawIo[11]);
        ImGui::Checkbox("忽略人机", &DrawIo[35]); ImGui::SameLine();
        ImGui::Checkbox("绘制距离", &DrawIo[5]); ImGui::SameLine();
        ImGui::Checkbox("绘制载具", &DrawIo[9]); ImGui::SameLine();
        ImGui::Checkbox("绘制手雷", &DrawIo[51]);
        ImGui::Checkbox("全图人数", &DrawIo[55]); ImGui::SameLine();
        ImGui::Checkbox("绘制雷达", &DrawIo[7]);
        ImGui::SliderFloat("雷达宽",&NumIo[1],0.0f,displayInfo.width,"%.1f");
        ImGui::SliderFloat("雷达高",&NumIo[2],0.0f,displayInfo.height,"%.1f");
        ImGui::SliderFloat("雷达X位置",&NumIo[53],0.0f,displayInfo.width,"%.1f");
        ImGui::SliderFloat("雷达Y位置",&NumIo[54],0.0f,displayInfo.height,"%.1f");
        ImGui::SliderFloat("雷达缩放",&NumIo[17],0.0f,500.0f,"%.1f");
    }
    if (ImGui::CollapsingHeader("[危险]触摸自瞄")) {
        ImGui::Checkbox("自瞄开关", &DrawIo[20]); ImGui::SameLine();
        ImGui::Checkbox("人机不瞄", &DrawIo[23]); ImGui::SameLine();
        ImGui::Checkbox("持续锁定", &DrawIo[25]); ImGui::SameLine();
        ImGui::Checkbox("倒地不瞄", &DrawIo[22]);
        ImGui::Checkbox("触摸位置", &DrawIo[21]);
        ImGui::RadioButton("充电口右", &NumIo[10], 0.0f); ImGui::SameLine();
        ImGui::RadioButton("充电口左", &NumIo[10], 1.0f);
        if (ImGui::Combo("自瞄部位", &intIo[6], "头部\0胸部\0臀部\0")) {
            switch (intIo[6]) {
                case 1:NumIo[8]=1.0; break;
                case 2:NumIo[8]=2.0; break;
                case 3:NumIo[8]=3.0; break;
            }
        }
        ImGui::SliderFloat("触摸范围", &NumIo[7],5.0f,1000.0f,"%.0f");
        ImGui::SliderFloat("自瞄范围", &NumIo[3],10.0f,500.0f,"%.0f");
        ImGui::SliderFloat("自瞄距离", &NumIo[30], 0.1f, 500.0f, "%.0f");
        ImGui::SliderFloat("喷子距离", &NumIo[68], 0.1f, 100.0f, "%.0f");
        ImGui::SliderFloat("腰射距离", &NumIo[98], 0.1f, 100.0f, "%.0f");
        ImGui::SliderFloat("自瞄速度", &NumIo[16], 4.5f, 17.0f, "%.1f");
        ImGui::SliderFloat("预判力度", &NumIo[36], 0.1f, 3.0f, "%.1f");
        ImGui::SliderFloat("压枪力度", &g_WeaponPressureDB[MyWeapon], 0.1f, 20.0f, "%.1f");
        ImGui::SliderFloat("蹲下压枪", &g_WeaponPressureDX[MyWeapon], 0.1f, 20.0f, "%.1f");
        ImGui::SliderFloat("趴下压枪", &g_WeaponPressurePX[MyWeapon], 0.1f, 20.0f, "%.1f");
    }
    if (ImGui::CollapsingHeader("物资绘制")) {
        ImGui::Checkbox("绘制盒子", &DrawIo[50]); ImGui::SameLine();
        ImGui::Checkbox("绘制宝箱", &DrawIo[52]); ImGui::SameLine();
        ImGui::Checkbox("盒内物资", &DrawIo[91]); ImGui::SameLine();
        ImGui::Checkbox("显示类名", &DrawIo[90]);
    }
    if (ImGui::CollapsingHeader("[!] 调试信息")) {
        ImGui::TextColored(ImVec4(0.0f, 0.9f, 1.0f, 1.0f), "── 进程信息 ──");
        ImGui::Text("PID: %d", pid);
        ImGui::Text("UE4:  0x%lX", UE4);
        ImGui::Text("Gname: 0x%lX", Gname);
        ImGui::Text("Uworld: 0x%lX", Uworld);
        ImGui::Text("Matrix: 0x%lX", Matrix);
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.0f, 0.9f, 1.0f, 1.0f), "── 游戏数据 ──");
        ImGui::Text("Arrayaddr: 0x%lX", Arrayaddr);
        ImGui::Text("实体数量:  %d", countddr);
        ImGui::Text("玩家数量:  %d", PlayerCount);
        ImGui::Text("人机数量:  %d", RobotCount);
        ImGui::Text("自身队伍:  %d", Myteam);
        ImGui::Text("自身动作:  %d", 自身动作);
        ImGui::Text("开镜: %d  开火: %d", Aiming, Firing);
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.0f, 0.9f, 1.0f, 1.0f), "── 读写检测 ──");
        static bool rw_tested = false;
        static bool rw_ok = false;
        static uint32_t rw_test_val = 0;
        if (ImGui::Button("检测读写", ImVec2(-1, 50))) {
            rw_tested = true;
            uint32_t magic = driver->read<uint32_t>(UE4);
            rw_ok = (magic == 0x464C457F);  // ELF magic: \x7FELF
            rw_test_val = magic;
        }
        if (rw_tested) {
            if (rw_ok) {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.3f, 1.0f), "✅ 读写正常 (ELF: 0x%08X)", rw_test_val);
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "❌ 读写异常! (读到: 0x%08X)", rw_test_val);
            }
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "点击按钮检测...");
        }
    }
    ImGui::End();
}
