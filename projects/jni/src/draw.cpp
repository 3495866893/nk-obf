#include "esp.h"
#include "ui.h"
#include "globals.h"
#include "config.h"
#include "camera.h"
#include "game.h"
#include "player.h"
#include "offsets.h"
#include "memory.h"
#include "physics.h"
#include "platform.h"

#include "AndroidImgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <android/log.h>

namespace ESP {

    bool IsOccluded(const Vec3 &from, const Vec3 &to) {
        if (DynamicLoadScene == nullptr) return false;

        physx::PxVec3 origin(from.x, from.y, from.z);
        physx::PxVec3 target(to.x, to.y, to.z);

        return DynamicLoadScene->Occluded(origin, target);
    }

    static bool PointVisible(const Vec3 &point) {
        return !IsOccluded(Camera::Current.Location, point);
    }

    bool CalcBox(const Player::Entity &entity, const Vec2 &rootScreen, Vec2 &min,
                 Vec2 &max) {
        const Vec3 &root = entity.WorldLocation;

        Vec2 topScreen;
        Vec2 bottomScreen;

        if (!Camera::WorldToScreen(
                Vec3(root.x, root.y, root.z + Offset::HeadHeight), topScreen)) {
            return false;
        }
        if (!Camera::WorldToScreen(
                Vec3(root.x, root.y, root.z + Offset::FootHeight), bottomScreen)) {
            return false;
        }

        const float span = bottomScreen.y - topScreen.y;
        if (!std::isfinite(span) || span <= 1.0f) return false;

        const float width = span * 0.5f;
        if (!std::isfinite(width) || width <= 1.0f) return false;

        const float left = rootScreen.x - span * 0.25f;
        const float bottomY = bottomScreen.y;
        const float topY = bottomY - width;
        const float right = left + width;
        const float bottom = bottomY + width;

        if (width > static_cast<float>(abs_ScreenX)) return false;
        if (right < 0.0f || left > static_cast<float>(abs_ScreenX)) return false;
        if (bottom < 0.0f || topY > static_cast<float>(abs_ScreenY)) return false;

        min = Vec2(left, topY);
        max = Vec2(right, bottom);
        return true;
    }

    void DrawBox(ImDrawList *canvas, const Vec2 &min, const Vec2 &max,
                 bool occluded) {
        canvas->AddRect(ImVec2(min.x, min.y), ImVec2(max.x, max.y),
                        occluded ? OccludedBoxColor : BoxColor, 0.0f, 0, 1.0f);
    }

    void DrawSkeleton(ImDrawList *canvas, const Player::Pose &pose,
                      float headRadius, ImU32 visibleColor,
                      ImU32 occludedColor) {
        if (!pose.Valid) return;

        Vec2 screen[Skeleton::KeyCount];
        for (int i = 0; i < Skeleton::KeyCount; ++i) {
            screen[i] = Camera::WorldToScreen(pose.Points[i]);
        }

        for (int i = 0; i < Skeleton::LinkCount; ++i) {
            const int a = Skeleton::LinkA[i];
            const int b = Skeleton::LinkB[i];

            if (!screen[a].IsFinite() || !screen[b].IsFinite()) continue;

            const bool anyVisible = pose.Visible[a] || pose.Visible[b];

            canvas->AddLine(ImVec2(screen[a].x, screen[a].y),
                            ImVec2(screen[b].x, screen[b].y),
                            anyVisible ? visibleColor : occludedColor, 2.5f);
        }

        if (screen[0].IsFinite()) {
            canvas->AddCircle(ImVec2(screen[0].x, screen[0].y), headRadius,
                              pose.Visible[0] ? visibleColor : occludedColor, 0);
        }
    }

