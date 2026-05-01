#include <algorithm>
#include <numeric>
#include <rtengine/bvh.hpp>

namespace rt {
void Aabb::expand(glm::vec3 p) {
    min = glm::min(min, p);
    max = glm::max(max, p);
}

void Aabb::expand(const Aabb& b) {
    min = glm::min(min, b.min);
    max = glm::max(max, b.max);
}

float Aabb::area() const {
    auto d = glm::max(max - min, glm::vec3(0));
    return 2 * (d.x * d.y + d.y * d.z + d.z * d.x);
}

bool Aabb::intersect(const Ray& ray, float limit) const {
    float lo = ray.minDistance, hi = std::min(ray.maxDistance, limit);

    for (int a = 0; a < 3; ++a) {
        if (std::abs(ray.direction[a]) < 1e-12f) {
            if (ray.origin[a] < min[a] || ray.origin[a] > max[a])
                return false;
        } else {
            float t0 = (min[a] - ray.origin[a]) / ray.direction[a],
                  t1 = (max[a] - ray.origin[a]) / ray.direction[a];

            if (t0 > t1)
                std::swap(t0, t1);
            lo = std::max(lo, t0);
            hi = std::min(hi, t1);

            if (hi < lo)
                return false;
        }
    }

    return hi >= lo;
}

Aabb Aabb::transformed(const glm::mat4& m) const {
    Aabb b;

    for (int i = 0; i < 8; ++i)
        b.expand(glm::vec3(m * glm::vec4((i & 1) ? max.x : min.x, (i & 2) ? max.y : min.y,
                                         (i & 4) ? max.z : min.z, 1)));
    return b;
}

void Bvh::build(std::span<const Aabb> bounds) {
    nodes_.clear();
    order_.resize(bounds.size());
    std::iota(order_.begin(), order_.end(), 0);

    if (!bounds.empty())
        split(0, uint32_t(bounds.size()), bounds, 0);
}

uint32_t Bvh::split(uint32_t first, uint32_t count, std::span<const Aabb> bounds, unsigned depth) {
    uint32_t idx = uint32_t(nodes_.size());
    nodes_.push_back({});
    Aabb all, centers;

    for (uint32_t j = first; j < first + count; ++j) {
        all.expand(bounds[order_[j]]);
        centers.expand((bounds[order_[j]].min + bounds[order_[j]].max) * .5f);
    }

    nodes_[idx].bounds = all;
    nodes_[idx].first = first;
    nodes_[idx].count = count;

    if (count <= 4 || depth >= 48 || all.area() < 1e-12f)
        return idx;
    constexpr int N = 12;
    float best = float(count) * all.area();
    int axis = -1, cut = -1;

    for (int a = 0; a < 3; ++a) {
        float extent = centers.max[a] - centers.min[a];

        if (extent < 1e-7f)
            continue;
        Aabb bins[N];
        uint32_t sizes[N] = {};

        for (uint32_t j = first; j < first + count; ++j) {
            auto& b = bounds[order_[j]];
            int n = std::clamp(int((((b.min[a] + b.max[a]) * .5f) - centers.min[a]) / extent * N),
                               0, N - 1);
            bins[n].expand(b);
            ++sizes[n];
        }

        for (int c = 0; c < N - 1; ++c) {
            Aabb l, r;
            uint32_t lc = 0, rc = 0;

            for (int n = 0; n <= c; ++n)
                if (sizes[n]) {
                    l.expand(bins[n]);
                    lc += sizes[n];
                }

            for (int n = c + 1; n < N; ++n)
                if (sizes[n]) {
                    r.expand(bins[n]);
                    rc += sizes[n];
                }

            if (!lc || !rc)
                continue;
            float cost = all.area() + lc * l.area() + rc * r.area();

            if (cost < best) {
                best = cost;
                axis = a;
                cut = c;
            }
        }
    }

    if (axis < 0)
        return idx;
    float extent = centers.max[axis] - centers.min[axis];
    auto mid =
        std::partition(order_.begin() + first, order_.begin() + first + count, [&](uint32_t id) {
            auto& b = bounds[id];
            int n = std::clamp(
                int((((b.min[axis] + b.max[axis]) * .5f) - centers.min[axis]) / extent * N), 0,
                N - 1);
            return n <= cut;
        });
    uint32_t leftCount = uint32_t(mid - (order_.begin() + first));

    if (!leftCount || leftCount == count)
        return idx;
    auto l = split(first, leftCount, bounds, depth + 1),
         r = split(first + leftCount, count - leftCount, bounds, depth + 1);
    nodes_[idx].left = l;
    nodes_[idx].right = r;
    nodes_[idx].count = 0;
    return idx;
}

std::vector<Aabb> triangleBounds(const Mesh& m) {
    std::vector<Aabb> out(m.indices.size() / 3);

    for (size_t t = 0; t < out.size(); ++t)
        for (int k = 0; k < 3; ++k)
            out[t].expand(m.vertices[m.indices[t * 3 + k]].position);
    return out;
}

bool intersectTriangle(const Mesh& m, uint32_t t, const Ray& ray, Hit& hit) {
    auto a = m.vertices[m.indices[t * 3]].position, b = m.vertices[m.indices[t * 3 + 1]].position,
         c = m.vertices[m.indices[t * 3 + 2]].position;
    auto e1 = b - a, e2 = c - a, p = glm::cross(ray.direction, e2);
    float det = glm::dot(e1, p);

    if (std::abs(det) < 1e-9f)
        return false;
    float inv = 1 / det;
    auto s = ray.origin - a;
    float u = glm::dot(s, p) * inv;

    if (u < 0 || u > 1)
        return false;
    auto q = glm::cross(s, e1);
    float v = glm::dot(ray.direction, q) * inv;

    if (v < 0 || u + v > 1)
        return false;
    float d = glm::dot(e2, q) * inv;

    if (d < ray.minDistance || d > ray.maxDistance || d >= hit.distance)
        return false;
    hit = {d, t, {u, v}};
    return true;
}

Hit Bvh::intersect(const Mesh& m, const Ray& ray) const {
    Hit result;

    if (nodes_.empty())
        return result;
    uint32_t stack[64];
    int top = 0;
    stack[top++] = 0;

    while (top > 0) {
        auto& n = nodes_[stack[--top]];

        if (!n.bounds.intersect(ray, result.distance))
            continue;
        if (n.count) {
            for (uint32_t i = n.first; i < n.first + n.count; ++i)
                intersectTriangle(m, order_[i], ray, result);
        } else {
            stack[top++] = n.right;
            stack[top++] = n.left;
        }
    }

    return result;
}
} // namespace rt
