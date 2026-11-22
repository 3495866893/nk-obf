#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "memory.h"
#include "offsets.h"

namespace FName {

inline constexpr uintptr_t BlocksOffset = 0x40;
inline constexpr uint32_t  BlockSize    = 0x20000;
inline constexpr uint32_t  OffsetScale  = 2;
inline constexpr uint32_t  MaxBlock     = 512;

inline uint64_t ModuleBase = 0;

inline void Init(uint64_t moduleBase) { ModuleBase = moduleBase; }

inline bool IsReady() { return ModuleBase > 0x10000; }

inline bool CompareNoCase(const char *left, const char *right) {
    if (left == nullptr || right == nullptr) return false;
    for (; *left != '\0' && *right != '\0'; ++left, ++right) {
        char a = *left;
        char b = *right;
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
        if (a != b) return false;
    }
    return *left == '\0' && *right == '\0';
}

inline uint64_t BlockBase(Reader *reader, uint32_t block) {
    if (reader == nullptr || !IsReady() || block >= MaxBlock) return 0;

    uint64_t ptr = 0;
    const uint64_t slot = ModuleBase + Offset::GNamesSlot + BlocksOffset +
                          static_cast<uint64_t>(block) * sizeof(uint64_t);
    if (!reader->Read(slot, ptr)) return 0;
    if (ptr < Offset::MinValidPointer || ptr > Offset::MaxValidPointer) return 0;
    return ptr;
}

inline bool Resolve(Reader *reader, uint32_t index, std::string &out) {
    out.clear();
    if (reader == nullptr || index == 0 || !IsReady()) return false;

    const uint32_t block = index >> 16;
    const uint32_t offset = (index & 0xFFFFu) * OffsetScale;
    if (offset >= BlockSize) return false;

    const uint64_t base = BlockBase(reader, block);
    if (base == 0) return false;

    const uint64_t entry = base + offset;

    uint16_t header = 0;
    if (!reader->Read(entry, header)) return false;

    const bool wide = (header & 1u) != 0;
    const uint32_t len = header >> 6;
    if (len == 0 || len > 512) return false;

    const uint32_t bytes = len * (wide ? 2u : 1u);
    std::vector<uint8_t> raw(bytes);
    if (!reader->Read(entry + 2, raw.data(), raw.size())) return false;

    out.reserve(len);
    if (!wide) {
        out.assign(reinterpret_cast<const char *>(raw.data()), bytes);
    } else {
        for (uint32_t i = 0; i < len; ++i) {
            const uint16_t cp = static_cast<uint16_t>(raw[i * 2] | (raw[i * 2 + 1] << 8));
            out.push_back(cp < 0x80 ? static_cast<char>(cp) : '?');
        }
    }
    return !out.empty();
}

inline uint32_t FindByName(Reader *reader, const char *target) {
    if (reader == nullptr || target == nullptr || !IsReady()) return 0;

    std::vector<uint8_t> buf(BlockSize + 64);
    std::string name;

    for (uint32_t blk = 0; blk < MaxBlock; ++blk) {
        const uint64_t base = BlockBase(reader, blk);
        if (base == 0) continue;
        if (!reader->Read(base, buf.data(), BlockSize + 64)) continue;

        uint32_t off = 0;
        while (off + 2 < BlockSize) {
            const uint16_t header = static_cast<uint16_t>(buf[off] | (buf[off + 1] << 8));
            const bool wide = (header & 1u) != 0;
            const uint32_t len = header >> 6;
            if (len == 0 || len > 512) break;
            const uint32_t bytes = len * (wide ? 2u : 1u);
            if (off + 2 + bytes > buf.size()) break;

            name.assign(reinterpret_cast<const char *>(buf.data() + off + 2), bytes);
            if (CompareNoCase(name.c_str(), target)) {
                return (blk << 16) | ((off / OffsetScale) & 0xFFFFu);
            }

            off += 2 + bytes;
            if (off & 1u) ++off;
        }
    }
    return 0;
}

}

namespace UObject {

    inline std::unordered_map<uint64_t, std::string> ClassNameCache;

    inline const std::string *ClassName(Reader *reader, uint64_t object) {
        if (reader == nullptr || object == 0) return nullptr;

        uint64_t classPtr = 0;
        if (!reader->Read(object + Offset::ObjectClass, classPtr)) return nullptr;
        if (classPtr < Offset::MinValidPointer ||
            classPtr > Offset::MaxValidPointer) {
            return nullptr;
        }

        const auto cached = ClassNameCache.find(classPtr);
        if (cached != ClassNameCache.end()) return &cached->second;
        if (ClassNameCache.size() > 8192) ClassNameCache.clear();

        uint32_t nameIndex = 0;
        std::string name;
        if (!reader->Read(classPtr + Offset::ObjectName, nameIndex) ||
            !FName::Resolve(reader, nameIndex, name)) {
            return nullptr;
        }

        auto &slot = ClassNameCache[classPtr];
        slot = std::move(name);
        return &slot;
    }

    inline void ClearClassNameCache() { ClassNameCache.clear(); }

}