    void DrawPlayers(ImDrawList *canvas) {
        if (canvas == nullptr || !Game::IsAttached()) return;
        if (!Camera::IsReady()) return;
        if (Arrayaddr == 0 || Count <= 0 || MySelf == 0) return;

        const Config &settings = ConfigManager::Current;

        const bool anyBoneLabel = settings.BoneName || settings.BoneIndex;

        const bool needPose = settings.Skeleton || anyBoneLabel ||
                              settings.Box || settings.Snapline;
        const bool testOcclusion = PhysXReady && needPose;

        PlayerCount = 0;
        NearestProbe = OcclusionProbe{};
        AimedBonePoint = AimedBone{};

        Player::Entity entity;
        Player::Pose pose;

        Vec3 selfLocation = Camera::Current.Location;
        {
            Player::Entity self;
            if (Player::ReadEntity(dr, static_cast<uint64_t>(MySelf), self)) {
                selfLocation = self.WorldLocation;
            }
        }

        const std::vector<uint64_t> &entities = Game::Entities();

        for (size_t i = 0; i < entities.size(); ++i) {
            const uint64_t address = entities[i];
            if (!Player::IsValidPointer(address) ||
                address == static_cast<uint64_t>(MySelf)) {
                continue;
            }

            if (!Player::ReadEntity(dr, address, entity)) continue;
            if (!Player::IsCharacter(entity)) continue;

            PlayerCount++;

            if (settings.Skeleton || anyBoneLabel) {
                static int resolveCooldown = 0;
                if (settings.BoneKeySource == 1 && !Skeleton::NameResolved &&
                    --resolveCooldown <= 0) {
                    resolveCooldown = 90;

                    uint64_t skeletalMesh = 0;
                    if (entity.HasMesh() &&
                        dr->Read(entity.Mesh + Offset::MeshSkeletalMesh,
                                 skeletalMesh) &&
                        skeletalMesh != 0 &&
                        Skeleton::ResolveKeyBones(dr, skeletalMesh) > 0) {
                        Skeleton::ApplyKeyBoneSource(1);
                    }
                }
            }

            const Vec3 &root = entity.WorldLocation;

            Vec2 rootScreen;
            if (!Camera::WorldToScreen(root, rootScreen)) continue;

            const float dx = rootScreen.x - px;
            const float dy = rootScreen.y - py;
            const float screenDistance = dx * dx + dy * dy;

            const bool isNearest =
                !NearestProbe.Valid || screenDistance < NearestProbe.ScreenDistance;
            if (isNearest) {
                NearestProbe.Valid = true;
                NearestProbe.ScreenDistance = screenDistance;
            }

            Vec2 min;
            Vec2 max;
            if (!CalcBox(entity, rootScreen, min, max)) continue;

            bool poseReady = false;
            if (needPose) {
                poseReady = Player::ReadPose(dr, entity, Vec3::Zero(),
                                             testOcclusion, PointVisible, pose);
            }

            const bool occlusionTested = poseReady && testOcclusion;

            int visibleBones = 0;
            if (occlusionTested) {
                for (int k = 0; k < Skeleton::KeyCount; ++k) {
                    if (pose.Visible[k]) ++visibleBones;
                }
            }

            const bool occluded = occlusionTested && visibleBones == 0;

            if (isNearest) {
                NearestProbe.Occluded = occluded;
                NearestProbe.VisibleBones = visibleBones;
                NearestProbe.TotalBones = occlusionTested ? Skeleton::KeyCount : 0;
            }

            if (settings.Box) DrawBox(canvas, min, max, occluded);

            if (settings.Snapline) {
                canvas->AddLine(ImVec2(px, 130.0f), ImVec2(rootScreen.x, min.y),
                                occluded ? OccludedLineColor : SnaplineColor,
                                1.0f);
            }

            if (settings.CapsuleSize) {
                char text[32];
                snprintf(text, sizeof(text), "%.1f", entity.CapsuleRadius);
                canvas->AddText(ImVec2(min.x, min.y - 20.0f),
                                ImColor(255, 255, 0, 255), text);
            }

            if (settings.Distance) {
                char text[32];
                snprintf(text, sizeof(text), "%.0f m",
                         Vec3::Distance(selfLocation, root) / 100.0f);
                canvas->AddText(ImVec2(min.x, max.y + 4.0f),
                                ImColor(255, 255, 255), text);
            }

            if (!poseReady) continue;

            if (anyBoneLabel) DumpBoneDebug(entity, pose);

            if (settings.Skeleton) {
                const float headRadius = (max.x - min.x) / 5.0f;
                DrawSkeleton(canvas, pose, headRadius, BoneColor,
                             OccludedBoxColor);
            }

            if (anyBoneLabel) {
                uint64_t skeletalMesh = 0;
                dr->Read(entity.Mesh + Offset::MeshSkeletalMesh, skeletalMesh);
                const std::vector<std::string> *boneNames =
                    Skeleton::GetBoneNames(dr, skeletalMesh);

                const int limit = static_cast<int>(pose.Bones.size());
                for (int j = 0; j < limit; ++j) {
                    const Vec2 screen = Camera::WorldToScreen(
                        BoneLocalToWorld(pose.Bones, j, pose.ComponentMatrix));
                    if (!screen.IsFinite() || screen.x <= 0.0f || screen.y <= 0.0f) {
                        continue;
                    }

                    canvas->AddCircleFilled(ImVec2(screen.x, screen.y), 3.0f,
                                            ImColor(255, 0, 0));

                    float textY = screen.y - 26.0f;

                    if (settings.BoneName) {
                        char fallback[24];
                        const char *label = nullptr;
                        if (boneNames != nullptr &&
                            j < static_cast<int>(boneNames->size()) &&
                            !(*boneNames)[static_cast<size_t>(j)].empty()) {
                            label = (*boneNames)[static_cast<size_t>(j)].c_str();
                        } else {
                            snprintf(fallback, sizeof(fallback), "bone_%d", j);
                            label = fallback;
                        }
                        canvas->AddText(nullptr, 14.0f,
                                        ImVec2(screen.x + 12.0f, textY),
                                        ImColor(255, 255, 0), label);
                        textY += 16.0f;
                    }

                    if (settings.BoneIndex) {
                        char index[16];
                        snprintf(index, sizeof(index), "%d", j);
                        canvas->AddText(nullptr, 14.0f,
                                        ImVec2(screen.x + 12.0f, textY),
                                        ImColor(0, 255, 255), index);
                    }

                    const float boneDx = screen.x - px;
                    const float boneDy = screen.y - py;
                    const float boneDistance = boneDx * boneDx + boneDy * boneDy;
                    if (!AimedBonePoint.Valid ||
                        boneDistance < AimedBonePoint.Distance) {
                        AimedBonePoint.Valid = true;
                        AimedBonePoint.Index = j;
                        AimedBonePoint.Distance = boneDistance;
                        AimedBonePoint.Screen = screen;

                        if (boneNames != nullptr &&
                            j < static_cast<int>(boneNames->size())) {
                            snprintf(AimedBonePoint.Name,
                                     sizeof(AimedBonePoint.Name), "%s",
                                     (*boneNames)[static_cast<size_t>(j)].c_str());
                        } else {
                            AimedBonePoint.Name[0] = '\0';
                        }
                    }
                }
            }
        }

        if (anyBoneLabel && AimedBonePoint.Valid) {
            const Vec2 &aimed = AimedBonePoint.Screen;

            char indexText[16];
            const char *label = nullptr;

            if (settings.BoneName && AimedBonePoint.Name[0] != '\0') {
                label = AimedBonePoint.Name;
            } else if (settings.BoneIndex) {
                snprintf(indexText, sizeof(indexText), "%d", AimedBonePoint.Index);
                label = indexText;
            }

            if (label != nullptr) {
                canvas->AddCircle(ImVec2(aimed.x, aimed.y), 7.0f,
                                  ImColor(0, 255, 255), 0, 1.5f);

                canvas->AddText(ImGui::GetFont(), 30.0f,
                                ImVec2(aimed.x + 16.0f, aimed.y - 46.0f),
                                ImColor(0, 255, 255), label);
            }
        }

        if (settings.PlayerCount) {
            char text[64];
            snprintf(text, sizeof(text), "%d", PlayerCount);
            canvas->AddText(ImGui::GetFont(), 50.0f, ImVec2(px - 60.0f, 95.0f),
                            ImColor(255, 255, 255, 255), text);
        }
    }

