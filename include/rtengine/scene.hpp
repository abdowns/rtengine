#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>
#include <vector>

namespace rt {
template <class Tag> struct Handle {
    uint32_t index = UINT32_MAX, generation = 0;

    explicit operator bool() const {
        return index != UINT32_MAX;
    }

    bool operator==(const Handle&) const = default;
};

using EntityHandle = Handle<struct EntityTag>;
using MeshHandle = Handle<struct MeshTag>;
using MaterialHandle = Handle<struct MaterialTag>;

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
    std::string name;
};

struct TextureData {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
    std::string name;
};

enum class AlphaMode { Opaque, Mask, Blend };

struct Material {
    std::string name;
    glm::vec4 baseColor{1};
    glm::vec3 emissive{0};
    float metallic = 0, roughness = 0.5f, alphaCutoff = 0.5f;
    AlphaMode alphaMode = AlphaMode::Opaque;
    bool doubleSided = false;
    std::shared_ptr<const TextureData> baseColorMap;
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
    MeshHandle mesh;
    MaterialHandle material;
    bool visible = true;
};

class Scene {
  public:
    EntityHandle createEntity(std::string name = {});
    void destroyEntity(EntityHandle);
    Entity& entity(EntityHandle);
    const Entity& entity(EntityHandle) const;
    bool alive(EntityHandle) const;
    MeshHandle addMesh(Mesh);
    MaterialHandle addMaterial(Material);
    const Mesh& mesh(MeshHandle) const;
    const Material& material(MaterialHandle) const;
    std::vector<EntityHandle> entities() const;

    uint64_t topologyRevision() const {
        return topology_;
    }

  private:
    struct Slot {
        Entity value;
        uint32_t generation = 1;
        bool active = false;
    };

    std::vector<Slot> slots_;
    std::vector<uint32_t> free_;
    std::vector<Mesh> meshes_;
    std::vector<Material> materials_;
    uint64_t topology_ = 1;
};

void generateNormals(Mesh&);
Mesh makeBox(glm::vec3 halfExtent = glm::vec3(0.5f));
Mesh makeSphere(float radius = 0.5f, unsigned segments = 24, unsigned rings = 16);
} // namespace rt
