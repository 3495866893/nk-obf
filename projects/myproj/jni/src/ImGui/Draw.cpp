#include "Draw.h"
#include "Font.h"

EGLDisplay display = EGL_NO_DISPLAY;
EGLConfig config;
EGLSurface surface = EGL_NO_SURFACE;
EGLContext context = EGL_NO_CONTEXT;

ANativeWindow *native_window;
//
int native_window_screen_x = 0;
int native_window_screen_y = 0;
android::ANativeWindowCreator::DisplayInfo displayInfo{0};
uint32_t orientation = 0;
bool g_Initialized = false;


bool initGUI_draw(uint32_t _screen_x, uint32_t _screen_y, bool log) {
    orientation = displayInfo.orientation;
        InitVulkan();
        SetupVulkan();
        ::native_window = android::ANativeWindowCreator::Create("AImGui", _screen_x, _screen_y, false);
        SetupVulkanWindow(::native_window, (int) _screen_x, (int) _screen_y);
    if (!ImGui_init()) {
        return false;
    }   
        UploadFonts();
    return true;
}

void screen_config() {
    displayInfo = android::ANativeWindowCreator::GetDisplayInfo();
}
void ImGuiMenustyle1()
{
ImGuiStyle & style = ImGui::GetStyle();

style.WindowBorderSize = 0.0;//框架描边宽度
style.WindowTitleAlign = ImVec2(-2, 0.6);//标题居中
style.WindowRounding = 0.5;//窗口圆角
style.FrameRounding = 4;//滚动条圆角
style.GrabRounding = 6;//滑动条圆角
style.FramePadding = ImVec2(2, 2);//标签栏长度
style.ScrollbarSize = 50; // 设置滚动条宽度
style.GrabMinSize = 40; // 设置滑块宽度
style.FrameBorderSize = 0.0; // 设置控件描边宽度

// 文本颜色 - 黑色
style.Colors[ImGuiCol_Text] = ImColor(0, 0, 0, 255);

// 窗口背景 - 纯白
style.Colors[ImGuiCol_WindowBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

// 弹出窗口背景 - 浅灰色，轻微透明
style.Colors[ImGuiCol_PopupBg] = ImVec4(0.86f, 0.86f, 0.86f, 0.63f);

// 边框颜色 - 浅灰色
style.Colors[ImGuiCol_Border] = ImColor(200, 200, 200, 255);

// 框架背景（如InputText、Slider等控件的背景）- 非常浅的灰，与纯白背景区分
style.Colors[ImGuiCol_FrameBg] = ImColor(240, 240, 240, 255);
style.Colors[ImGuiCol_FrameBgHovered] = ImColor(230, 230, 230, 255); // 悬停时稍深
style.Colors[ImGuiCol_FrameBgActive] = ImColor(220, 220, 220, 255);  // 激活时（如正在输入）再深一点

// 滚动条 - 中性灰色
style.Colors[ImGuiCol_ScrollbarBg] = ImColor(240, 240, 240, 255); // 背景
style.Colors[ImGuiCol_ScrollbarGrab] = ImColor(180, 180, 180, 255); // 滑块
style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImColor(160, 160, 160, 255); // 悬停
style.Colors[ImGuiCol_ScrollbarGrabActive] = ImColor(140, 140, 140, 255); // 点击

// 复选框标记、滑块滑块 - 深灰色，与背景形成对比
style.Colors[ImGuiCol_CheckMark] = ImColor(80, 80, 80, 255);
style.Colors[ImGuiCol_SliderGrab] = ImColor(160, 160, 160, 255);
style.Colors[ImGuiCol_SliderGrabActive] = ImColor(120, 120, 120, 255);

// 分隔线 - 浅灰色
style.Colors[ImGuiCol_Separator] = ImColor(200, 200, 200, 255);
style.Colors[ImGuiCol_SeparatorHovered] = ImColor(190, 190, 190, 255);
style.Colors[ImGuiCol_SeparatorActive] = ImColor(180, 180, 180, 255);

// 按钮 - 浅灰色，并区分状态
style.Colors[ImGuiCol_Button] = ImColor(225, 225, 225, 255); // 普通
style.Colors[ImGuiCol_ButtonHovered] = ImColor(210, 210, 210, 255); // 悬停
style.Colors[ImGuiCol_ButtonActive] = ImColor(190, 190, 190, 255); // 点击

// 标题（如可折叠区域的头部）- 区分状态
style.Colors[ImGuiCol_Header] = ImColor(235, 235, 235, 255); // 普通
style.Colors[ImGuiCol_HeaderHovered] = ImColor(220, 220, 220, 255); // 悬停
style.Colors[ImGuiCol_HeaderActive] = ImColor(200, 200, 200, 255); // 点击

// 窗口标题栏
style.Colors[ImGuiCol_TitleBg] = ImColor(230, 230, 230, 255); // 非激活
style.Colors[ImGuiCol_TitleBgActive] = ImColor(210, 210, 210, 255); // 激活
style.Colors[ImGuiCol_TitleBgCollapsed] = ImColor(245, 245, 245, 200); // 折叠

}

bool loadSystemFont() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontFromMemoryTTF((void*)OPPOSans_H, OPPOSans_H_size, 45.0f, NULL, io.Fonts->GetGlyphRangesChineseFull());    
    return true;
}

bool ImGui_init() {
    if (g_Initialized) {
        return true;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplAndroid_Init(native_window);
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    // 加载系统字体
    bool success = loadSystemFont();
    if (!success) {
        // 处理加载字体失败的情况
        return false;
    }
    ImGui::GetStyle().ScaleAllSizes(3.0f);
    ImGuiMenustyle1();
    ::g_Initialized = true;
    return true;
}


void drawBegin() {
    screen_config();
    if (::orientation != displayInfo.orientation) {
        ::orientation = displayInfo.orientation;
        UpdateScreenData(displayInfo.width, displayInfo.height, displayInfo.orientation);

        }
      ImGui_ImplVulkan_NewFrame();
     ImGui_ImplAndroid_NewFrame(native_window_screen_x, native_window_screen_y);
    ImGui::NewFrame();
}

void drawEnd() {
    ImGui::Render();
    FrameRender(ImGui::GetDrawData());
    FramePresent();
}

void shutdown() {
    if (!g_Initialized) {
        return;
    }
    DeviceWait();
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplAndroid_Shutdown();
    ImGui::DestroyContext();
    CleanupVulkanWindow();
    CleanupVulkan();
    ANativeWindow_release(native_window);
    android::ANativeWindowCreator::Destroy(native_window);
    ::g_Initialized = false;
}
