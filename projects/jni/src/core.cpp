#include "camera.h"
#include "bone.h"
#include "player.h"
#include "game.h"
#include "config.h"
#include "platform.h"
#include "globals.h"
#include "offsets.h"
#include "memory.h"

#include "TouchHelperA.h"
#include "embedded_assets.h"
#include "imgui_internal.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <thread>
#include <unistd.h>
#include <fcntl.h>

namespace Camera {

    Projection Current;

    struct PoseSnapshot {
        Vec3 Location;
        Vec3 Rotation;
        float Fov;
    };

    static_assert(sizeof(PoseSnapshot) == 0x1C, "camera pose must be packed");
    static_assert(offsetof(PoseSnapshot, Rotation) == 0x0C, "rotation offset");
    static_assert(offsetof(PoseSnapshot, Fov) == 0x18, "fov offset");

    Matrix RotationToMatrix(const Rotator &rotation) {
        const float pitch = Angle::ToRadians(rotation.Pitch);
        const float yaw = Angle::ToRadians(rotation.Yaw);
        const float roll = Angle::ToRadians(rotation.Roll);

        const float SP = sinf(pitch), CP = cosf(pitch);
        const float SY = sinf(yaw), CY = cosf(yaw);
        const float SR = sinf(roll), CR = cosf(roll);

        Matrix m;

        m.M[0][0] = CP * CY;
        m.M[0][1] = CP * SY;
        m.M[0][2] = SP;
        m.M[0][3] = 0.0f;

        m.M[1][0] = SR * SP * CY - CR * SY;
        m.M[1][1] = SR * SP * SY + CR * CY;
        m.M[1][2] = -SR * CP;
        m.M[1][3] = 0.0f;

        m.M[2][0] = -(CR * SP * CY + SR * SY);
        m.M[2][1] = CY * SR - CR * SP * SY;
        m.M[2][2] = CR * CP;
        m.M[2][3] = 0.0f;

        m.M[3][0] = 0.0f;
        m.M[3][1] = 0.0f;
        m.M[3][2] = 0.0f;
        m.M[3][3] = 1.0f;

        return m;
    }

    static float AngleDeltaShortest(float current, float previous) {
        float delta = std::fmod(current - previous, 360.0f);
        if (delta > 180.0f) delta -= 360.0f;
        if (delta < -180.0f) delta += 360.0f;
        return delta;
    }

