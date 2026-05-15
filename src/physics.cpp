// jolt config must come first
// clang-format off
#include <Jolt/Jolt.h>
// clang-format on
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
#include <algorithm>
#include <mutex>
#include <rtengine/physics.hpp>
#include <stdexcept>
#include <thread>

namespace rt {

namespace {
std::mutex runtimeMutex;
unsigned runtimeUsers = 0;

struct Runtime {
    Runtime() {
        std::lock_guard lock(runtimeMutex);

        if (runtimeUsers++ == 0) {
            JPH::RegisterDefaultAllocator();
            JPH::Factory::sInstance = new JPH::Factory;
            JPH::RegisterTypes();
        }
    }

    ~Runtime() {
        std::lock_guard lock(runtimeMutex);

        if (--runtimeUsers == 0) {
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }
    }
};

JPH::Vec3 vec(glm::vec3 v) {
    return {v.x, v.y, v.z};
}

JPH::RVec3 pos(glm::vec3 v) {
    return {v.x, v.y, v.z};
}

glm::vec3 from(JPH::Vec3Arg v) {
    return {v.GetX(), v.GetY(), v.GetZ()};
}

JPH::Quat quat(glm::quat q) {
    return {q.x, q.y, q.z, q.w};
}

glm::quat from(JPH::QuatArg q) {
    return {q.GetW(), q.GetX(), q.GetY(), q.GetZ()};
}

class BroadLayers final : public JPH::BroadPhaseLayerInterface {
  public:
    uint32_t GetNumBroadPhaseLayers() const override {
        return 2;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer l) const override {
        return JPH::BroadPhaseLayer(uint8_t(l));
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer l) const override {
        return l.GetValue() == 0 ? "Static" : "Moving";
    }

#endif
};

class ObjectBroadFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
    bool ShouldCollide(JPH::ObjectLayer a, JPH::BroadPhaseLayer b) const override {
        return a != 0 || b.GetValue() != 0;
    }
};

class PairFilter final : public JPH::ObjectLayerPairFilter {
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a != 0 || b != 0;
    }
};
} // namespace
struct PhysicsWorld::Impl {
    Runtime runtime;
    Scene& scene;
    BroadLayers layers;
    ObjectBroadFilter broadFilter;
    PairFilter pairFilter;
    JPH::TempAllocatorImpl allocator{16 * 1024 * 1024};
    JPH::JobSystemThreadPool jobs{JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
                                  int(std::clamp(std::thread::hardware_concurrency(), 2u, 5u) - 1)};
    JPH::PhysicsSystem system;

    struct Record {
        JPH::BodyID id;
        EntityHandle entity;
        Transform previous, current;
        uint32_t generation = 1;
        bool active = false;
    };

    std::vector<Record> records;
    std::vector<uint32_t> free;
    double accumulator = 0;
    static constexpr double step = 1.0 / 60.0;

    Impl(Scene& s, uint32_t max) : scene(s) {
        if (max == 0)
            throw std::invalid_argument("maxBodies must be positive");

        system.Init(max, 0, max * 4, max * 4, layers, broadFilter, pairFilter);
    }

    ~Impl() {
        auto& bi = system.GetBodyInterface();

        for (auto& r : records)
            if (r.active) {
                bi.RemoveBody(r.id);
                bi.DestroyBody(r.id);
            }
    }

    Record& record(BodyHandle h) {
        if (h.index >= records.size() || !records[h.index].active ||
            records[h.index].generation != h.generation)
            throw std::invalid_argument("Stale physics body handle");

        return records[h.index];
    }

    const Record& record(BodyHandle h) const {
        return const_cast<Impl*>(this)->record(h);
    }
};

PhysicsWorld::PhysicsWorld(Scene& s, uint32_t max) : impl_(std::make_unique<Impl>(s, max)) {}
PhysicsWorld::~PhysicsWorld() = default;

