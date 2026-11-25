LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := aether_overlay.sh

LOCAL_CFLAGS := -Wall -O2 -DNDEBUG
# -DIMGUI_IMPL_OPENGL_ES3：让 imgui_impl_opengl3 走 GLES3 分支（GLSL "#version 300 es"）。
# 不加会退回内置 loader + "#version 130"，在 Android 上着色器编译失败（黑屏）。
LOCAL_CPPFLAGS := -std=c++17 -Wall -O2 -DNDEBUG -fno-rtti -fexceptions -DIMGUI_IMPL_OPENGL_ES3

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/include \
    $(LOCAL_PATH)/include/ImGui \
    $(LOCAL_PATH)/src

LOCAL_SRC_FILES := \
    src/main.cpp \
    src/AetherUI.cpp \
    src/TouchSimple.cpp \
    src/ImGui/imgui.cpp \
    src/ImGui/imgui_draw.cpp \
    src/ImGui/imgui_widgets.cpp \
    src/ImGui/imgui_tables.cpp \
    src/ImGui/imgui_impl_android.cpp \
    src/ImGui/imgui_impl_opengl3.cpp

LOCAL_LDLIBS := -llog -landroid -lEGL -lGLESv3 -ldl

# Android 15 起 16KB page size 设备要求
LOCAL_LDFLAGS += -Wl,-z,max-page-size=16384

include $(BUILD_EXECUTABLE)