    void Setup(const ViewInfo &info, float screenWidth, float screenHeight) {
        static ViewInfo previous{};
        static std::chrono::steady_clock::time_point previousTime{};
        static bool hasPrevious = false;

        if (!info.IsValid() || screenWidth <= 0.0f || screenHeight <= 0.0f) {
            hasPrevious = false;
            Current.Ready = false;
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const int leadMs = std::clamp(ConfigManager::Current.MotionLeadMs, 0, 40);

        ViewInfo view = info;

        if (leadMs > 0 && hasPrevious) {
            const double elapsed =
                std::chrono::duration<double>(now - previousTime).count();

            if (elapsed > 0.001 && elapsed < 0.1) {
                const float lead = static_cast<float>(leadMs) / 1000.0f;
                const float inverse = 1.0f / static_cast<float>(elapsed);

                const Vec3 linear = (info.Location - previous.Location) * inverse;
                view.Location = info.Location + linear * lead;

                view.Rotation.Pitch += AngleDeltaShortest(
                                           info.Rotation.Pitch,
                                           previous.Rotation.Pitch) *
                                       inverse * lead;
                view.Rotation.Yaw += AngleDeltaShortest(
                                         info.Rotation.Yaw,
                                         previous.Rotation.Yaw) *
                                     inverse * lead;
                view.Rotation.Roll += AngleDeltaShortest(
                                          info.Rotation.Roll,
                                          previous.Rotation.Roll) *
                                      inverse * lead;

                if (view.Rotation.Pitch > 89.0f) view.Rotation.Pitch = 89.0f;
                if (view.Rotation.Pitch < -89.0f) view.Rotation.Pitch = -89.0f;

                view.Fov += (info.Fov - previous.Fov) * inverse * lead;
                view.Fov = std::clamp(view.Fov, 1.0f, 179.0f);
            }
        }

        previous = info;
        previousTime = now;
        hasPrevious = true;

        const Matrix m = RotationToMatrix(view.Rotation);

        Current.AxisX = Vec3(m.M[0][0], m.M[0][1], m.M[0][2]);
        Current.AxisY = Vec3(m.M[1][0], m.M[1][1], m.M[1][2]);
        Current.AxisZ = Vec3(m.M[2][0], m.M[2][1], m.M[2][2]);

        Current.Location = view.Location;
        Current.HalfWidth = screenWidth * 0.5f;
        Current.HalfHeight = screenHeight * 0.5f;

        const float halfFov = tanf(view.Fov * (PI / 360.0f));
        if (!(halfFov > 0.0f) || !std::isfinite(halfFov)) {
            Current.Ready = false;
            return;
        }

        Current.Scale = Current.HalfWidth / halfFov;
        Current.Ready = std::isfinite(Current.Scale) && Current.Scale > 0.0f;
    }
}

Matrix TransformToMatrix(const BoneTransform &transform) {
    Matrix m;

    m.M[3][0] = transform.Translation.x;
    m.M[3][1] = transform.Translation.y;
    m.M[3][2] = transform.Translation.z;

    const float x2 = transform.Rotation.x + transform.Rotation.x;
    const float y2 = transform.Rotation.y + transform.Rotation.y;
    const float z2 = transform.Rotation.z + transform.Rotation.z;

    const float xx2 = transform.Rotation.x * x2;
    const float yy2 = transform.Rotation.y * y2;
    const float zz2 = transform.Rotation.z * z2;

    m.M[0][0] = (1.0f - (yy2 + zz2)) * transform.Scale3D.x;
    m.M[1][1] = (1.0f - (xx2 + zz2)) * transform.Scale3D.y;
    m.M[2][2] = (1.0f - (xx2 + yy2)) * transform.Scale3D.z;

    const float yz2 = transform.Rotation.y * z2;
    const float wx2 = transform.Rotation.w * x2;
    m.M[2][1] = (yz2 - wx2) * transform.Scale3D.z;
    m.M[1][2] = (yz2 + wx2) * transform.Scale3D.y;

    const float xy2 = transform.Rotation.x * y2;
    const float wz2 = transform.Rotation.w * z2;
    m.M[1][0] = (xy2 - wz2) * transform.Scale3D.y;
    m.M[0][1] = (xy2 + wz2) * transform.Scale3D.x;

    const float xz2 = transform.Rotation.x * z2;
    const float wy2 = transform.Rotation.w * y2;
    m.M[2][0] = (xz2 + wy2) * transform.Scale3D.z;
    m.M[0][2] = (xz2 - wy2) * transform.Scale3D.x;

    m.M[0][3] = 0.0f;
    m.M[1][3] = 0.0f;
    m.M[2][3] = 0.0f;
    m.M[3][3] = 1.0f;

    return m;
}

Matrix MultiplyMatrix(const Matrix &left, const Matrix &right) {
    Matrix result;

    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += left.M[row][k] * right.M[k][col];
            }
            result.M[row][col] = sum;
        }
    }

    return result;
}

Vec3 MatrixPosition(const Matrix &matrix) {
    return Vec3(matrix.M[3][0], matrix.M[3][1], matrix.M[3][2]);
}

BoneTransform ReadBone(Reader *reader, uint64_t arrayBase, int index) {
    BoneTransform result;

    if (reader == nullptr || arrayBase == 0 || index < 0) return result;

    const uint64_t address =
        arrayBase + static_cast<uint64_t>(index) * Offset::BoneStride;
    reader->Read(address, result);
    return result;
}

Vec3 BoneWorldPosition(Reader *reader, uint64_t arrayBase, int index,
                       const Matrix &componentMatrix) {
    const BoneTransform bone = ReadBone(reader, arrayBase, index);
    return MatrixPosition(MultiplyMatrix(TransformToMatrix(bone), componentMatrix));
}

