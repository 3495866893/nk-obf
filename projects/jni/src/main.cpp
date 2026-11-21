#include "globals.h"
#include "memory.h"
#include "platform.h"
#include "game.h"
#include "camera.h"
#include "esp.h"
#include "ui.h"
#include "config.h"
#include "physics.h"

#include "GraphicsManager.h"
#include "TouchHelperA.h"

#include "../res/xl_verify.h"   /* XL云验证：SM4-CBC(动态IV)+HMAC-SM3 卡密登录 */

#include <cstdio>

Reader *dr = nullptr;

std::unique_ptr<AndroidImgui> graphics = nullptr;
ANativeWindow *window = nullptr;
android::ANativeWindowCreator::DisplayInfo displayInfo;
ImGuiWindow *g_window = nullptr;
ImFont *zh_font = nullptr;

int abs_ScreenX = 0;
int abs_ScreenY = 0;
int native_window_screen_x = 0;
int native_window_screen_y = 0;
float px = 0.0f;
float py = 0.0f;

long int libUE4 = 0;
long int Uworld = 0;
long int Uleve = 0;
long int Arrayaddr = 0;
long int MySelf = 0;
long int Gname = 0;
long int PlayerController = 0;
long int CameraManagerPtr = 0;
int Count = 0;

Vec3 CameraLocation;
Vec3 CameraRotation;
float CameraFov = 0.0f;

bool Initialized = false;
bool PhysXReady = false;
bool NeedRecreateOverlay = false;
bool NeedRestoreWindow = false;
int PlayerCount = 0;
float FrameRate = 120.0f;
WindowRect LastWindow;
OcclusionProbe NearestProbe;
AimedBone AimedBonePoint;

ImColor BoxColor = ImColor(255, 0, 0, 255);
ImColor SnaplineColor = ImColor(255, 0, 0, 255);
ImColor BoneColor = ImColor(255, 0, 0, 255);
ImColor OccludedBoxColor = ImColor(0, 255, 0, 255);
ImColor OccludedLineColor = ImColor(0, 255, 0, 255);

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;
    ConfigManager::Load();

    /* 卡密验证：已存卡密则后台自动登录，否则进入登录窗 */
    XLV::Init();


    ::graphics = GraphicsManager::getGraphicsInterface();
    if (!::graphics) {
        printf("renderer creation failed\n");
        return 1;
    }

    Platform::RefreshDisplay();

    ::native_window_screen_x = ::displayInfo.width;
    ::native_window_screen_y = ::displayInfo.height;
    ::abs_ScreenX = ::displayInfo.width;
    ::abs_ScreenY = ::displayInfo.height;

    ::window = android::ANativeWindowCreator::Create(
        "test", native_window_screen_x, native_window_screen_y,
        ConfigManager::Current.HideFromCapture);

    ::graphics->Init_Render(::window, native_window_screen_x,
                            native_window_screen_y);

    Touch::Init({static_cast<float>(::abs_ScreenX),
                 static_cast<float>(::abs_ScreenY)},
                true);
    Touch::setOrientation(displayInfo.orientation);

    Platform::InitRenderEnv();

    bool running = true;
    while (running) {
        Platform::BeginFrame();

        Touch::UpdateImGuiInput();
        graphics->NewFrame(true);

        XLV::Tick();

        if (XLV::Verified()) {
            static bool login_logged = false;
            if (!login_logged) {
                login_logged = true;
                XLV_LOGI("验证通过，到期时间：%s", XLV::Expiry().c_str());
            }

            UI::Layout_tick_UI(&running);

            Game::Update();

            if (Game::IsAttached() && CameraFov > 0.0f) {
                Camera::Setup({CameraLocation,
                               Rotator{CameraRotation.x, CameraRotation.y,
                                       CameraRotation.z},
                               CameraFov},
                              static_cast<float>(abs_ScreenX),
                              static_cast<float>(abs_ScreenY));
            } else {
                Camera::Setup({}, 0.0f, 0.0f);
            }

            ESP::DrawPlayers(ImGui::GetForegroundDrawList());
            ESP::DrawCollision(ImGui::GetForegroundDrawList());
            ESP::DrawObjectNames(ImGui::GetForegroundDrawList());
        } else {
            /* 未通过卡密验证：只显示登录窗，不挂游戏、不绘制 */
            XLV::ShowWindow(&running);
        }

        graphics->EndFrame();

        Platform::LimitFrameRate();
    }

    Physics::StopWorkerThreads();

    Touch::Close();
    graphics->Shutdown();
    android::ANativeWindowCreator::Destroy(::window);

    return 0;
}