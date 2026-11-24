#pragma once

#include <cstdint>

#include "memory.h"

namespace Offset {

    constexpr const char *ProcessName = "com.idreamsky.klbqm";  // 卡拉彼丘 国服
    constexpr const char *ModuleName = "libUE4.so";             // 游戏逻辑模块

    constexpr uintptr_t Gworld = 0xB46EDD0;       // 世界
     constexpr uintptr_t GNamesSlot = 0xB2B9280;     // gname

    constexpr uintptr_t Uleve[] = {0x30};                       // 关卡: 世界 -> 0x30
    constexpr uintptr_t NetDriver[] = {0x188, 0x38, 0x0, 0x30}; // 人物控制器

    constexpr uintptr_t EntityArray = 0x98;       // 实体数组首地址
    constexpr uintptr_t EntityCount = 0xA0;       // 实体数组元素个数

    constexpr uintptr_t Pawn = 0x390;             // 自身角色
    constexpr uintptr_t CameraManager = 0x3A8;    // 相机管理器

    constexpr uintptr_t CameraLocation = 0x2300;  // 相机位置
    constexpr uintptr_t CameraRotation = 0x230C;  // 相机旋转 (Pitch / Yaw / Roll)
    constexpr uintptr_t CameraFov = 0x2318;       // 视场角

    constexpr uintptr_t RootComponent = 0x168;      // 根组件
    constexpr uintptr_t RootWorldLocation = 0x1F0;  // 根组件世界坐标
    constexpr uintptr_t CapsuleComponent = 0x380;   // 胶囊体组件
    constexpr uintptr_t CapsuleRadius = 0x54C;      // 胶囊体半径
    constexpr uintptr_t Mesh = 0x370;               // 骨骼网格组件
    constexpr uintptr_t ComponentToWorld = 0x2A0;   // 网格组件变换 (四元数 + 平移 + 缩放)
    constexpr uintptr_t BoneArray = 0x578;          // 骨骼数组指针 (需再解引用一次)

    constexpr uintptr_t BoneStride = 48;            // 单根骨骼占用的字节数

    constexpr uintptr_t MeshSkeletalMesh = 0x558;   // 网格组件
    constexpr uintptr_t SkeletonBoneInfo = 0x1A8;   // 骨架资源
    constexpr uintptr_t SkeletonRefPose = 0x1B8;    // 骨架资源
    constexpr uintptr_t BoneNameStride = 12;        // 骨骼名表单项

    constexpr uintptr_t PhysXInstance = 0xB312B88;  // PhysX 实例 (模块基址 +)
    constexpr uintptr_t RigidActors = 0x25E8;       // 场景 -> 刚体数组
    constexpr uintptr_t ShapeManager = 0x2430;      // 场景 -> 形状管理器

    constexpr uintptr_t ObjectClass = 0x10;         // UObject::ClassPrivate (UClass*)
    constexpr uintptr_t ObjectName = 0x18;          // UObject::NamePrivate (FName)

    constexpr float CharacterRadius = 40.0f;        // 角色胶囊体半径实测值
    constexpr float RadiusTolerance = 0.1f;         // 判定容差

    constexpr float HeadHeight = 165.0f;            // 根坐标 -> 头顶
    constexpr float FootHeight = -5.0f;             // 根坐标 -> 脚底
    constexpr float ProbeHeight = 100.0f;           // 遮挡检测取样高度

    constexpr uintptr_t MinValidPointer = 0x10000000ULL;     // 用户态地址下限
    constexpr uintptr_t MaxValidPointer = 0x10000000000ULL;  // 用户态地址上限
    constexpr uintptr_t MinModuleBase = 0x10000ULL;          // 模块基址下限

    // 沿链逐级"加偏移 + 解引用", 返回最后一级读到的对象地址
    // 起点由调用方给, 例: ReadChain(reader, world, Offset::NetDriver)
    template <size_t N>
    inline uintptr_t ReadChain(Reader *reader, uintptr_t start,
                               const uintptr_t (&chain)[N]) {
        if (reader == nullptr || start == 0) return 0;

        uintptr_t current = start;
        for (size_t i = 0; i < N; ++i) {
            uint64_t next = 0;
            if (!reader->Read(current + chain[i], next) || next == 0) return 0;
            current = static_cast<uintptr_t>(next);
        }
        return current;
    }

    // 同上, 但最后一级只相加不解引用, 用于"对象 -> 字段地址"
    template <size_t N>
    inline uintptr_t ReadFieldAddress(Reader *reader, uintptr_t start,
                                      const uintptr_t (&chain)[N]) {
        if (reader == nullptr || start == 0 || N == 0) return 0;

        uintptr_t current = start;
        for (size_t i = 0; i + 1 < N; ++i) {
            uint64_t next = 0;
            if (!reader->Read(current + chain[i], next) || next == 0) return 0;
            current = static_cast<uintptr_t>(next);
        }
        return current + chain[N - 1];
    }
}