    static int ClipTriangleNear(const Camera::ClipPoint in[3],
                                Camera::ClipPoint out[4]) {
        constexpr float MinDepth = 0.001f;
        int count = 0;

        for (int i = 0; i < 3; ++i) {
            const Camera::ClipPoint &cur = in[i];
            const Camera::ClipPoint &next = in[(i + 1) % 3];

            const bool curInside = cur.w >= MinDepth;
            const bool nextInside = next.w >= MinDepth;

            if (curInside) out[count++] = cur;

            if (curInside != nextInside) {
                const float t = (MinDepth - cur.w) / (next.w - cur.w);
                out[count].x = cur.x + (next.x - cur.x) * t;
                out[count].y = cur.y + (next.y - cur.y) * t;
                out[count].w = MinDepth;
                ++count;
            }
        }
        return count;
    }


    static bool TriangleHitsViewport(const Vec2 *v, float width, float height) {
        float minX = v[0].x, maxX = v[0].x;
        float minY = v[0].y, maxY = v[0].y;
        for (int i = 1; i < 3; ++i) {
            minX = std::min(minX, v[i].x);
            maxX = std::max(maxX, v[i].x);
            minY = std::min(minY, v[i].y);
            maxY = std::max(maxY, v[i].y);
        }

        if (maxX < 0.0f || minX > width) return false;
        if (maxY < 0.0f || minY > height) return false;

        for (int i = 0; i < 3; ++i) {
            const Vec2 &a = v[i];
            const Vec2 &b = v[(i + 1) % 3];

            const float nx = b.y - a.y;
            const float ny = a.x - b.x;

            float triMin = nx * a.x + ny * a.y;
            float triMax = triMin;
            for (int k = 0; k < 3; ++k) {
                const float p = nx * v[k].x + ny * v[k].y;
                triMin = std::min(triMin, p);
                triMax = std::max(triMax, p);
            }

            const float px = nx * width;
            const float py = ny * height;
            const float boxMin = std::min(0.0f, px) + std::min(0.0f, py);
            const float boxMax = std::max(0.0f, px) + std::max(0.0f, py);

            if (triMax < boxMin || triMin > boxMax) return false;
        }
        return true;
    }

