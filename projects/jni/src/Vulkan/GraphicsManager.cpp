/* TEXT_INJECTOR:BEGIN */
/*此源码完全免费，请勿被骗
看到不加群死全家@WanMiankernel
q群 1105344707
晚宀宝宝
当前注入时间：2026-09-05 12:09:45（北京时间）
*/
/* TEXT_INJECTOR:END */
//
// Created by ITEK on 2024/2/3.
//

#include "GraphicsManager.h"
#include "VulkanGraphics.h"

std::unique_ptr<AndroidImgui> GraphicsManager::getGraphicsInterface() {
    return std::make_unique<VulkanGraphics>();
}