BodyHandle PhysicsWorld::createBody(EntityHandle e, const BodyDescription& desc) {
    auto t = impl_->scene.entity(e).transform;
    auto& c = desc.collider;
    t.rotation = glm::normalize(t.rotation);

    for (auto& r : impl_->records)
        if (r.active && r.entity == e)
            throw std::invalid_argument("Entity already has a physics body");

    if (glm::any(glm::lessThanEqual(t.scale, glm::vec3(0))) || desc.mass <= 0 ||
        desc.friction < 0 || desc.restitution < 0 || desc.restitution > 1)
        throw std::invalid_argument("Invalid physics scale or body properties");

    if (c.type == ColliderType::TriangleMesh && desc.motion != MotionType::Static)
        throw std::invalid_argument("Triangle-mesh colliders must be static");

    JPH::RefConst<JPH::Shape> shape;

    switch (c.type) {
    case ColliderType::Box:
        shape = JPH::BoxShapeSettings(vec(c.halfExtent * t.scale)).Create().Get();
        break;
    case ColliderType::Sphere:
        if (glm::length(t.scale - glm::vec3(t.scale.x)) > 1e-5f)
            throw std::invalid_argument("Sphere requires uniform scale");

        shape = JPH::SphereShapeSettings(c.radius * t.scale.x).Create().Get();
        break;
    case ColliderType::Capsule:
        if (glm::length(t.scale - glm::vec3(t.scale.x)) > 1e-5f)
            throw std::invalid_argument("Capsule requires uniform scale");

        shape = JPH::CapsuleShapeSettings(c.halfHeight * t.scale.x, c.radius * t.scale.x)
                    .Create()
                    .Get();
        break;
    case ColliderType::ConvexHull: {
        JPH::Array<JPH::Vec3> pts;

        if (c.mesh) {
            for (auto& v : impl_->scene.mesh(c.mesh).vertices)
                pts.push_back(vec(v.position * t.scale));
        } else
            for (auto p : c.points)
                pts.push_back(vec(p * t.scale));
        shape = JPH::ConvexHullShapeSettings(pts).Create().Get();
        break;
    }

    case ColliderType::TriangleMesh: {
        auto& m = impl_->scene.mesh(c.mesh);
        JPH::TriangleList tris;

        for (size_t i = 0; i < m.indices.size(); i += 3)
            tris.emplace_back(vec(m.vertices[m.indices[i]].position * t.scale),
                              vec(m.vertices[m.indices[i + 1]].position * t.scale),
                              vec(m.vertices[m.indices[i + 2]].position * t.scale));
        shape = JPH::MeshShapeSettings(tris).Create().Get();
        break;
    }
    }

    if (!shape)
        throw std::runtime_error("Collider creation failed");

    auto motion = desc.motion == MotionType::Static      ? JPH::EMotionType::Static
                  : desc.motion == MotionType::Kinematic ? JPH::EMotionType::Kinematic
                                                         : JPH::EMotionType::Dynamic;
    JPH::BodyCreationSettings settings(shape, pos(t.position), quat(t.rotation), motion,
                                       desc.motion == MotionType::Static ? 0 : 1);
    settings.mFriction = desc.friction;
    settings.mRestitution = desc.restitution;

    if (desc.motion == MotionType::Dynamic) {
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = desc.mass;
    }

    if (desc.continuousCollision)
        settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
    auto& bi = impl_->system.GetBodyInterface();
    auto body = bi.CreateBody(settings);

    if (!body)
        throw std::runtime_error("Physics body capacity exceeded");

    auto id = body->GetID();
    bi.AddBody(id, desc.motion == MotionType::Static ? JPH::EActivation::DontActivate
                                                     : JPH::EActivation::Activate);
    uint32_t idx;

    if (impl_->free.empty()) {
        idx = uint32_t(impl_->records.size());
        impl_->records.emplace_back();
    } else {
        idx = impl_->free.back();
        impl_->free.pop_back();
    }

    auto& r = impl_->records[idx];
    r.id = id;
    r.entity = e;
    r.previous = r.current = t;
    r.active = true;
    return {idx, r.generation};
}