    static void EmitTriangle(ImDrawList *canvas, const Vec2 &a, const Vec2 &b,
                             const Vec2 &c, ImU32 color) {
        canvas->AddTriangle(ImVec2(a.x, a.y), ImVec2(b.x, b.y), ImVec2(c.x, c.y),
                            color, 0.5f);
    }


    static void DrawMesh(TriangleMeshData *mesh, ImDrawList *canvas,
                         ImU32 color) {
        if (mesh == nullptr) return;

        const float viewportWidth = static_cast<float>(abs_ScreenX);
        const float viewportHeight = static_cast<float>(abs_ScreenY);

        for (size_t i = 0; i + 2 < mesh->Indices.size(); i += 3) {
            const auto &p0 = mesh->Vertices[mesh->Indices[i]];
            const auto &p1 = mesh->Vertices[mesh->Indices[i + 1]];
            const auto &p2 = mesh->Vertices[mesh->Indices[i + 2]];

            Camera::ClipPoint source[3];
            if (!Camera::WorldToClip(Vec3(p0.x, p0.y, p0.z), source[0])) continue;
            if (!Camera::WorldToClip(Vec3(p1.x, p1.y, p1.z), source[1])) continue;
            if (!Camera::WorldToClip(Vec3(p2.x, p2.y, p2.z), source[2])) continue;

            Camera::ClipPoint clipped[4];
            const int count = ClipTriangleNear(source, clipped);
            if (count < 3) continue;

            Vec2 screen[4];
            bool finite = true;
            for (int k = 0; k < count; ++k) {
                screen[k] = Camera::ClipToScreen(clipped[k]);
                if (!screen[k].IsFinite()) {
                    finite = false;
                    break;
                }
            }
            if (!finite) continue;

            if (count == 3) {
                if (!TriangleHitsViewport(screen, viewportWidth, viewportHeight)) {
                    continue;
                }
                EmitTriangle(canvas, screen[0], screen[1], screen[2], color);
                continue;
            }

            const Vec2 first[3] = {screen[0], screen[1], screen[2]};
            const Vec2 second[3] = {screen[0], screen[2], screen[3]};

            if (TriangleHitsViewport(first, viewportWidth, viewportHeight)) {
                EmitTriangle(canvas, first[0], first[1], first[2], color);
            }
            if (TriangleHitsViewport(second, viewportWidth, viewportHeight)) {
                EmitTriangle(canvas, second[0], second[1], second[2], color);
            }
        }
    }

    constexpr ImU32 CollisionMeshColor = IM_COL32(0, 255, 255, 255);
    constexpr ImU32 CollisionHeightFieldColor = IM_COL32(0, 110, 255, 255);

