#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace RayTrace {

    constexpr uint32_t InvalidID = 0xFFFFFFFFu;

    struct Vec3f {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        Vec3f() = default;

        Vec3f(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}

        [[nodiscard]] float Component(int axis) const {
            return axis == 0 ? x : (axis == 1 ? y : z);
        }
    };

    struct Bounds {
        float minX = 0.0f, minY = 0.0f, minZ = 0.0f;
        float maxX = 0.0f, maxY = 0.0f, maxZ = 0.0f;

        void Reset() {
            constexpr float Inf = std::numeric_limits<float>::infinity();
            minX = minY = minZ = Inf;
            maxX = maxY = maxZ = -Inf;
        }

        void Expand(const Vec3f &p) {
            if (p.x < minX) minX = p.x;
            if (p.y < minY) minY = p.y;
            if (p.z < minZ) minZ = p.z;
            if (p.x > maxX) maxX = p.x;
            if (p.y > maxY) maxY = p.y;
            if (p.z > maxZ) maxZ = p.z;
        }

        void Expand(const Bounds &b) {
            if (b.minX < minX) minX = b.minX;
            if (b.minY < minY) minY = b.minY;
            if (b.minZ < minZ) minZ = b.minZ;
            if (b.maxX > maxX) maxX = b.maxX;
            if (b.maxY > maxY) maxY = b.maxY;
            if (b.maxZ > maxZ) maxZ = b.maxZ;
        }

        [[nodiscard]] bool IsValid() const { return minX <= maxX; }

        [[nodiscard]] Vec3f Centroid() const {
            return Vec3f((minX + maxX) * 0.5f, (minY + maxY) * 0.5f,
                         (minZ + maxZ) * 0.5f);
        }

        [[nodiscard]] float Extent(int axis) const {
            return axis == 0 ? (maxX - minX)
                             : (axis == 1 ? (maxY - minY) : (maxZ - minZ));
        }

        [[nodiscard]] int LongestAxis() const {
            const float ex = maxX - minX;
            const float ey = maxY - minY;
            const float ez = maxZ - minZ;
            if (ex >= ey && ex >= ez) return 0;
            return ey >= ez ? 1 : 2;
        }
    };

    struct Hit {
        uint32_t geomID = InvalidID;
        uint32_t primID = InvalidID;
        float t = 0.0f;
        float u = 0.0f;
        float v = 0.0f;

        [[nodiscard]] bool Valid() const { return geomID != InvalidID; }
    };

    inline Vec3f SafeInverse(const Vec3f &d) {
        constexpr float Huge = 1.0e30f;
        Vec3f r;
        r.x = (d.x != 0.0f) ? (1.0f / d.x) : (std::signbit(d.x) ? -Huge : Huge);
        r.y = (d.y != 0.0f) ? (1.0f / d.y) : (std::signbit(d.y) ? -Huge : Huge);
        r.z = (d.z != 0.0f) ? (1.0f / d.z) : (std::signbit(d.z) ? -Huge : Huge);
        return r;
    }

    inline void SlabRange(const Bounds &b, const Vec3f &org, const Vec3f &invDir,
                          float &tNear, float &tFar) {
        float t0 = (b.minX - org.x) * invDir.x;
        float t1 = (b.maxX - org.x) * invDir.x;
        if (t0 > t1) std::swap(t0, t1);
        tNear = t0;
        tFar = t1;

        t0 = (b.minY - org.y) * invDir.y;
        t1 = (b.maxY - org.y) * invDir.y;
        if (t0 > t1) std::swap(t0, t1);
        if (t0 > tNear) tNear = t0;
        if (t1 < tFar) tFar = t1;

        t0 = (b.minZ - org.z) * invDir.z;
        t1 = (b.maxZ - org.z) * invDir.z;
        if (t0 > t1) std::swap(t0, t1);
        if (t0 > tNear) tNear = t0;
        if (t1 < tFar) tFar = t1;
    }

    inline bool SlabHit(const Bounds &b, const Vec3f &org, const Vec3f &invDir,
                        float tMax) {
        float tNear = 0.0f;
        float tFar = 0.0f;
        SlabRange(b, org, invDir, tNear, tFar);

        if (!(tNear <= tFar)) return false;
        if (!(tNear < tMax)) return false;
        if (!(tFar > 0.0f)) return false;
        return true;
    }

    inline float EntryDistance(const Bounds &b, const Vec3f &org,
                               const Vec3f &invDir) {
        float tNear = 0.0f;
        float tFar = 0.0f;
        SlabRange(b, org, invDir, tNear, tFar);
        return tNear;
    }

    inline bool RayTriangle(const float *v0, const float *v1, const float *v2,
                            const Vec3f &org, const Vec3f &dir, float tMax,
                            float &t, float &u, float &v) {
        const double e1x = static_cast<double>(v1[0]) - v0[0];
        const double e1y = static_cast<double>(v1[1]) - v0[1];
        const double e1z = static_cast<double>(v1[2]) - v0[2];
        const double e2x = static_cast<double>(v2[0]) - v0[0];
        const double e2y = static_cast<double>(v2[1]) - v0[1];
        const double e2z = static_cast<double>(v2[2]) - v0[2];

        const double dx = dir.x, dy = dir.y, dz = dir.z;

        const double px = dy * e2z - dz * e2y;
        const double py = dz * e2x - dx * e2z;
        const double pz = dx * e2y - dy * e2x;

        const double det = e1x * px + e1y * py + e1z * pz;
        if (det == 0.0 || !std::isfinite(det)) return false;

        const double invDet = 1.0 / det;

        const double tx = static_cast<double>(org.x) - v0[0];
        const double ty = static_cast<double>(org.y) - v0[1];
        const double tz = static_cast<double>(org.z) - v0[2];

        constexpr double Eps = 1.0e-9;

        const double bu = (tx * px + ty * py + tz * pz) * invDet;
        if (bu < -Eps || bu > 1.0 + Eps) return false;

        const double qx = ty * e1z - tz * e1y;
        const double qy = tz * e1x - tx * e1z;
        const double qz = tx * e1y - ty * e1x;

        const double bv = (dx * qx + dy * qy + dz * qz) * invDet;
        if (bv < -Eps || bu + bv > 1.0 + 2.0 * Eps) return false;

        const double bt = (e2x * qx + e2y * qy + e2z * qz) * invDet;
        if (bt < 0.0 || bt > static_cast<double>(tMax)) return false;

        t = static_cast<float>(bt);
        u = static_cast<float>(bu);
        v = static_cast<float>(bv);
        return true;
    }

    class Bvh {
    public:
        static constexpr uint32_t MaxLeafPrims = 4;
        static constexpr uint32_t MaxDepth = 48;
        static constexpr int StackSize = 80;

        void Build(std::vector<Bounds> &&primBounds) {
            nodes_.clear();
            order_.clear();
            scratch_ = std::move(primBounds);

            const uint32_t count = static_cast<uint32_t>(scratch_.size());
            if (count == 0) {
                scratch_.clear();
                return;
            }

            order_.resize(count);
            for (uint32_t i = 0; i < count; ++i) order_[i] = i;

            nodes_.reserve(static_cast<size_t>(count) * 2u);
            BuildRange(0, count, 0);

            scratch_.clear();
            scratch_.shrink_to_fit();
        }

        void Clear() {
            nodes_.clear();
            order_.clear();
            scratch_.clear();
        }

        [[nodiscard]] bool Empty() const { return nodes_.empty(); }

        [[nodiscard]] const Bounds &RootBounds() const {
            static const Bounds kEmpty{};
            return nodes_.empty() ? kEmpty : nodes_.front().bounds;
        }

        [[nodiscard]] size_t NodeCount() const { return nodes_.size(); }

        template <typename OnPrim>
        bool TraverseClosest(const Vec3f &org, const Vec3f &invDir, float &tMax,
                             OnPrim &&onPrim) const {
            if (nodes_.empty()) return false;

            uint32_t stack[StackSize];
            int top = 0;
            stack[top++] = 0;

            bool hit = false;

            while (top > 0) {
                const uint32_t index = stack[--top];
                const Node &node = nodes_[index];

                if (!SlabHit(node.bounds, org, invDir, tMax)) continue;

                if (node.count != 0) {
                    const uint32_t last = node.offset + node.count;
                    for (uint32_t i = node.offset; i < last; ++i) {
                        if (onPrim(order_[i], tMax)) hit = true;
                    }
                    continue;
                }

                const uint32_t left = index + 1;
                const uint32_t right = node.offset;
                const float tl = EntryDistance(nodes_[left].bounds, org, invDir);
                const float tr = EntryDistance(nodes_[right].bounds, org, invDir);

                if (tl <= tr) {
                    stack[top++] = right;
                    stack[top++] = left;
                } else {
                    stack[top++] = left;
                    stack[top++] = right;
                }
            }

            return hit;
        }

        template <typename OnPrim>
        bool TraverseAny(const Vec3f &org, const Vec3f &invDir, float tMax,
                         OnPrim &&onPrim) const {
            if (nodes_.empty()) return false;

            uint32_t stack[StackSize];
            int top = 0;
            stack[top++] = 0;

            while (top > 0) {
                const uint32_t index = stack[--top];
                const Node &node = nodes_[index];

                if (!SlabHit(node.bounds, org, invDir, tMax)) continue;

                if (node.count != 0) {
                    const uint32_t last = node.offset + node.count;
                    for (uint32_t i = node.offset; i < last; ++i) {
                        if (onPrim(order_[i])) return true;
                    }
                    continue;
                }

                const uint32_t left = index + 1;
                const uint32_t right = node.offset;
                const float tl = EntryDistance(nodes_[left].bounds, org, invDir);
                const float tr = EntryDistance(nodes_[right].bounds, org, invDir);

                if (tl <= tr) {
                    stack[top++] = right;
                    stack[top++] = left;
                } else {
                    stack[top++] = left;
                    stack[top++] = right;
                }
            }

            return false;
        }

    private:
        struct Node {
            Bounds bounds;
            uint32_t offset = 0;
            uint32_t count = 0;
        };

        static_assert(sizeof(Node) == 32, "node size");

        uint32_t BuildRange(uint32_t begin, uint32_t end, uint32_t depth) {
            Bounds bounds;
            bounds.Reset();
            for (uint32_t i = begin; i < end; ++i) {
                bounds.Expand(scratch_[order_[i]]);
            }

            const uint32_t index = static_cast<uint32_t>(nodes_.size());
            nodes_.push_back(Node{bounds, 0, 0});

            const uint32_t count = end - begin;

            const auto makeLeaf = [this, index, begin, count]() {
                nodes_[index].offset = begin;
                nodes_[index].count = count;
                return index;
            };

            if (count <= MaxLeafPrims || depth >= MaxDepth) return makeLeaf();

            Bounds centroidBounds;
            centroidBounds.Reset();
            for (uint32_t i = begin; i < end; ++i) {
                centroidBounds.Expand(scratch_[order_[i]].Centroid());
            }

            const int axis = centroidBounds.LongestAxis();
            if (!(centroidBounds.Extent(axis) > 0.0f)) return makeLeaf();

            const uint32_t mid = begin + count / 2;
            std::nth_element(
                order_.begin() + begin, order_.begin() + mid, order_.begin() + end,
                [this, axis](uint32_t a, uint32_t b) {
                    return scratch_[a].Centroid().Component(axis) <
                           scratch_[b].Centroid().Component(axis);
                });

            BuildRange(begin, mid, depth + 1);
            const uint32_t right = BuildRange(mid, end, depth + 1);

            nodes_[index].offset = right;
            nodes_[index].count = 0;
            return index;
        }

        std::vector<Node> nodes_;
        std::vector<uint32_t> order_;
        std::vector<Bounds> scratch_;
    };

}  // namespace RayTrace
