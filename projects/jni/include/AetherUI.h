#pragma once

#include "imgui.h"

// AETHER 玻璃拟态 3D 菜单（仿 B 站「Deepseek 写的 3D 悬浮窗」）
// 纯 ImDrawList 绘制 + 透视投影，不依赖 FBO 与自定义着色器。
namespace AetherUI {

enum Page : int { PAGE_VISUAL = 0, PAGE_SETTINGS = 1, PAGE_THEME = 2 };

struct Theme {
  const char* cn;
  const char* en;
  ImU32 c0, c1, c2;  // 封面渐变色
};

struct State {
  // 页面 / 皮肤
  int page = PAGE_THEME;
  bool darkSkin = false;   // false = 浅雾, true = 暗夜全息
  int accentIdx = 0;       // 强调色索引
  int themeIdx = 0;        // 当前主题卡

  // 外观参数
  float menuScale = 1.00f;
  float fontSize = 1.45f;
  float menuAlpha = 1.00f;
  float fpsTarget = 122.f;
  bool rainFx = true;
  bool use3D = true;

  // 视觉页（纯 UI 演示，不接任何游戏数据）
  bool drawBox = true, drawName = true, drawBone = false, drawHp = true;
  bool drawRadar = true, drawAimLine = false;
  float fovX = 300.f, fovY = 400.f, radarRange = 200.f;

  // 设置页
  bool driverLinked = false;

  // 3D 姿态
  float yaw = 0.f, pitch = 0.f;
  float tYaw = 0.f, tPitch = 0.f;  // 目标姿态（松手回弹）

  bool requestExit = false;
  bool open = true;
};

State& GetState();

// 每帧调用；screenW/H 为整屏像素，uiScale 为整体缩放基准
void Draw(float screenW, float screenH, float uiScale);

// 触摸命中：点是否落在面板（或收起后的小球）上
bool PointBlocksTouch(float x, float y);

}  // namespace AetherUI