    static void DrawVisibleMesh(TriangleMeshData *mesh, ImDrawList *canvas,
                                ImU32 color) {
        if (mesh == nullptr) return;

        const RayTrace::Bounds &bounds = mesh->WorldBounds;
        if (!Camera::AabbVisible(bounds.minX, bounds.minY, bounds.minZ,
                                 bounds.maxX, bounds.maxY, bounds.maxZ)) {
            return;
        }

        DrawMesh(mesh, canvas, color);
    }

    void DrawCollision(ImDrawList *canvas) {
        if (canvas == nullptr) return;
        if (!ConfigManager::Current.Collision) return;
        if (!Camera::IsReady()) return;

        if (ConfigManager::Current.CollisionMode == 0) {
            if (DynamicLoadScene) {
                for (const auto &mesh : DynamicLoadScene->GetMeshDatas()) {
                    DrawVisibleMesh(mesh.get(), canvas, CollisionMeshColor);
                }
            }
            if (HeightFieldScene) {
                for (const auto &mesh : HeightFieldScene->GetMeshDatas()) {
                    DrawVisibleMesh(mesh.get(), canvas,
                                    CollisionHeightFieldColor);
                }
            }
            if (DynamicRigidScene) {
                for (const auto &mesh : DynamicRigidScene->GetMeshDatas()) {
                    DrawVisibleMesh(mesh.get(), canvas, CollisionMeshColor);
                }
            }
            return;
        }

        const Vec3 start = Camera::Current.Location;
        const Vec3 end = start + Camera::Current.AxisX * 100000.0f;

        physx::PxVec3 origin(start.x, start.y, start.z);
        physx::PxVec3 target(end.x, end.y, end.z);

        if (DynamicLoadScene) {
            const auto hit = DynamicLoadScene->Raycast(origin, target);
            if (hit.Valid()) {
                DrawMesh(DynamicLoadScene->GetGeomeoryData(hit.geomID), canvas,
                         CollisionMeshColor);
            }
        }
        if (HeightFieldScene) {
            const auto hit = HeightFieldScene->Raycast(origin, target);
            if (hit.Valid()) {
                DrawMesh(HeightFieldScene->GetGeomeoryData(hit.geomID), canvas,
                         CollisionHeightFieldColor);
            }
        }
        if (DynamicRigidScene) {
            const auto hit = DynamicRigidScene->Raycast(origin, target);
            if (hit.Valid()) {
                DrawMesh(DynamicRigidScene->GetGeomeoryData(hit.geomID), canvas,
                         CollisionMeshColor);
            }
        }
    }

    void DrawObjectNames(ImDrawList *canvas) {
        if (canvas == nullptr) return;
        if (!ConfigManager::Current.ObjectName) return;
        if (!Game::IsAttached() || !Camera::IsReady()) return;
        if (dr == nullptr) return;

        struct Label {
            Vec3 World;
            std::string Name;
        };

        constexpr size_t MaxLabels = 512;

        static std::vector<Label> labels;
        static int cooldown = 0;

        if (--cooldown <= 0) {
            cooldown = 20;
            labels.clear();

            const std::vector<uint64_t> &entities = Game::Entities();
            for (size_t i = 0; i < entities.size() && labels.size() < MaxLabels;
                 ++i) {
                const uint64_t address = entities[i];
                if (!Player::IsValidPointer(address)) continue;

                const std::string *name = UObject::ClassName(dr, address);
                if (name == nullptr || name->empty()) continue;

                uint64_t root = 0;
                if (!dr->Read(address + Offset::RootComponent, root)) continue;
                if (root < Offset::MinValidPointer ||
                    root > Offset::MaxValidPointer) {
                    continue;
                }

                Vec3 world;
                if (!dr->Read(root + Offset::RootWorldLocation, world)) continue;
                if (!world.IsFinite()) continue;

                labels.push_back({world, *name});
            }
        }

        for (const Label &label : labels) {
            Vec2 screen;
            if (!Camera::WorldToScreen(label.World, screen)) continue;
            if (screen.x < 0.0f || screen.y < 0.0f) continue;
            if (screen.x > static_cast<float>(abs_ScreenX) ||
                screen.y > static_cast<float>(abs_ScreenY)) {
                continue;
            }

            canvas->AddText(ImVec2(screen.x, screen.y), ImColor(255, 255, 255, 255),
                            label.Name.c_str());
        }
    }