size_t ReadBoneRange(Reader *reader, uint64_t arrayBase, int firstIndex,
                     int count, std::vector<BoneTransform> &out) {
    out.clear();

    if (reader == nullptr || arrayBase == 0 || firstIndex < 0 || count <= 0) {
        return 0;
    }

    const uint64_t firstAddress =
        arrayBase + static_cast<uint64_t>(firstIndex) * Offset::BoneStride;

    const size_t stride = static_cast<size_t>(Offset::BoneStride);
    const size_t total = stride * static_cast<size_t>(count);

    static thread_local std::vector<uint8_t> raw;
    raw.resize(total);
    if (!reader->Read(firstAddress, raw.data(), total)) return 0;

    out.resize(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        std::memcpy(&out[static_cast<size_t>(i)],
                    raw.data() + static_cast<size_t>(i) * stride,
                    sizeof(BoneTransform));
    }

    return static_cast<size_t>(count);
}

Vec3 BoneLocalToWorld(const std::vector<BoneTransform> &bones, int index,
                      const Matrix &componentMatrix) {
    if (index < 0 || static_cast<size_t>(index) >= bones.size()) {
        return Vec3::Zero();
    }

    return MatrixPosition(
        MultiplyMatrix(TransformToMatrix(bones[static_cast<size_t>(index)]),
                       componentMatrix));
}

namespace Player {

    bool IsValidPointer(uint64_t address) {
        return address > Offset::MinValidPointer &&
               address < Offset::MaxValidPointer &&
               (address % 4) == 0;
    }

    bool ReadEntity(Reader *reader, uint64_t address, Entity &out) {
        out = Entity{};

        if (reader == nullptr || !IsValidPointer(address)) return false;

        uint64_t root = 0;
        if (!reader->Read(address + Offset::RootComponent, root) ||
            !IsValidPointer(root)) {
            return false;
        }

        uint64_t capsule = 0;
        if (!reader->Read(address + Offset::CapsuleComponent, capsule) ||
            !IsValidPointer(capsule)) {
            return false;
        }

        float capsuleRadius = 0.0f;
        if (!reader->Read(capsule + Offset::CapsuleRadius, capsuleRadius)) {
            return false;
        }

        Vec3 worldLocation;
        if (!reader->Read(root + Offset::RootWorldLocation, worldLocation)) {
            return false;
        }
        if (!worldLocation.IsFinite()) return false;

        uint64_t mesh = 0;
        if (reader->Read(address + Offset::Mesh, mesh)) {
            out.Mesh = IsValidPointer(mesh) ? mesh : 0;
        }

        out.Address = address;
        out.RootComponent = root;
        out.CapsuleComponent = capsule;
        out.CapsuleRadius = capsuleRadius;
        out.WorldLocation = worldLocation;
        return true;
    }

    bool IsCharacter(const Entity &entity) {
        const float delta =
            std::fabs(entity.CapsuleRadius - Offset::CharacterRadius);
        return delta <= Offset::RadiusTolerance;
    }

