#pragma once

#include "scene.hpp"

namespace rt {
using BodyHandle = Handle<struct BodyTag>;

enum class MotionType { Static, Dynamic };

enum class ColliderType { Box, Sphere, Capsule };

struct Collider {
    ColliderType type = ColliderType::Box;
    glm::vec3 halfExtent{.5f};
    float radius = .5f, halfHeight = .5f;
};

struct BodyDescription {
    MotionType motion = MotionType::Dynamic;
    Collider collider;
    float mass = 1, friction = .5f, restitution = 0;
};

class PhysicsWorld {
  public:
    explicit PhysicsWorld(Scene&);
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    BodyHandle createBody(EntityHandle, const BodyDescription&);
    void destroyBody(BodyHandle);
    void update(double frameSeconds);
    void setGravity(glm::vec3);
    void addForce(BodyHandle, glm::vec3);
    void addImpulse(BodyHandle, glm::vec3);
    void setVelocity(BodyHandle, glm::vec3);
    glm::vec3 velocity(BodyHandle) const;
    void setPose(BodyHandle, const Transform&);
    Transform pose(BodyHandle) const;
    size_t bodyCount() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace rt
