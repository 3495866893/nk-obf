// ====== 卡密验证悬浮窗界面 ======
// 验证方式：读取 /data/local/tmp/.arlo_log/.key 文件验证
// 日志：走 ArloLog.h 加密系统，带去重（相同消息只写一次）
//
// 验证模式：
//   g_auto_login=true  → 自动：启动检测 .key 文件，有就直接联网验证进主界面
//   g_auto_login=false → 手动：每次启动都在验证界面点按钮读取文件验证
// 模式持久化在 /data/local/tmp/.arlo_log/verify_mode（1=自动 0=手动）

#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>

// ================= 去重日志 =================
// 相同消息只写一次；内容变化才追加
static void add_dedup_log(const char *level, const char *mod, const char *msg)
{
    static char last_msg[256] = "";
    if (strcmp(last_msg, msg) == 0) return;  // 重复，跳过
    strncpy(last_msg, msg, sizeof(last_msg) - 1);
    ARLO_LOG(level, mod, "%s", msg);
}

#define LOG_INFO_1(mod, fmt, ...) do { char m[256]; snprintf(m, sizeof(m), fmt, ##__VA_ARGS__); add_dedup_log("INFO", mod, m); } while(0)
#define LOG_ERROR_1(mod, fmt, ...) do { char m[256]; snprintf(m, sizeof(m), fmt, ##__VA_ARGS__); add_dedup_log("ERROR", mod, m); } while(0)
#define LOG_AUTH_STEP_1(fmt, ...) LOG_INFO_1("AUTH", fmt, ##__VA_ARGS__)

// ================= 验证模式（持久化） =================
static bool g_auto_login = true;
static const char *g_mode_path = "/data/local/tmp/.arlo_log/verify_mode";

static void load_verify_mode()
{
    FILE *fp = fopen(g_mode_path, "r");
    if (fp) {
        int c = fgetc(fp);
        fclose(fp);
        g_auto_login = (c == '1');
        LOG_INFO_1("AUTH", "验证模式: %s", g_auto_login ? "自动" : "手动");
    } else {
        g_auto_login = true;
    }
}

static void save_verify_mode()
{
    mkdir("/data/local/tmp/.arlo_log", 0755);
    FILE *fp = fopen(g_mode_path, "w");
    if (fp) {
        fputc(g_auto_login ? '1' : '0', fp);
        fclose(fp);
        LOG_INFO_1("AUTH", "保存模式: %s", g_auto_login ? "自动" : "手动");
    }
}

// ================= 验证状态 =================
static bool g_verify_busy = false;
static char g_verify_msg[256] = "";
static bool g_verify_last_ok = false;

// ================= 确保 .key 文件存在 =================
static void ensure_key_file()
{
    const char *key_path = "/data/local/tmp/.arlo_log/.key";
    mkdir("/data/local/tmp/.arlo_log", 0755);
    FILE *fp = fopen(key_path, "r");
    if (fp) {
        fclose(fp);
        // 已存在，不动
    } else {
        fp = fopen(key_path, "w");
        if (fp) {
            fprintf(fp, "");
            fclose(fp);
            LOG_INFO_1("FILE", "已创建卡密文件: %s", key_path);
        } else {
            LOG_ERROR_1("FILE", "无法创建卡密文件: %s", key_path);
        }
    }
}

// ================= 读取 .key 文件 =================
static bool read_key_file(char *out_km, size_t out_size)
{
    const char *key_path = "/data/local/tmp/.arlo_log/.key";
    if (read_text_file(key_path, out_km, out_size)) {
        size_t n = strlen(out_km);
        while (n > 0 && (out_km[n-1] == '\n' || out_km[n-1] == '\r' || out_km[n-1] == ' '))
            out_km[--n] = 0;
        if (strlen(out_km) > 0) {
            LOG_INFO_1("FILE", "读取卡密文件成功 [%s]", out_km);
            return true;
        }
        out_km[0] = 0;
    }
    LOG_ERROR_1("FILE", "卡密文件不存在或为空: %s", key_path);
    return false;
}

// ================= 执行验证 =================
static void do_verify_once()
{
    g_verify_busy = true;
    char km[40] = {0};

    if (read_key_file(km, sizeof(km))) {
        char imei[40] = {0};
        char imei_luj[128];
        snprintf(imei_luj, sizeof(imei_luj), "/storage/emulated/0/Android/%s.imei", APPID);
        if (!read_text_file(imei_luj, imei, sizeof(imei))) {
            memset(imei, '0', 15);
            imei[15] = 0;
        }
        LOG_INFO_1("AUTH", "验证中 imei=[%s]", imei);
        bool ok = login_once((char *)"yz.dujiuyue.cn", APPID, APPKEY, RC4KEY, RC4_ON,
                             "/data/local/tmp/.arlo_log/.key",
                             km, sizeof(km), imei);
        g_verify_last_ok = ok;
        if (ok) {
            LOG_INFO_1("AUTH", "验证通过");
            snprintf(g_verify_msg, sizeof(g_verify_msg), "验证通过！");
            g_need_verify = false;
        } else {
            LOG_ERROR_1("AUTH", "验证失败：卡密错误或服务器无响应");
            snprintf(g_verify_msg, sizeof(g_verify_msg),
                     "验证失败：卡密错误或服务器无响应。");
        }
    } else {
        g_verify_last_ok = false;
        snprintf(g_verify_msg, sizeof(g_verify_msg),
                 "卡密文件不存在或为空。\n请把卡密写入：\n/data/local/tmp/.arlo_log/.key");
    }
    g_verify_busy = false;
}

