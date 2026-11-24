#pragma once

#include <cstdint>
#include <memory>

#include "imgui.h"
#include "ANativeWindowCreator.h"
#include "AndroidImgui.h"

#include "types.h"

struct ImFont;
struct ImGuiWindow;
struct ANativeWindow;

struct WindowRect {
    float PosX = 0.0f;
    float PosY = 0.0f;
    float Width = 0.0f;
    float Height = 0.0f;
};

struct OcclusionProbe {
    bool Valid = false;
    bool Occluded = false;
    float ScreenDistance = 0.0f;

    int VisibleBones = 0;
    int TotalBones = 0;
};

struct AimedBone {
    bool Valid = false;
    int Index = -1;
    float Distance = 0.0f;
    Vec2 Screen{};
    char Name[64] = {};
};

extern OcclusionProbe NearestProbe;
extern AimedBone AimedBonePoint;

extern std::unique_ptr<AndroidImgui> graphics;
extern ANativeWindow *window;
extern android::ANativeWindowCreator::DisplayInfo displayInfo;
extern ImGuiWindow *g_window;
extern ImFont *zh_font;

extern int abs_ScreenX;
extern int abs_ScreenY;
extern int native_window_screen_x;
extern int native_window_screen_y;
extern float px;
extern float py;

extern long int libUE4;
extern long int Uworld;
extern long int Uleve;
extern long int Arrayaddr;
extern long int MySelf;
extern long int Gname;
extern long int PlayerController;
extern long int CameraManagerPtr;
extern int Count;

extern Vec3 CameraLocation;
extern Vec3 CameraRotation;
extern float CameraFov;

extern bool Initialized;
extern bool PhysXReady;
extern bool NeedRecreateOverlay;
extern bool NeedRestoreWindow;
extern int PlayerCount;
extern float FrameRate;
extern WindowRect LastWindow;

extern ImColor BoxColor;
extern ImColor SnaplineColor;
extern ImColor BoneColor;
extern ImColor OccludedBoxColor;
extern ImColor OccludedLineColor;