    void DumpBoneDebug(const Player::Entity &entity, const Player::Pose &pose) {
        static int frames = 0;
        if ((++frames % 90) != 0) return;

        __android_log_print(ANDROID_LOG_INFO, "BONE_DBG",
                            "obj=%llx mesh=%llx bones=%zu root=(%.1f,%.1f,%.1f)",
                            static_cast<unsigned long long>(entity.Address),
                            static_cast<unsigned long long>(entity.Mesh),
                            pose.Bones.size(), entity.WorldLocation.x,
                            entity.WorldLocation.y, entity.WorldLocation.z);

        for (int i = 0; i < Skeleton::KeyCount; ++i) {
            __android_log_print(ANDROID_LOG_INFO, "BONE_DBG",
                                "key[%d] bone=%d world=(%.1f,%.1f,%.1f) visible=%d",
                                i, Skeleton::KeyBones[i], pose.Points[i].x,
                                pose.Points[i].y, pose.Points[i].z,
                                pose.Visible[i] ? 1 : 0);
        }
    }
}

namespace UI {

    static void RestorePanelGeometry() {
        if (!NeedRestoreWindow) return;

        const float width =
            std::min(LastWindow.Width, static_cast<float>(displayInfo.width));
        const float height =
            std::min(LastWindow.Height, static_cast<float>(displayInfo.height));
        const float maxX =
            std::max(0.0f, static_cast<float>(displayInfo.width) - width);
        const float maxY =
            std::max(0.0f, static_cast<float>(displayInfo.height) - height);

        ImGui::SetWindowSize({width, height});
        ImGui::SetWindowPos({std::clamp(LastWindow.PosX, 0.0f, maxX),
                             std::clamp(LastWindow.PosY, 0.0f, maxY)});

        NeedRestoreWindow = false;
    }

    static void DrawRenderInfo() {
        static int themeIndex = 0;

        ImGui::Text("渲染接口 : %s, gui版本 : %s",
                    graphics ? graphics->RenderName : "无", ImGui::GetVersion());

        if (ImGui::Checkbox("过录制", &ConfigManager::Current.HideFromCapture)) {
            NeedRecreateOverlay = true;
        }

        if (ImGui::Combo("##主题", &themeIndex, "白色主题\0蓝色主题\0紫色主题\0")) {
            switch (themeIndex) {
                case 0: ImGui::StyleColorsLight(); break;
                case 1: ImGui::StyleColorsDark(); break;
                default: ImGui::StyleColorsClassic(); break;
            }
        }
    }

    static void DrawAttachSection() {
        if (ImGui::Button("初始化绘制",
                          ImVec2(ImGui::GetContentRegionAvail().x, 50))) {
            Game::Attach();
        }

        if (ImGui::Button("physx初始化",
                          ImVec2(ImGui::GetContentRegionAvail().x, 50))) {
            if (!PhysXReady) {
                if (LineTrace::initPhysX()) {
                    Physics::StartWorkerThreads();
                    PhysXReady = true;
                }
            }
        }
    }

    static uint64_t FindSkeletalMesh() {
        const std::vector<uint64_t> &entities = Game::Entities();

        for (size_t i = 0; i < entities.size(); ++i) {
            const uint64_t address = entities[i];
            if (!Player::IsValidPointer(address)) continue;

            Player::Entity entity;
            if (!Player::ReadEntity(dr, address, entity)) continue;
            if (!Player::IsCharacter(entity) || !entity.HasMesh()) continue;

            uint64_t skeletalMesh = 0;
            if (dr->Read(entity.Mesh + Offset::MeshSkeletalMesh, skeletalMesh) &&
                skeletalMesh != 0) {
                return skeletalMesh;
            }
        }
        return 0;
    }

