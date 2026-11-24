#pragma once

#include "imgui.h"

#include "names.h"
#include "types.h"
#include "player.h"

namespace ESP {

    void DrawPlayers(ImDrawList *canvas);

    void DrawCollision(ImDrawList *canvas);

    void DrawObjectNames(ImDrawList *canvas);

    bool CalcBox(const Player::Entity &entity, const Vec2 &rootScreen, Vec2 &min,
                 Vec2 &max);

    void DrawBox(ImDrawList *canvas, const Vec2 &min, const Vec2 &max,
                 bool occluded);

    void DrawSkeleton(ImDrawList *canvas, const Player::Pose &pose,
                      float headRadius, ImU32 visibleColor,
                      ImU32 occludedColor);

    bool IsOccluded(const Vec3 &from, const Vec3 &to);

    void DumpBoneDebug(const Player::Entity &entity, const Player::Pose &pose);
}
