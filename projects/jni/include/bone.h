#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "types.h"
#include "memory.h"
#include "offsets.h"
#include "names.h"

enum BoneIndex : int {
    Pelvis = 2,
    Chest = 5,
    Head = 7,
    LShoulder = 8,
    LElbow = 10,
    LWrist = 11,
    RShoulder = 29,
    RElbow = 31,
    RWrist = 32,
    LThigh = 49,
    LKnee = 50,
    LAnkle = 51,
    RThigh = 53,
    RKnee = 54,
    RAnkle = 55,
};

struct BoneTransform {
    Quat Rotation;
    Vec3 Translation;
    Vec3 Scale3D;
};

namespace Skeleton {
    constexpr int KeyCount = 15;
    constexpr int LinkCount = 14;
    constexpr int MaxBoneCount = 180;

    inline constexpr int BuiltinKeyBones[KeyCount] = {
        Head, Chest, Pelvis,
        LShoulder, LElbow, LWrist,
        RShoulder, RElbow, RWrist,
        LThigh, LKnee, LAnkle,
        RThigh, RKnee, RAnkle
    };

    inline int ResolvedKeyBones[KeyCount] = {
        Head, Chest, Pelvis,
        LShoulder, LElbow, LWrist,
        RShoulder, RElbow, RWrist,
        LThigh, LKnee, LAnkle,
        RThigh, RKnee, RAnkle
    };

    inline int KeyBones[KeyCount] = {
        Head, Chest, Pelvis,
        LShoulder, LElbow, LWrist,
        RShoulder, RElbow, RWrist,
        LThigh, LKnee, LAnkle,
        RThigh, RKnee, RAnkle
    };

    inline constexpr const char *KeyBoneNames[KeyCount] = {
        "Bip001-Head",
        "Bip001-Spine2",
        "Bip001-Pelvis",
        "Bip001-L-Clavicle",
        "Bip001-L-Forearm",
        "Bip001-L-Hand",
        "Bip001-R-Clavicle",
        "Bip001-R-Forearm",
        "Bip001-R-Hand",
        "Bip001-L-Thigh",
        "Bip001-L-Calf",
        "Bip001-L-Foot",
        "Bip001-R-Thigh",
        "Bip001-R-Calf",
        "Bip001-R-Foot",
    };

    inline bool NameResolved = false;
    inline int  NameHitCount = 0;
    inline char NameStatus[192] = "未解析";

    inline void ApplyKeyBoneSource(int source) {
        const int *src = (source == 1 && NameResolved) ? ResolvedKeyBones
                                                       : BuiltinKeyBones;
        for (int k = 0; k < KeyCount; ++k) {
            KeyBones[k] = src[k];
        }
    }

    struct BoneNameEntry {
        int32_t BoneID;
        int32_t Index;
        int32_t ParentIndex;
    };

    inline uint64_t CachedCountMesh = 0;
    inline int CachedCountValue = 0;

    inline int BoneCount(Reader *reader, uint64_t skeletalMesh) {
        if (reader == nullptr || skeletalMesh == 0) return 0;
        if (CachedCountMesh == skeletalMesh && CachedCountValue > 0) {
            return CachedCountValue;
        }

        int32_t count = 0;
        if (!reader->Read(skeletalMesh + Offset::SkeletonBoneInfo + 8, count)) return 0;
        if (count <= 0 || count > 1024) return 0;

        CachedCountMesh = skeletalMesh;
        CachedCountValue = count;
        return count;
    }

    inline int ResolveKeyBones(Reader *reader, uint64_t skeletalMesh) {
        if (reader == nullptr || skeletalMesh == 0) return 0;

        uint64_t array = 0;
        int32_t count = 0;
        if (!reader->Read(skeletalMesh + Offset::SkeletonBoneInfo, array)) return 0;
        if (!reader->Read(skeletalMesh + Offset::SkeletonBoneInfo + 8, count)) return 0;
        if (array < Offset::MinValidPointer || array > Offset::MaxValidPointer) return 0;
        if (count <= 0 || count > 1024) return 0;

        std::vector<BoneNameEntry> entries(static_cast<size_t>(count));
        if (!reader->Read(array, entries.data(), entries.size() * sizeof(BoneNameEntry))) {
            return 0;
        }

        std::string name;
        int hits = 0;
        for (int32_t i = 0; i < count; ++i) {
            const uint32_t id = static_cast<uint32_t>(entries[static_cast<size_t>(i)].BoneID);
            if (!FName::Resolve(reader, id, name)) continue;

            for (int k = 0; k < KeyCount; ++k) {
                if (FName::CompareNoCase(name.c_str(), KeyBoneNames[k])) {
                    ResolvedKeyBones[k] = i;
                    ++hits;
                    break;
                }
            }
        }

        NameHitCount = hits;
        NameResolved = hits > 0;
        return hits;
    }

    inline std::map<uint64_t, std::vector<std::string>> CachedNameTables;

    inline const std::vector<std::string> *GetBoneNames(Reader *reader,
                                                        uint64_t skeletalMesh) {
        if (reader == nullptr || skeletalMesh == 0) return nullptr;

        auto cached = CachedNameTables.find(skeletalMesh);
        if (cached != CachedNameTables.end()) return &cached->second;
        if (CachedNameTables.size() >= 32) CachedNameTables.clear();

        uint64_t array = 0;
        int32_t count = 0;
        if (!reader->Read(skeletalMesh + Offset::SkeletonBoneInfo, array)) return nullptr;
        if (!reader->Read(skeletalMesh + Offset::SkeletonBoneInfo + 8, count)) return nullptr;
        if (array < Offset::MinValidPointer || array > Offset::MaxValidPointer) return nullptr;
        if (count <= 0 || count > 1024) return nullptr;

        std::vector<BoneNameEntry> entries(static_cast<size_t>(count));
        if (!reader->Read(array, entries.data(), entries.size() * sizeof(BoneNameEntry))) {
            return nullptr;
        }

        std::vector<std::string> names(static_cast<size_t>(count));
        std::string name;
        for (int32_t i = 0; i < count; ++i) {
            const uint32_t id = static_cast<uint32_t>(entries[static_cast<size_t>(i)].BoneID);
            names[static_cast<size_t>(i)] =
                FName::Resolve(reader, id, name) ? name : std::string("?");
        }

        auto &slot = CachedNameTables[skeletalMesh];
        slot.swap(names);
        return &slot;
    }

    inline void ClearBoneNameCache() {
        CachedNameTables.clear();
    }

    inline constexpr int LinkA[LinkCount] = {1, 1, 1, 3, 4, 1, 6, 7, 2, 9, 10, 2, 12, 13};

    inline constexpr int LinkB[LinkCount] = {0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
}

Matrix TransformToMatrix(const BoneTransform &transform);

Matrix MultiplyMatrix(const Matrix &left, const Matrix &right);

Vec3 MatrixPosition(const Matrix &matrix);

BoneTransform ReadBone(Reader *reader, uint64_t arrayBase, int index);

Vec3 BoneWorldPosition(Reader *reader, uint64_t arrayBase, int index,
                       const Matrix &componentMatrix);

size_t ReadBoneRange(Reader *reader, uint64_t arrayBase, int firstIndex,
                     int count, std::vector<BoneTransform> &out);

Vec3 BoneLocalToWorld(const std::vector<BoneTransform> &bones, int index,
                      const Matrix &componentMatrix);