    static void ResolveAndApply() {
        const uint64_t skeletalMesh = FindSkeletalMesh();

        if (skeletalMesh == 0) {
            snprintf(Skeleton::NameStatus, sizeof(Skeleton::NameStatus),
                     "没找到角色骨架 (需要先进对局)");
            return;
        }

        const int hits = Skeleton::ResolveKeyBones(dr, skeletalMesh);
        snprintf(Skeleton::NameStatus, sizeof(Skeleton::NameStatus),
                 "命中 %d/%d   骨架 %llx", hits, Skeleton::KeyCount,
                 static_cast<unsigned long long>(skeletalMesh));
        if (hits > 0) {
            ConfigManager::Current.BoneKeySource = 1;
            Skeleton::ApplyKeyBoneSource(1);
        }
    }

    static void DrawBoneResolveSection() {
        ImGui::SeparatorText("骨架");

        if (ImGui::Button("按名字解析骨骼索引",
                          ImVec2(ImGui::GetContentRegionAvail().x, 50))) {
            ResolveAndApply();
        }

        ImGui::TextWrapped("%s", Skeleton::NameStatus);

        if (ImGui::TreeNode("骨骼索引 (生效/内置/解析)")) {
            for (int k = 0; k < Skeleton::KeyCount; ++k) {
                ImGui::Text("%-22s %3d | %3d | %3d", Skeleton::KeyBoneNames[k],
                            Skeleton::KeyBones[k],
                            Skeleton::BuiltinKeyBones[k],
                            Skeleton::ResolvedKeyBones[k]);
            }
            ImGui::TreePop();
        }
    }

    static void DrawCollisionSection() {
        Config &settings = ConfigManager::Current;

        ImGui::SeparatorText("碰撞体");

        ImGui::Checkbox("绘制碰撞体", &settings.Collision);

        if (ImGui::Button("全部网格")) settings.CollisionMode = 0;
        ImGui::SameLine();
        if (ImGui::Button("准星命中")) settings.CollisionMode = 1;
        ImGui::SameLine();
        ImGui::Text("当前: %s",
                    settings.CollisionMode == 0 ? "全部网格" : "准星命中");
    }

    static void DrawStatusSection() {
        ImGui::SeparatorText("状态");

        ImGui::Text("PID: %d", dr ? dr->Pid() : -1);
        ImGui::Text("模块基址: 0x%llx",
                    static_cast<unsigned long long>(Game::Current.Module));
        ImGui::Text("名字池: 0x%llx", static_cast<unsigned long long>(Gname));
        ImGui::Text("世界: 0x%llx", static_cast<unsigned long long>(Uworld));
        ImGui::Text("关卡: 0x%llx", static_cast<unsigned long long>(Uleve));
        ImGui::Text("实体数组: 0x%llx",
                    static_cast<unsigned long long>(Arrayaddr));
        ImGui::Text("实体数量: %d", Count);
        ImGui::Text("玩家控制器: 0x%llx",
                    static_cast<unsigned long long>(PlayerController));
        ImGui::Text("自身角色: 0x%llx", static_cast<unsigned long long>(MySelf));
        ImGui::Text("相机管理器: 0x%llx",
                    static_cast<unsigned long long>(CameraManagerPtr));
        ImGui::Text("相机位置: X %.2f  Y %.2f  Z %.2f", CameraLocation.x,
                    CameraLocation.y, CameraLocation.z);
        ImGui::Text("相机旋转: P %.2f  Y %.2f  R %.2f", CameraRotation.x,
                    CameraRotation.y, CameraRotation.z);
        ImGui::Text("视场角: %.2f", CameraFov);

        ImGui::Text("PhysX 静态网格: %zu",
                    DynamicLoadScene ? DynamicLoadScene->MeshCount() : 0);
        ImGui::Text("PhysX 地形网格: %zu",
                    HeightFieldScene ? HeightFieldScene->MeshCount() : 0);
        ImGui::Text("PhysX 动态网格: %zu",
                    DynamicRigidScene ? DynamicRigidScene->MeshCount() : 0);

        if (Camera::IsReady()) {
            const bool hit = ESP::IsOccluded(
                Camera::Current.Location,
                Camera::Current.Location + Camera::Current.AxisX * 5000.0f);
            ImGui::Text("准星射线: %s", hit ? "命中掩体" : "空");
        } else {
            ImGui::Text("准星射线: 投影未就绪");
        }

        ImGui::Text("最近敌人: %s", NearestProbe.Valid ? "有" : "无");
        if (NearestProbe.Valid) {
            if (NearestProbe.TotalBones > 0) {
                ImGui::Text("骨骼 %d/%d 可见    %s",
                            NearestProbe.VisibleBones, NearestProbe.TotalBones,
                            NearestProbe.Occluded ? "全被挡" : "有露出");
            } else {
                ImGui::Text("骨骼: 未做遮挡判定 (physx 未初始化)");
            }
        }
    }

