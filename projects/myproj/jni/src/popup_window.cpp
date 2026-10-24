#pragma once
// ====== 卡密到期弹窗模块 ======
#include "Draw.h"
#include "res/pz.h"

static bool popup_dismissed = false;   // 本次运行是否已关闭过弹窗
static bool popup_showing = false;     // 弹窗是否正在显示

 void ShowExpirePopup() {
    // 不需要弹窗的情况
    if (popup_dismissed) return;
    if (g_vip_time <= 0) return;       // 永久卡密

    time_t now = time(NULL);
    time_t remaining = g_vip_time - now;

    // 剩余 > 10分钟 不弹，已到期由主循环超时检测处理
    if (remaining > 600 || remaining <= 0) {
        if (popup_showing) popup_showing = false;
        return;
    }

    // 弹窗居中 —— 必须在 OpenPopup 之前设置，否则首次打开位置就会跑偏
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImVec2 center = ImVec2(display.x * 0.5f, display.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(420, 220), ImGuiCond_Always);

    // 每帧确保弹窗处于打开状态（重复 OpenPopup 不会叠加，只会刷新状态）
    popup_showing = true;
    ImGui::OpenPopup("卡密即将到期");

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.06f, 0.10f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.3f, 0.1f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.6f, 0.05f, 0.05f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.8f, 0.10f, 0.10f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.15f, 0.15f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.25f, 0.25f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.10f, 0.10f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.90f, 0.85f, 1.0f));

    if (ImGui::BeginPopupModal("卡密即将到期", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        int mins = (int)(remaining / 60);
        int secs = (int)(remaining % 60);

        ImGui::Spacing();
        ImGui::SetCursorPosX(40);
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.10f, 1.0f), "卡密即将到期！");

        ImGui::Spacing();
        ImGui::SetCursorPosX(40);
        char timebuf[64];
        sprintf(timebuf, "剩余时间: %d 分 %d 秒", mins, secs);
        ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.70f, 1.0f), "%s", timebuf);

        ImGui::Spacing();
        ImGui::SetCursorPosX(40);
        ImGui::TextColored(ImVec4(0.60f, 0.60f, 0.60f, 1.0f), "请及时续费, 到期后悬浮窗将自动退出");

        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::SetCursorPosX(140);
        if (ImGui::Button("确认", ImVec2(140, 50))) {
            popup_dismissed = true;
            popup_showing = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    } else {
        // 弹窗未打开（可能被其他 modal 挡住，或 OpenPopup 尚未生效）
        // 保持 popup_showing，下帧 OpenPopup 重试 —— 绝不提前 dismiss
    }

    ImGui::PopStyleColor(8);
    ImGui::PopStyleVar(3);
}
