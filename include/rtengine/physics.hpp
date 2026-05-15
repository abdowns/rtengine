#pragma once

#include "scene.hpp"
#include <span>

namespace rt {
using BodyHandle = Handle<struct BodyTag>;

enum class MotionType { Static, Dynamic, Kinematic };

enum class ColliderType { Box, Sphere, Capsule, ConvexHull, TriangleMesh };

struct Collider {
    ColliderType type = ColliderType::Box;
    glm::vec3 halfExtent{.5f};
    float radius = .5f, halfHeight = .5f;
    std::vector<glm::vec3> points;
    MeshHandle mesh;
};

struct BodyDescription {
    MotionType motion = MotionType::Dynamic;
    Collider collider;
    float mass = 1, friction = .5f, restitution = 0;
    bool continuousCollision = false;
};

struct ContactEvent {
    enum class Type { Begin, Persist, End } type = Type::Begin;
    EntityHandle a, b;
    glm::vec3 point{0}, normal{0};
};

struct PhysicsRayHit {
    EntityHandle entity;
    glm::vec3 position{0}, normal{0};
    float distance = 0;
};

class PhysicsWorld {
  public:
    explicit PhysicsWorld(Scene&, uint32_t maxBodies = 4096);
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    BodyHandle createBody(EntityHandle, const BodyDescription&);
    void destroyBody(BodyHandle);
    void advance(double frameSeconds);
    void synchronize();
    void setGravity(glm::vec3);
    void addForce(BodyHandle, glm::vec3);
    void addImpulse(BodyHandle, glm::vec3);
    void setVelocity(BodyHandle, glm::vec3);
    glm::vec3 velocity(BodyHandle) const;
    void setPose(BodyHandle, const Transform&);
    Transform pose(BodyHandle) const;
    void setKinematicTarget(BodyHandle, const Transform&);
    std::optional<PhysicsRayHit> rayQuery(glm::vec3 origin, glm::vec3 direction,
                                          float maxDistance) const;
    std::vector<ContactEvent> takeContacts();
    float interpolationAlpha() const;
    size_t bodyCount() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace rt