    bool ReadPose(Reader *reader, const Entity &entity, const Vec3 &boneOffset,
                  bool needVisibility, VisibilityFn visibilityFn, Pose &out) {
        out = Pose{};

        if (reader == nullptr || !entity.HasMesh()) return false;

        BoneTransform componentTransform;
        if (!reader->Read(entity.Mesh + Offset::ComponentToWorld,
                          componentTransform)) {
            return false;
        }

        uint64_t arrayBase = 0;
        if (!reader->Read(entity.Mesh + Offset::BoneArray, arrayBase) ||
            !IsValidPointer(arrayBase)) {
            return false;
        }

        uint64_t skeletalMesh = 0;
        reader->Read(entity.Mesh + Offset::MeshSkeletalMesh, skeletalMesh);

        int boneCount = Skeleton::BoneCount(reader, skeletalMesh);
        if (boneCount <= 0 || boneCount > Skeleton::MaxBoneCount) {
            boneCount = static_cast<int>(RAnkle) + 1;
        }

        const int critical = static_cast<int>(RAnkle) + 1;
        const int first = (boneCount > critical) ? critical : boneCount;

        if (ReadBoneRange(reader, arrayBase, 0, first, out.Bones) == 0) {
            return false;
        }

        if (boneCount > first) {
            std::vector<BoneTransform> tail;
            if (ReadBoneRange(reader, arrayBase, first, boneCount - first, tail) ==
                static_cast<size_t>(boneCount - first)) {
                out.Bones.insert(out.Bones.end(), tail.begin(), tail.end());
            }
        }

        const Matrix componentMatrix = TransformToMatrix(componentTransform);
        out.ComponentMatrix = componentMatrix;

        for (int i = 0; i < Skeleton::KeyCount; ++i) {
            Vec3 world =
                BoneLocalToWorld(out.Bones, Skeleton::KeyBones[i], componentMatrix);
            world += boneOffset;

            out.Points[i] = world;
            out.Visible[i] =
                !needVisibility || visibilityFn == nullptr || visibilityFn(world);
        }

        out.Valid = true;
        return true;
    }
}

namespace Game {

    Context Current;

    static std::vector<uint64_t> entityList;

    static constexpr int MaxEntities = 4096;

    const std::vector<uint64_t> &Entities() {
        return entityList;
    }

    static void RefreshEntities() {
        entityList.clear();

        if (!IsAttached() || Arrayaddr == 0 || Count <= 0) return;

        const int count = std::min(Count, MaxEntities);
        const uint64_t first = static_cast<uint64_t>(Arrayaddr);

        entityList.resize(static_cast<size_t>(count));

        const size_t hit =
            dr->ReadArray(first, entityList.data(), entityList.size());
        if (hit == entityList.size()) return;

        entityList.assign(static_cast<size_t>(count), 0);
        for (int i = 0; i < count; ++i) {
            entityList[static_cast<size_t>(i)] = dr->Read<uint64_t>(
                first + sizeof(uint64_t) * static_cast<uint64_t>(i));
        }
    }

    bool Attach(const char *package) {
        if (dr == nullptr) {
            dr = new Reader();
            if (dr == nullptr) return false;
        }

        const char *target = (package != nullptr) ? package : Offset::ProcessName;

        const int pid = dr->Attach(target);
        if (pid <= 0) return false;

        uint64_t moduleBase = 0;
        if (!dr->GetModuleBase(Offset::ModuleName, 0, &moduleBase, true)) {
            return false;
        }
        if (moduleBase < Offset::MinModuleBase) return false;

        libUE4 = static_cast<long int>(moduleBase);
        Gname = static_cast<long int>(moduleBase + Offset::GNamesSlot);
        Current.Module = moduleBase;
        FName::Init(moduleBase);
        Initialized = true;
        return true;
    }

    bool IsAttached() {
        return Initialized && dr != nullptr && dr->IsReady() &&
               Current.Module > Offset::MinModuleBase;
    }

    void Update() {
        if (!IsAttached()) return;

        Reader *reader = dr;

        uint64_t world = 0;
        if (!reader->Read(Current.Module + Offset::Gworld, world) || world == 0) {
            return;
        }
        Uworld = static_cast<long int>(world);
        Current.World = world;

        const uintptr_t level = Offset::ReadChain(reader, world, Offset::Uleve);
        if (level == 0) return;
        Uleve = static_cast<long int>(level);

        uint64_t entityArray = 0;
        if (reader->Read(level + Offset::EntityArray, entityArray)) {
            Arrayaddr = static_cast<long int>(entityArray);
        }

        int entityCount = 0;
        if (reader->Read(level + Offset::EntityCount, entityCount)) {
            Count = entityCount;
        }

        RefreshEntities();

        const uintptr_t controller =
            Offset::ReadChain(reader, world, Offset::NetDriver);
        if (controller == 0) return;

        PlayerController = static_cast<long int>(controller);
        Current.PlayerController = controller;

        uint64_t self = 0;
        if (reader->Read(controller + Offset::Pawn, self) && self != 0) {
            MySelf = static_cast<long int>(self);
            Current.Self = self;
        }

        uint64_t cameraManager = 0;
        if (!reader->Read(controller + Offset::CameraManager, cameraManager) ||
            cameraManager == 0) {
            return;
        }

        CameraManagerPtr = static_cast<long int>(cameraManager);
        Current.CameraManager = cameraManager;

        Camera::PoseSnapshot pose{};
        if (reader->Read(cameraManager + Offset::CameraLocation, pose)) {
            if (pose.Location.IsFinite()) {
                CameraLocation = pose.Location;
                Current.CameraLocation = pose.Location;
            }

            if (pose.Rotation.IsFinite()) {
                CameraRotation = pose.Rotation;
                Current.CameraRotation = Rotator{pose.Rotation.x, pose.Rotation.y,
                                                 pose.Rotation.z};
            }

            if (std::isfinite(pose.Fov) && pose.Fov > 0.0f) {
                CameraFov = pose.Fov;
                Current.Fov = pose.Fov;
            }
        }

        Current.CameraCache = cameraManager + Offset::CameraLocation;
    }
}

