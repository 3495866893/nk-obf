#pragma once

#include <cstdint>

constexpr uint32_t ConfigMagic = 0x42504C4BU;
constexpr uint32_t ConfigVersion = 1;

struct Config {
    uint32_t Magic = ConfigMagic;
    uint32_t Version = ConfigVersion;
    uint32_t StructSize = 0;

    bool HideFromCapture = false;
    int GuiFps = 60;
    int TargetFps = 90;

    bool Box = false;
    bool Snapline = false;
    bool Skeleton = false;
    bool BoneName = false;
    bool BoneIndex = false;
    int  BoneKeySource = 1;
    bool CapsuleSize = false;
    bool Distance = true;
    bool ObjectName = false;
    bool PlayerCount = true;

    bool Collision = false;
    int CollisionMode = 0;

    int MotionLeadMs = 8;
};

namespace ConfigManager {

    extern Config Current;

    bool Load();

    bool Save();
}