void PhysicsWorld::destroyBody(BodyHandle h) {
    auto& r = impl_->record(h);
    auto& bi = impl_->system.GetBodyInterface();
    bi.RemoveBody(r.id);
    bi.DestroyBody(r.id);
    r.active = false;
    ++r.generation;
    impl_->free.push_back(h.index);
}

void PhysicsWorld::advance(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0)
        throw std::invalid_argument("Invalid physics frame duration");

    for (uint32_t i = 0; i < impl_->records.size(); ++i) {
        auto& r = impl_->records[i];

        if (r.active && !impl_->scene.alive(r.entity))
            destroyBody({i, r.generation});
    }

    impl_->accumulator += std::min(seconds, 0.25);
    int steps = 0;
    auto& bi = impl_->system.GetBodyInterface();

    while (impl_->accumulator >= Impl::step && steps++ < 8) {
        for (auto& r : impl_->records)
            r.previous = r.current;
        impl_->system.Update(float(Impl::step), 1, &impl_->allocator, &impl_->jobs);

        for (auto& r : impl_->records)
            if (r.active) {
                r.current.position = from(bi.GetPosition(r.id));
                r.current.rotation = from(bi.GetRotation(r.id));
            }
        impl_->accumulator -= Impl::step;
    }

    if (impl_->accumulator >= Impl::step)
        impl_->accumulator = std::fmod(impl_->accumulator, Impl::step);
}

void PhysicsWorld::synchronize() {
    float alpha = interpolationAlpha();

    for (auto& r : impl_->records)
        if (r.active && impl_->scene.alive(r.entity)) {
            auto& t = impl_->scene.entity(r.entity).transform;
            t.position = glm::mix(r.previous.position, r.current.position, alpha);
            t.rotation = glm::slerp(r.previous.rotation, r.current.rotation, alpha);
        }
}

void PhysicsWorld::setGravity(glm::vec3 v) {
    impl_->system.SetGravity(vec(v));
}

void PhysicsWorld::addForce(BodyHandle h, glm::vec3 v) {
    impl_->system.GetBodyInterface().AddForce(impl_->record(h).id, vec(v));
}

void PhysicsWorld::addImpulse(BodyHandle h, glm::vec3 v) {
    impl_->system.GetBodyInterface().AddImpulse(impl_->record(h).id, vec(v));
}

void PhysicsWorld::setVelocity(BodyHandle h, glm::vec3 v) {
    impl_->system.GetBodyInterface().SetLinearVelocity(impl_->record(h).id, vec(v));
}

glm::vec3 PhysicsWorld::velocity(BodyHandle h) const {
    return from(impl_->system.GetBodyInterface().GetLinearVelocity(impl_->record(h).id));
}

void PhysicsWorld::setPose(BodyHandle h, const Transform& t) {
    auto& r = impl_->record(h);

    if (glm::length(t.scale - r.current.scale) > 1e-5f)
        throw std::invalid_argument("Recreate body to change collider scale");

    impl_->system.GetBodyInterface().SetPositionAndRotation(r.id, pos(t.position), quat(t.rotation),
                                                            JPH::EActivation::Activate);
    r.previous = r.current = t;

    if (impl_->scene.alive(r.entity))
        impl_->scene.entity(r.entity).transform = t;
}

Transform PhysicsWorld::pose(BodyHandle h) const {
    auto& r = impl_->record(h);
    auto& bi = impl_->system.GetBodyInterface();
    Transform t = r.current;
    t.position = from(bi.GetPosition(r.id));
    t.rotation = from(bi.GetRotation(r.id));
    return t;
}

float PhysicsWorld::interpolationAlpha() const {
    return float(impl_->accumulator / Impl::step);
}

size_t PhysicsWorld::bodyCount() const {
    size_t count = 0;

    for (auto& r : impl_->records)
        count += r.active;
    return count;
}
} // namespace rt
