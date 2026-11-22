#pragma once

#include "globals.h"

namespace Platform {

    void InitRenderEnv();

    void RefreshDisplay();

    void LimitFrameRate();

    float DisplayRefreshRate();

    void SyncRotation();

    void RecreateOverlay();

    void UpdateScreenCenter();

    void BeginFrame();
}
