// AETHER 3D 悬浮菜单入口：
//   android::ANativeWindowCreator 建窗口 -> EGL/GLES3 -> ImGui -> 每帧 Draw -> eglSwapBuffers
// 界面全部画在 AetherUI.cpp 里，这里只负责生命周期。

#include <android/native_window.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <thread>

#include "ANativeWindowCreator.h"
#include "AetherUI.h"
#include "ProjectConfig.hpp"
#include "TouchSimple.h"
#include "imgui.h"
#include "imgui_impl_android.h"
#include "imgui_impl_opengl3.h"

#if APPUI_CJK_FONT
#include "Font.h"
#endif

static EGLDisplay g_display = EGL_NO_DISPLAY;
static EGLConfig g_config{};
static EGLSurface g_surface = EGL_NO_SURFACE;
static EGLContext g_context = EGL_NO_CONTEXT;
static ANativeWindow* g_window = nullptr;
static int g_screenW = 0, g_screenH = 0;
static float g_uiScale = 1.f;

static void CleanupEgl(bool releaseWindow) {
  if (g_display != EGL_NO_DISPLAY)
    eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  if (g_surface != EGL_NO_SURFACE) {
    eglDestroySurface(g_display, g_surface);
    g_surface = EGL_NO_SURFACE;
  }
  if (g_context != EGL_NO_CONTEXT) {
    eglDestroyContext(g_display, g_context);
    g_context = EGL_NO_CONTEXT;
  }
  if (g_display != EGL_NO_DISPLAY) {
    eglTerminate(g_display);
    g_display = EGL_NO_DISPLAY;
  }
  if (releaseWindow && g_window) {
    ANativeWindow_release(g_window);
    g_window = nullptr;
  }
}

static bool InitEgl(int w, int h) {
  g_window = android::ANativeWindowCreator::Create(PROJECT_OVERLAY_WINDOW_NAME, w, h, true);
  if (!g_window)
    return false;
  ANativeWindow_acquire(g_window);

  g_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (g_display == EGL_NO_DISPLAY)
    return false;
  if (!eglInitialize(g_display, nullptr, nullptr))
    return false;

  const EGLint attribs[] = {EGL_SURFACE_TYPE,
                            EGL_WINDOW_BIT,
                            EGL_RENDERABLE_TYPE,
                            EGL_OPENGL_ES2_BIT,
                            EGL_RED_SIZE,
                            8,
                            EGL_GREEN_SIZE,
                            8,
                            EGL_BLUE_SIZE,
                            8,
                            EGL_ALPHA_SIZE,
                            8,
                            EGL_DEPTH_SIZE,
                            16,
                            EGL_STENCIL_SIZE,
                            8,
                            EGL_NONE};
  EGLint n = 0;
  if (!eglChooseConfig(g_display, attribs, &g_config, 1, &n) || n <= 0)
    return false;

  EGLint format = 0;
  eglGetConfigAttrib(g_display, g_config, EGL_NATIVE_VISUAL_ID, &format);
  ANativeWindow_setBuffersGeometry(g_window, 0, 0, format);

  const EGLint ctxAttr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  g_context = eglCreateContext(g_display, g_config, EGL_NO_CONTEXT, ctxAttr);
  if (g_context == EGL_NO_CONTEXT)
    return false;
  g_surface = eglCreateWindowSurface(g_display, g_config, g_window, nullptr);
  if (g_surface == EGL_NO_SURFACE)
    return false;
  if (!eglMakeCurrent(g_display, g_surface, g_surface, g_context))
    return false;
  return true;
}

static bool InitImGui() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;                     // 不落 imgui.ini
  io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;  // 悬浮窗需要鼠标/触摸事件

#if APPUI_CJK_FONT
  {
    ImFontConfig cfg;
    cfg.OversampleH = 1;
    cfg.OversampleV = 1;
    cfg.PixelSnapH = true;
    cfg.FontDataOwnedByAtlas = false;  // 字体数据是静态数组，不交给 ImGui 释放
    const float fontPx = 34.f * g_uiScale;
    ImFont* font = io.Fonts->AddFontFromMemoryTTF((void*)OPPOSans_H, (int)OPPOSans_H_size, fontPx,
                                                 &cfg, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    if (!font)
      io.Fonts->AddFontDefault();
  }
#else
  io.Fonts->AddFontDefault();
#endif

    ImGui_ImplAndroid_Init(g_window);
  ImGui_ImplOpenGL3_Init("#version 300 es");
  return true;
}

static void Frame() {
  auto info = android::ANativeWindowCreator::GetDisplayInfo();
  if ((int)info.width != g_screenW || (int)info.height != g_screenH) {
    g_screenW = (int)info.width;
    g_screenH = (int)info.height;
    UpdateScreenData(g_screenW, g_screenH, info.orientation);
  }

  Touch_PumpAll();

  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2((float)g_screenW, (float)g_screenH);

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplAndroid_NewFrame(g_screenW, g_screenH);
  ImGui::NewFrame();

    AetherUI::Draw((float)g_screenW, (float)g_screenH, g_uiScale);

  ImGui::Render();
  glViewport(0, 0, g_screenW, g_screenH);
  glClearColor(0.f, 0.f, 0.f, 0.f);  // 全透明背景
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  eglSwapBuffers(g_display, g_surface);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  auto info = android::ANativeWindowCreator::GetDisplayInfo();
  g_screenW = (int)info.width;
  g_screenH = (int)info.height;
  if (g_screenW <= 0 || g_screenH <= 0) {
    fprintf(stderr, "overlay: failed to read display info\n");
    return 1;
  }

  const int shortSide = g_screenW < g_screenH ? g_screenW : g_screenH;
  g_uiScale = shortSide / 900.f;
  if (g_uiScale < 0.85f)
    g_uiScale = 0.85f;
  if (g_uiScale > 2.2f)
    g_uiScale = 2.2f;

  if (!InitEgl(g_screenW, g_screenH)) {
    fprintf(stderr, "overlay: EGL init failed\n");
    CleanupEgl(true);
    return 2;
  }
  if (!InitImGui()) {
    fprintf(stderr, "overlay: ImGui init failed\n");
    CleanupEgl(true);
    return 3;
  }

  Touch_Init(g_screenW, g_screenH, info.orientation, false);  // false = 独占 + 注入，需要 root
  printf("overlay running %dx%d scale=%.2f\n", g_screenW, g_screenH, g_uiScale);

  using clock = std::chrono::steady_clock;
  auto next = clock::now();
  while (!AetherUI::GetState().requestExit) {
    Frame();
    next += std::chrono::milliseconds(16);  // ~60fps
    auto now = clock::now();
    if (next > now)
      std::this_thread::sleep_until(next);
    else
      next = now;
  }

  Touch_Close();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplAndroid_Shutdown();
  ImGui::DestroyContext();
  CleanupEgl(true);
  return 0;
}
