#pragma once

#include "scene.hpp"
#include <limits>
#include <span>

namespace rt {
struct Ray {
    glm::vec3 origin{0}, direction{0, 0, -1};
    float minDistance = 1e-4f, maxDistance = std::numeric_limits<float>::infinity();
};

struct Aabb {
    glm::vec3 min{std::numeric_limits<float>::infinity()},
        max{-std::numeric_limits<float>::infinity()};
    void expand(glm::vec3);
    void expand(const Aabb&);
    float area() const;
    bool intersect(const Ray&, float limit) const;
};

struct BvhNode {
    Aabb bounds;
    uint32_t left = 0, right = 0, first = 0, count = 0;
};

struct Hit {
    float distance = std::numeric_limits<float>::infinity();
    uint32_t triangle = UINT32_MAX;
    glm::vec2 barycentric{0};
};

class Bvh {
  public:
    void build(std::span<const Aabb> bounds);
    const std::vector<BvhNode>& nodes() const {
        return nodes_;
    }

    Hit intersect(const Mesh&, const Ray&) const;

  private:
    uint32_t split(uint32_t first, uint32_t count, std::span<const Aabb>);
    std::vector<BvhNode> nodes_;
    std::vector<uint32_t> order_;
};

std::vector<Aabb> triangleBounds(const Mesh&);
bool intersectTriangle(const Mesh&, uint32_t, const Ray&, Hit&);
} // namespace rt