// ================= 自动登录检查 =================
static bool auto_login_if_saved()
{
    char km[40] = {0};
    if (read_text_file("/data/local/tmp/.arlo_log/.key", km, sizeof(km))) {
        size_t n = strlen(km);
        while (n > 0 && (km[n-1] == '\n' || km[n-1] == '\r' || km[n-1] == ' '))
            km[--n] = 0;
        if (strlen(km) > 0) {
            char imei[40] = {0};
            char imei_luj[128];
            snprintf(imei_luj, sizeof(imei_luj), "/storage/emulated/0/Android/%s.imei", APPID);
            if (!read_text_file(imei_luj, imei, sizeof(imei))) {
                memset(imei, '0', 15);
                imei[15] = 0;
            }
            bool ok = login_once((char *)"yz.dujiuyue.cn", APPID, APPKEY, RC4KEY, RC4_ON,
                                 "/data/local/tmp/.arlo_log/.key",
                                 km, sizeof(km), imei);
            if (ok) {
                LOG_INFO_1("AUTO", "自动验证通过");
                g_need_verify = false;
                return true;
            }
            LOG_ERROR_1("AUTO", "自动验证失败，转验证界面");
        }
    }
    g_need_verify = true;
    return false;
}

// ================= 验证 UI =================
static void draw_verify_ui(ImDrawList *winDraw, ImVec2 winPos, ImVec2 winSize)
{
    // 确保卡密文件存在（只查一次：有就留着，没有就创建）
    static bool key_checked = false;
    if (!key_checked) {
        key_checked = true;
        ensure_key_file();
    }

    ImGui::SetNextWindowSize(ImVec2(900.0f, 700.0f), ImGuiCond_Always);

    float W = winSize.x, H = winSize.y;
    winDraw->AddRectFilled(winPos, ImVec2(winPos.x + W, winPos.y + H),
        ImColor(0.0f, 0.0f, 0.0f, 0.52f));

    float cardW = W * 0.86f, cardH = H * 0.86f;
    if (cardW < 340) cardW = 340;
    if (cardH < 260) cardH = 260;
    ImVec2 cP(winPos.x + (W - cardW) * 0.5f, winPos.y + (H - cardH) * 0.5f);
    ImVec2 cE(cP.x + cardW, cP.y + cardH);
    float pad = 28.0f;

    winDraw->AddRectFilled(cP, cE, ImColor(0.04f, 0.10f, 0.16f, 0.97f), 14.0f);
    winDraw->AddRect(cP, cE, ImColor(0.0f, 0.85f, 1.0f, 0.9f), 14.0f, 0, 2.0f);

    // ---- 内容 ----
    ImGui::SetCursorScreenPos(ImVec2(cP.x + pad, cP.y + 20));
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "[ Arlo 公益漏打 - 卡密验证 ]");

    // 验证模式
    ImGui::SetCursorScreenPos(ImVec2(cP.x + pad, cP.y + 60));
    ImGui::TextColored(ImVec4(0.80f, 0.90f, 1.00f, 1.0f), "验证模式:");
    ImGui::SameLine();
    if (ImGui::Checkbox("自动登录（启动时有卡密直接验证进主界面）", &g_auto_login)) {
        save_verify_mode();
    }
    ImGui::SameLine();
    ImGui::TextColored(g_auto_login ? ImVec4(0.4f, 1.0f, 0.6f, 1.0f) : ImVec4(1.0f, 0.8f, 0.4f, 1.0f),
                       g_auto_login ? "[自动]" : "[手动]");

    // 说明
    ImGui::SetCursorScreenPos(ImVec2(cP.x + pad, cP.y + 94));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.75f, 0.95f, 1.0f));
    ImGui::PushTextWrapPos(cE.x - pad);
    ImGui::TextWrapped(
        "把卡密写入文件：\n"
        "/data/local/tmp/.arlo_log/.key\n"
        "（目录不存在会自动创建）\n"
        "然后点击下方按钮验证。");
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();

    // 主按钮 - 读取文件验证
    ImGui::SetCursorScreenPos(ImVec2(cP.x + pad, cP.y + 220));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.45f, 0.75f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.65f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.35f, 0.55f, 1.0f));
    if (g_verify_busy) ImGui::BeginDisabled();
    if (ImGui::Button(g_verify_busy ? "验证中..." : "读取文件验证", ImVec2(cardW - pad * 2, 52.0f))) {
        do_verify_once();
    }
    if (g_verify_busy) ImGui::EndDisabled();
    ImGui::PopStyleColor(3);

    // 状态消息
    if (g_verify_msg[0]) {
        ImVec4 col = g_verify_last_ok
            ? ImVec4(0.2f, 0.95f, 0.4f, 1.0f)
            : ImVec4(1.0f, 0.5f, 0.35f, 1.0f);
        ImGui::SetCursorScreenPos(ImVec2(cP.x + pad, cP.y + 300));
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::PushTextWrapPos(cE.x - pad);
        ImGui::TextWrapped("%s", g_verify_msg);
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
    }

    // 退出
    ImGui::SetCursorScreenPos(ImVec2(cE.x - 92, cE.y - 36));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.10f, 0.10f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.20f, 0.20f, 1.0f));
    if (ImGui::Button("退出", ImVec2(72, 28))) {
        exit_decryption();
        kill(getpid(), SIGKILL);
    }
    ImGui::PopStyleColor(2);
}
