LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := hack

LOCAL_CFLAGS := -std=gnu17
LOCAL_CPPFLAGS := -std=gnu++20
LOCAL_CPPFLAGS += -fexceptions
LOCAL_CPPFLAGS += -frtti
LOCAL_CPPFLAGS += -fvisibility=hidden
LOCAL_CPPFLAGS += -fdata-sections
LOCAL_CPPFLAGS += -ffunction-sections
LOCAL_CPPFLAGS += -funwind-tables
LOCAL_CPPFLAGS += -fstack-protector-strong

LOCAL_CPPFLAGS += -DVK_USE_PLATFORM_ANDROID_KHR
LOCAL_CPPFLAGS += -DIMGUI_IMPL_VULKAN_NO_PROTOTYPES
LOCAL_CPPFLAGS += -DIMGUI_DISABLE_DEBUG_TOOLS
# ---- 云验证对接方式（二选一）----
# A) 直连官方后台（默认，不定义宏）：XLV_REQUIRE_SIG 自动为 0
#    LOCAL_CPPFLAGS += -DXLV_HOST='"yz.dujiuyue.cn"'
# B) 走自建签名中转（推荐，能真正防伪造）：取消下面两行，
#    并把域名换成你部署的中转地址、把证书 pin 换成该域名的 SPKI-SHA256
# LOCAL_CPPFLAGS += -DXLV_RELAY
# LOCAL_CPPFLAGS += -DXLV_HOST='"relay.example.com"'
# ---- 开发者设备白名单（默认已开启，不用改）----
# 第0层 令牌文件 /storage/emulated/0/Android/.xlv_dev  已启用  -DXLV_DEV_KEY=0 可关
# 第2层 su 读 serial（需 KernelSU 授权一次）        始终启用
# 第1层 纯属性兜底（同型号同固件会撞，不安全）      默认关  -DXLV_DEV_WEAK=1 可开


LOCAL_C_INCLUDES += $(LOCAL_PATH)/include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/ImGui
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/Vulkan

LOCAL_SRC_FILES := src/main.cpp
LOCAL_SRC_FILES += src/core.cpp
LOCAL_SRC_FILES += src/draw.cpp
LOCAL_SRC_FILES += src/ImGui/imgui.cpp
LOCAL_SRC_FILES += src/ImGui/imgui_draw.cpp
LOCAL_SRC_FILES += src/ImGui/imgui_tables.cpp
LOCAL_SRC_FILES += src/ImGui/imgui_widgets.cpp
LOCAL_SRC_FILES += src/ImGui/imgui_impl_vulkan.cpp
LOCAL_SRC_FILES += src/ImGui/AndroidImgui.cpp
LOCAL_SRC_FILES += src/ImGui/my_imgui_impl_android.cpp
LOCAL_SRC_FILES += src/ImGui/TouchHelperA.cpp
LOCAL_SRC_FILES += src/ImGui/stb_image.cpp
LOCAL_SRC_FILES += src/ImGui/heiti_ttf.cpp
LOCAL_SRC_FILES += src/Vulkan/GraphicsManager.cpp
LOCAL_SRC_FILES += src/Vulkan/VulkanGraphics.cpp
LOCAL_SRC_FILES += src/Vulkan/vulkan_wrapper.cpp

LOCAL_LDLIBS := -llog -landroid -lz -ldl

include $(BUILD_EXECUTABLE)