#pragma once

#include <cstdint>
#include <vector>

#include "types.h"
#include "memory.h"
#include "bone.h"

namespace Player {

    struct Entity {
        uint64_t Address = 0;
        uint64_t RootComponent = 0;
        uint64_t CapsuleComponent = 0;
        uint64_t Mesh = 0;
        float CapsuleRadius = 0.0f;
        Vec3 WorldLocation{};

        bool HasRoot() const { return RootComponent != 0; }

        bool HasMesh() const { return Mesh != 0; }
    };

    struct Pose {
        Vec3 Points[Skeleton::KeyCount]{};
        bool Visible[Skeleton::KeyCount]{};
        bool Valid = false;
        Matrix ComponentMatrix{};
        std::vector<BoneTransform> Bones;
    };

    using VisibilityFn = bool (*)(const Vec3 &);

    bool IsValidPointer(uint64_t address);

    bool ReadEntity(Reader *reader, uint64_t address, Entity &out);

    bool IsCharacter(const Entity &entity);

    bool ReadPose(Reader *reader, const Entity &entity, const Vec3 &boneOffset,
                  bool needVisibility, VisibilityFn visibilityFn, Pose &out);
}