    void Layout_tick_UI(bool *main_thread_flag) {
        if (!ImGui::Begin("和平精英", nullptr,
                          ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::End();
            return;
        }

        ImGui::StyleColorsDark();
        ImGuiStyle &style = ImGui::GetStyle();
        style.WindowBorderSize = 0.0f;
        style.Colors[ImGuiCol_Border] = ImVec4(0, 0, 0, 0);
        style.WindowRounding = 4.0f;
        style.FrameRounding = 4.0f;
        style.ChildRounding = 4.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
        ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 6.0f);

        ImGui::Text("当前帧率: %.1f FPS   刷新率: %.1f Hz",
                    ImGui::GetIO().Framerate, Platform::DisplayRefreshRate());
        ImGui::Separator();

        static int selected_tab = 0;
        const char *tab_names[] = {"主页", "绘制", "调节", "D bug"};
        for (int i = 0; i < IM_ARRAYSIZE(tab_names); ++i) {
            if (ImGui::Button(tab_names[i])) selected_tab = i;
            if (i < IM_ARRAYSIZE(tab_names) - 1) ImGui::SameLine();
        }
        ImGui::Separator();

        RestorePanelGeometry();

        Config &settings = ConfigManager::Current;

        switch (selected_tab) {
            case 0: {
                DrawRenderInfo();

                ImGui::Separator();

                ImGui::SliderInt("界面帧率", &settings.GuiFps, 60, 185, "%d FPS");

                ImGui::SliderInt("运动补偿", &settings.MotionLeadMs, 0, 40, "%d ms");

                ImGui::Separator();

                DrawAttachSection();

                ImGui::Separator();

                if (ImGui::Button("保存设置",
                                  ImVec2(ImGui::GetContentRegionAvail().x, 40))) {
                    ConfigManager::Save();
                }
            } break;

            case 1: {
                ImGui::SeparatorText("绘制");

                ImGui::Checkbox("方框", &settings.Box);
                ImGui::SameLine(0, 40);
                ImGui::Checkbox("射线", &settings.Snapline);

                ImGui::Checkbox("人数", &settings.PlayerCount);
                ImGui::SameLine(0, 40);
                ImGui::Checkbox("骨骼", &settings.Skeleton);

                ImGui::Checkbox("胶囊体", &settings.CapsuleSize);
                ImGui::SameLine(0, 40);
                ImGui::Checkbox("距离", &settings.Distance);

                ImGui::Checkbox("类名", &settings.ObjectName);
            } break;

            case 2: {
                ImGui::SeparatorText("骨骼索引来源");

                if (ImGui::RadioButton("内置", &settings.BoneKeySource, 0)) {
                    Skeleton::ApplyKeyBoneSource(0);
                }
                ImGui::SameLine(0, 30);
                if (ImGui::RadioButton("名称", &settings.BoneKeySource, 1)) {
                    if (Skeleton::NameResolved) {
                        Skeleton::ApplyKeyBoneSource(1);
                    } else {
                        ResolveAndApply();
                    }
                }

                if (settings.BoneKeySource == 1 && !Skeleton::NameResolved) {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                                       "尚未解析, 请点下面的按钮");
                }

                ImGui::Separator();

                DrawBoneResolveSection();
            } break;

            case 3: {
                ImGui::SeparatorText("骨骼标签");

                ImGui::Checkbox("名称", &settings.BoneName);
                ImGui::SameLine(0, 40);
                ImGui::Checkbox("索引", &settings.BoneIndex);

                ImGui::Separator();

                DrawCollisionSection();

                ImGui::Separator();

                DrawStatusSection();
            } break;
        }

        ImGui::Separator();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        if (ImGui::Button("退出")) {
            if (main_thread_flag) *main_thread_flag = false;
        }
        ImGui::PopStyleColor(2);

        ImGui::PopStyleVar(4);
        g_window = ImGui::GetCurrentWindow();
        ImGui::End();
    }
}
