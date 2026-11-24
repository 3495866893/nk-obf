#pragma once

#include <cstdint>
#include <vector>

#include "types.h"
#include "globals.h"

namespace Game {

    struct Context {
        uintptr_t Module = 0;
        uintptr_t World = 0;
        uintptr_t PlayerController = 0;
        uintptr_t Self = 0;
        uintptr_t CameraManager = 0;
        uintptr_t CameraCache = 0;
        Vec3 CameraLocation{};
        Rotator CameraRotation{};
        float Fov = 0.0f;
    };

    extern Context Current;

    bool Attach(const char *package = nullptr);

    bool IsAttached();

    void Update();

    const std::vector<uint64_t> &Entities();
}
