#pragma once

#include "types.h"

namespace Camera {

    struct ViewInfo {
        Vec3 Location{};
        Rotator Rotation{};
        float Fov = 0.0f;

        bool IsValid() const {
            return Location.IsFinite() && Rotation.IsFinite() &&
                   Fov >= 1.0f && Fov <= 179.0f;
        }
    };

    struct Projection {
        Vec3 Location{};
        Vec3 AxisX{};
        Vec3 AxisY{};
        Vec3 AxisZ{};
        float Scale = 0.0f;
        float HalfWidth = 0.0f;
        float HalfHeight = 0.0f;
        bool Ready = false;
    };

    Matrix RotationToMatrix(const Rotator &rotation);

    extern Projection Current;

    void Setup(const ViewInfo &info, float screenWidth, float screenHeight);

    inline bool IsReady() { return Current.Ready; }

    inline bool WorldToScreen(const Vec3 &world, Vec2 &screen) {
        if (!Current.Ready) {
            screen = Vec2::Invalid();
            return false;
        }

        const Vec3 offset = world - Current.Location;
        const float lateral = Vec3::Dot(offset, Current.AxisY);
        const float vertical = Vec3::Dot(offset, Current.AxisZ);
        const float depth = Vec3::Dot(offset, Current.AxisX);

        if (depth < 1.0f) return false;

        const float scale = Current.Scale / depth;
        screen.x = Current.HalfWidth + lateral * scale;
        screen.y = Current.HalfHeight - vertical * scale;
        return screen.IsFinite();
    }

    inline Vec2 WorldToScreen(const Vec3 &world) {
        Vec2 result;
        if (!WorldToScreen(world, result)) return Vec2::Invalid();
        return result;
    }

    inline bool Project(const Projection &projection, const Vec3 &world,
                        Vec2 &screen) {
        if (!projection.Ready) {
            screen = Vec2::Invalid();
            return false;
        }

        const Vec3 offset = world - projection.Location;
        const float lateral = Vec3::Dot(offset, projection.AxisY);
        const float vertical = Vec3::Dot(offset, projection.AxisZ);
        const float depth = Vec3::Dot(offset, projection.AxisX);

        if (depth < 1.0f) return false;

        const float scale = projection.Scale / depth;
        screen.x = projection.HalfWidth + lateral * scale;
        screen.y = projection.HalfHeight - vertical * scale;
        return screen.IsFinite();
    }

    // 齐次裁剪坐标。w 为纵深, 与 Scale 相乘后仍不除 w,
    // 便于先做近平面裁剪再做透视除法。
    struct ClipPoint {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
    };

    inline bool WorldToClip(const Vec3 &world, ClipPoint &out) {
        if (!Current.Ready) return false;

        const Vec3 offset = world - Current.Location;
        out.x = Vec3::Dot(offset, Current.AxisY) * Current.Scale;
        out.y = Vec3::Dot(offset, Current.AxisZ) * Current.Scale;
        out.w = Vec3::Dot(offset, Current.AxisX);
        return true;
    }

    inline Vec2 ClipToScreen(const ClipPoint &clip) {
        if (!(clip.w > 0.0f)) return Vec2::Invalid();
        return Vec2(Current.HalfWidth + clip.x / clip.w,
                    Current.HalfHeight - clip.y / clip.w);
    }

    inline bool AabbVisible(float minX, float minY, float minZ,
                            float maxX, float maxY, float maxZ) {
        if (!Current.Ready) return true;

        if (Current.Location.x >= minX && Current.Location.x <= maxX &&
            Current.Location.y >= minY && Current.Location.y <= maxY &&
            Current.Location.z >= minZ && Current.Location.z <= maxZ) {
            return true;
        }

        int front = 0;
        bool allLeft = true;
        bool allRight = true;
        bool allTop = true;
        bool allBottom = true;

        for (int i = 0; i < 8; ++i) {
            const Vec3 corner((i & 1) ? maxX : minX,
                              (i & 2) ? maxY : minY,
                              (i & 4) ? maxZ : minZ);

            ClipPoint clip;
            if (!WorldToClip(corner, clip)) return true;
            if (!(clip.w >= 1.0f)) continue;

            ++front;

            const float halfWidth = Current.HalfWidth * clip.w;
            const float halfHeight = Current.HalfHeight * clip.w;

            if (!(clip.x < -halfWidth)) allLeft = false;
            if (!(clip.x > halfWidth)) allRight = false;
            if (!(clip.y < -halfHeight)) allBottom = false;
            if (!(clip.y > halfHeight)) allTop = false;
        }

        if (front == 0) return false;
        if (allLeft || allRight || allTop || allBottom) return false;

        return true;
    }
}