namespace ConfigManager {

    Config Current;

    static constexpr uint8_t Key[] = {
        0x8B, 0x8F, 0xD3, 0xDF, 0x7B, 0xF7, 0x0D, 0x9F
    };
    static constexpr size_t KeyLength = sizeof(Key);

    static constexpr const char *ConfigPath = "/data/Config";

    static void Obfuscate(uint8_t *data, size_t length) {
        for (size_t i = 0; i < length; ++i) {
            data[i] ^= Key[i % KeyLength];
        }
    }

    bool Load() {
        const int fd = open(ConfigPath, O_RDONLY);
        if (fd < 0) return false;

        Config loaded;
        const ssize_t n = read(fd, &loaded, sizeof(loaded));
        close(fd);

        if (n != static_cast<ssize_t>(sizeof(loaded))) return false;

        Obfuscate(reinterpret_cast<uint8_t *>(&loaded), sizeof(loaded));

        if (loaded.Magic != ConfigMagic) return false;
        if (loaded.Version != ConfigVersion) return false;
        if (loaded.StructSize != sizeof(Config)) return false;

        Current = loaded;
        return true;
    }

    bool Save() {
        Config out = Current;
        out.Magic = ConfigMagic;
        out.Version = ConfigVersion;
        out.StructSize = sizeof(Config);

        Obfuscate(reinterpret_cast<uint8_t *>(&out), sizeof(out));

        const int fd = open(ConfigPath, O_WRONLY | O_CREAT | O_TRUNC, 0660);
        if (fd < 0) return false;

        const ssize_t n = write(fd, &out, sizeof(out));
        close(fd);
        return n == static_cast<ssize_t>(sizeof(out));
    }
}

namespace Platform {

    static double g_refreshPeriod = 0.0;

    static bool LoadFont(float size) {
        ImGuiIO &io = ImGui::GetIO();

        ImFontConfig fontConfig;
        fontConfig.FontDataOwnedByAtlas = false;
        fontConfig.SizePixels = size;
        fontConfig.OversampleH = 2;
        fontConfig.OversampleV = 1;

        zh_font = io.Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char *>(g_heiti_ttf_data),
            static_cast<int>(EmbeddedAssets::HeitiSize()), size, &fontConfig,
            io.Fonts->GetGlyphRangesChineseFull());

        if (zh_font == nullptr) return false;

