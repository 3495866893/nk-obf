/* TEXT_INJECTOR:BEGIN */
/*此源码完全免费，请勿被骗
看到不加群死全家@WanMiankernel
q群 1105344707
晚宀宝宝
当前注入时间：2026-09-05 12:09:45（北京时间）
*/
/* TEXT_INJECTOR:END */
#pragma once

#include "imgui.h"

struct ANativeWindow;
struct AInputEvent;

bool My_ImGui_ImplAndroid_Init(ANativeWindow *window);

int32_t My_ImGui_ImplAndroid_HandleInputEvent(AInputEvent *input_event);

int32_t My_ImGui_ImplAndroid_HandleInputEvent_old(AInputEvent *input_event);

void My_ImGui_ImplAndroid_Shutdown();

void My_ImGui_ImplAndroid_NewFrame(bool resize = false);