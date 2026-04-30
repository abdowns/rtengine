#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

namespace rt {
struct Transform {
    glm::vec3 position{0};
    glm::quat rotation{1, 0, 0, 0};
    glm::vec3 scale{1};
    glm::mat4 matrix() const;
};

struct Vertex {
    glm::vec3 position{0}, normal{0, 1, 0};
    glm::vec2 uv{0};
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

struct Material {
    glm::vec4 baseColor{1};
    float metallic = 0, roughness = 0.5f;
};

struct Camera {
    Transform transform;
    float verticalFov = 60.f, nearPlane = 0.1f, farPlane = 100.f;
    glm::mat4 view() const;
    glm::mat4 projection(float aspect) const;
};

struct Entity {
    std::string name;
    Transform transform;
    int mesh = -1, material = -1;
};

struct Scene {
    std::vector<Entity> entities;
    std::vector<Mesh> meshes;
    std::vector<Material> materials;

    Entity& createEntity(std::string name = {});
};
} // namespace rt
