LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := main

LOCAL_CPPFLAGS := -w -std=c++14

# OLLVM 混淆配置（保守版，兼顾安卓设备内存限制）
LOCAL_CPPFLAGS += -mllvm -bcf               # 虚假控制流
LOCAL_CPPFLAGS += -mllvm -bcf_prob=70       # 混淆概率70%
LOCAL_CPPFLAGS += -mllvm -bcf_loop=2        # 重复2次
LOCAL_CPPFLAGS += -mllvm -fla               # 控制流平坦化
LOCAL_CPPFLAGS += -mllvm -sub               # 指令替换
LOCAL_CPPFLAGS += -mllvm -sub_loop=1        # 替换1次
LOCAL_CPPFLAGS += -mllvm -sobf              # 字符串混淆

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH) \
    $(LOCAL_PATH)/res

LOCAL_SRC_FILES := main.cpp

LOCAL_LDLIBS := -llog -landroid

include $(BUILD_EXECUTABLE)