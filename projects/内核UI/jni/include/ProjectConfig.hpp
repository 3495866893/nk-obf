#pragma once

#define PROJECT_OVERLAY_WINDOW_NAME "AetherMenu"
#define PROJECT_SURFACE_LOG_TAG "AetherMenu"

/* 1 = 从 Font.h 内嵌的 OPPO Sans 加载中文字体；0 = 用 ImGui 默认字体（可同时删掉 src/Font.h） */
#ifndef APPUI_CJK_FONT
#define APPUI_CJK_FONT 1
#endif

/* 触摸层是否对 /dev/input 做 EVIOCGRAB 独占（需要 root；仅想显示界面时保持 0） */
#ifndef PROJECT_TOUCH_CHMOD_PROC_BUS_INPUT
#define PROJECT_TOUCH_CHMOD_PROC_BUS_INPUT 0
#endif