        io.FontDefault = zh_font;
        return true;
    }

    void InitRenderEnv() {
        ImGui::StyleColorsLight();
        LoadFont(25.0f);
        ImGui::GetStyle().ScaleAllSizes(3.0f);
    }

    void RefreshDisplay() {
        static std::chrono::steady_clock::time_point last{};
        const auto now = std::chrono::steady_clock::now();
        if (now - last < std::chrono::milliseconds(250)) return;

        const auto info = android::ANativeWindowCreator::GetDisplayInfo();
        if (info.width > 0 && info.height > 0) {
            displayInfo = info;
        }

        // 过录制开关交给 ANativeWindowCreator 内部处理：hide/show 镜像层，不再销毁重建
        android::ANativeWindowCreator::SetMirrorEnabled(!ConfigManager::Current.HideFromCapture);
        android::ANativeWindowCreator::ProcessMirrorDisplay();

        last = now;
    }

    static double RefreshPeriod() {
        using Clock = std::chrono::steady_clock;

        static Clock::time_point last{};
        static int samples = 0;

        const auto now = Clock::now();
        if (last != Clock::time_point{}) {
            const double dt = std::chrono::duration<double>(now - last).count();
            if (dt > 0.002 && dt < 0.1) {
                if (g_refreshPeriod <= 0.0 || dt < g_refreshPeriod) {
                    g_refreshPeriod = dt;
                }
            }
        }
        last = now;

        if (++samples >= 600) {
            samples = 0;
            g_refreshPeriod = 0.0;
        }

        return g_refreshPeriod > 0.0 ? g_refreshPeriod : (1.0 / 60.0);
    }

    float DisplayRefreshRate() {
        return g_refreshPeriod > 0.0 ? static_cast<float>(1.0 / g_refreshPeriod)
                                     : 0.0f;
    }

    void LimitFrameRate() {
        using Clock = std::chrono::steady_clock;

        const double refreshPeriod = RefreshPeriod();
        const int targetFps = std::clamp(ConfigManager::Current.GuiFps, 30, 240);

        const double want = 1.0 / static_cast<double>(targetFps);
        const int step = std::max(
            1, static_cast<int>(std::lround(want / refreshPeriod)));

        const auto period = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(static_cast<double>(step) *
                                          refreshPeriod));

        static Clock::time_point anchor{};

        const auto now = Clock::now();
        if (anchor == Clock::time_point{}) anchor = now;

        const auto earliest = anchor + period;
        if (now < earliest) {
            std::this_thread::sleep_until(earliest);
        }

        anchor = Clock::now();
    }

    void SyncRotation() {
        static int32_t last = -1;
        if (last == displayInfo.orientation) return;

        last = displayInfo.orientation;
        Touch::setOrientation(displayInfo.orientation);
    }

    void RecreateOverlay() {
        if (g_window != nullptr) {
            LastWindow.PosX = g_window->Pos.x;
            LastWindow.PosY = g_window->Pos.y;
            LastWindow.Width = g_window->Size.x;
            LastWindow.Height = g_window->Size.y;
        }

        const bool sizeChanged =
            displayInfo.width > 0 && displayInfo.height > 0 &&
            (native_window_screen_x != displayInfo.width ||
             native_window_screen_y != displayInfo.height);

        if (sizeChanged) {
            native_window_screen_x = displayInfo.width;
            native_window_screen_y = displayInfo.height;
            abs_ScreenX = displayInfo.width;
            abs_ScreenY = displayInfo.height;
        }

        if (graphics) graphics->Shutdown();
        if (window != nullptr) android::ANativeWindowCreator::Destroy(window);

        window = android::ANativeWindowCreator::Create(
            "test_sysGui", native_window_screen_x, native_window_screen_y,
            ConfigManager::Current.HideFromCapture);

        if (graphics) {
            graphics->Init_Render(window, native_window_screen_x,
                                  native_window_screen_y);
        }

        InitRenderEnv();
        g_window = nullptr;
        NeedRestoreWindow = true;

        Touch::UpdateDisplaySize({static_cast<float>(native_window_screen_x),
                                  static_cast<float>(native_window_screen_y)});
    }

    void UpdateScreenCenter() {
        px = static_cast<float>(abs_ScreenX) * 0.5f;
        py = static_cast<float>(abs_ScreenY) * 0.5f;
    }

    void BeginFrame() {
        RefreshDisplay();

        const bool sizeChanged =
            displayInfo.width > 0 && displayInfo.height > 0 &&
            (native_window_screen_x != displayInfo.width ||
             native_window_screen_y != displayInfo.height);

        if (NeedRecreateOverlay || sizeChanged) {
            RecreateOverlay();
        }

        SyncRotation();
        UpdateScreenCenter();
    }
}
