/* TEXT_INJECTOR:BEGIN */
/*此源码完全免费，请勿被骗
看到不加群死全家@WanMiankernel
q群 1105344707
晚宀宝宝
当前注入时间：2026-09-05 12:09:45（北京时间）
*/
/* TEXT_INJECTOR:END */
#ifndef ANDROID_IMGUI_EMBEDDED_ASSETS_H
#define ANDROID_IMGUI_EMBEDDED_ASSETS_H

#include <cstddef>

// 字体以二进制数组内嵌 (heiti_ttf.cpp, 由 heiti.ttf 转换生成)
extern "C" {
extern const unsigned char g_heiti_ttf_data[];
extern const unsigned int g_heiti_ttf_size;
}

namespace EmbeddedAssets {

inline std::size_t HeitiSize() {
    return static_cast<std::size_t>(g_heiti_ttf_size);
}

} // namespace EmbeddedAssets

#endif // ANDROID_IMGUI_EMBEDDED_ASSETS_H
