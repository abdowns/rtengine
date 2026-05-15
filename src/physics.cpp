// jolt config must come first
// clang-format off
#include <Jolt/Jolt.h>
// clang-format on
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
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
#include <unordered_map>

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

uint64_t encode(EntityHandle h) {
    return (uint64_t(h.generation) << 32) | h.index;
}

EntityHandle decode(uint64_t v) {
    return {uint32_t(v), uint32_t(v >> 32)};
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
        MotionType motion;
        Transform previous, current, target;
        uint32_t generation = 1;
        bool active = false;
    };

    std::vector<Record> records;
    std::vector<uint32_t> free;
    std::unordered_map<uint32_t, EntityHandle> bodyEntities;
    double accumulator = 0;
    static constexpr double step = 1.0 / 60.0;

    struct Listener final : public JPH::ContactListener {
        Impl& owner;
        std::mutex mutex;
        std::vector<ContactEvent> events;
        bool overflow = false;

        explicit Listener(Impl& o) : owner(o) {}
        void emit(ContactEvent::Type type, const JPH::Body& a, const JPH::Body& b,
                  const JPH::ContactManifold& m) {
            std::lock_guard lock(mutex);

            if (events.size() >= 65536) {
                overflow = true;
                return;
            }

            events.push_back({type, decode(a.GetUserData()), decode(b.GetUserData()),
                              from(m.GetWorldSpaceContactPointOn1(0)), from(m.mWorldSpaceNormal)});
        }

        void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& m,
                            JPH::ContactSettings&) override {
            emit(ContactEvent::Type::Begin, a, b, m);
        }

        void OnContactPersisted(const JPH::Body& a, const JPH::Body& b,
                                const JPH::ContactManifold& m, JPH::ContactSettings&) override {
            emit(ContactEvent::Type::Persist, a, b, m);
        }

        void OnContactRemoved(const JPH::SubShapeIDPair& pair) override {
            std::lock_guard lock(mutex);
            auto a = owner.bodyEntities.find(pair.GetBody1ID().GetIndexAndSequenceNumber()),
                 b = owner.bodyEntities.find(pair.GetBody2ID().GetIndexAndSequenceNumber());

            if (events.size() >= 65536) {
                overflow = true;
                return;
            }

            if (a != owner.bodyEntities.end() && b != owner.bodyEntities.end())
                events.push_back({ContactEvent::Type::End, a->second, b->second, {}, {}});
        }
    } listener{*this};
    Impl(Scene& s, uint32_t max) : scene(s) {
        if (max == 0)
            throw std::invalid_argument("maxBodies must be positive");

        system.Init(max, 0, max * 4, max * 4, layers, broadFilter, pairFilter);
        system.SetContactListener(&listener);
    }

    ~Impl() {
        system.SetContactListener(nullptr);
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

    Transform read(const Record& r) const {
        auto& bi = system.GetBodyInterface();
        Transform t = r.current;
        t.position = from(bi.GetPosition(r.id));
        t.rotation = from(bi.GetRotation(r.id));
        return t;
    }
};

PhysicsWorld::PhysicsWorld(Scene& s, uint32_t max) : impl_(std::make_unique<Impl>(s, max)) {}
PhysicsWorld::~PhysicsWorld() = default;

BodyHandle PhysicsWorld::createBody(EntityHandle e, const BodyDescription& desc) {
    auto t = impl_->scene.entity(e).transform;
    auto& c = desc.collider;

    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(t.position[i]) || !std::isfinite(t.scale[i]) ||
            !std::isfinite(c.halfExtent[i]) || c.halfExtent[i] <= 0)
            throw std::invalid_argument("Invalid body position, scale, or box dimensions");

    if (!std::isfinite(glm::length(t.rotation)) || glm::length(t.rotation) < 1e-6f ||
        !std::isfinite(desc.mass) || !std::isfinite(desc.friction) ||
        !std::isfinite(desc.restitution) || !std::isfinite(c.radius) ||
        !std::isfinite(c.halfHeight) || c.radius <= 0 || c.halfHeight < 0)
        throw std::invalid_argument("Non-finite or invalid body/collider properties");

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
    auto checked = [&](JPH::ShapeSettings::ShapeResult result) {
        if (result.HasError())
            throw std::runtime_error("Collider creation failed: " +
                                     std::string(result.GetError().c_str()));
        return result.Get();
    };

    switch (c.type) {
    case ColliderType::Box:
        shape = checked(JPH::BoxShapeSettings(vec(c.halfExtent * t.scale)).Create());
        break;
    case ColliderType::Sphere:
        if (glm::length(t.scale - glm::vec3(t.scale.x)) > 1e-5f)
            throw std::invalid_argument("Sphere requires uniform scale");

        shape = checked(JPH::SphereShapeSettings(c.radius * t.scale.x).Create());
        break;
    case ColliderType::Capsule:
        if (glm::length(t.scale - glm::vec3(t.scale.x)) > 1e-5f)
            throw std::invalid_argument("Capsule requires uniform scale");

        shape = checked(
            JPH::CapsuleShapeSettings(c.halfHeight * t.scale.x, c.radius * t.scale.x).Create());
        break;
    case ColliderType::ConvexHull: {
        JPH::Array<JPH::Vec3> pts;

        if (c.mesh) {
            for (auto& v : impl_->scene.mesh(c.mesh).vertices)
                pts.push_back(vec(v.position * t.scale));
        } else
            for (auto p : c.points)
                pts.push_back(vec(p * t.scale));
        shape = checked(JPH::ConvexHullShapeSettings(pts).Create());
        break;
    }

    case ColliderType::TriangleMesh: {
        auto& m = impl_->scene.mesh(c.mesh);
        JPH::TriangleList tris;

        for (size_t i = 0; i < m.indices.size(); i += 3)
            tris.emplace_back(vec(m.vertices[m.indices[i]].position * t.scale),
                              vec(m.vertices[m.indices[i + 1]].position * t.scale),
                              vec(m.vertices[m.indices[i + 2]].position * t.scale));
        shape = checked(JPH::MeshShapeSettings(tris).Create());
        break;
    }
    }

    auto motion = desc.motion == MotionType::Static      ? JPH::EMotionType::Static
                  : desc.motion == MotionType::Kinematic ? JPH::EMotionType::Kinematic
                                                         : JPH::EMotionType::Dynamic;
    JPH::BodyCreationSettings settings(shape, pos(t.position), quat(t.rotation), motion,
                                       desc.motion == MotionType::Static ? 0 : 1);
    settings.mFriction = desc.friction;
    settings.mRestitution = desc.restitution;
    settings.mUserData = encode(e);

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
    r.motion = desc.motion;
    r.previous = r.current = r.target = t;
    r.active = true;
    impl_->bodyEntities[id.GetIndexAndSequenceNumber()] = e;
    return {idx, r.generation};
}

void PhysicsWorld::destroyBody(BodyHandle h) {
    auto& r = impl_->record(h);
    auto& bi = impl_->system.GetBodyInterface();
    bi.RemoveBody(r.id);
    bi.DestroyBody(r.id);
    impl_->bodyEntities.erase(r.id.GetIndexAndSequenceNumber());
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

    while (impl_->accumulator >= Impl::step && steps++ < 8) {
        for (auto& r : impl_->records)
            if (r.active) {
                r.previous = r.current;

                if (r.motion == MotionType::Kinematic)
                    impl_->system.GetBodyInterface().MoveKinematic(
                        r.id, pos(r.target.position), quat(r.target.rotation), float(Impl::step));
            }

        auto error = impl_->system.Update(float(Impl::step), 1, &impl_->allocator, &impl_->jobs);

        if (error != JPH::EPhysicsUpdateError::None)
            throw std::runtime_error("Jolt update capacity exceeded");

        for (auto& r : impl_->records)
            if (r.active)
                r.current = impl_->read(r);
        impl_->accumulator -= Impl::step;
    }

    if (impl_->accumulator >= Impl::step)
        impl_->accumulator = std::fmod(impl_->accumulator, Impl::step);
    {
        std::lock_guard lock(impl_->listener.mutex);

        if (impl_->listener.overflow)
            throw std::runtime_error(
                "Contact event queue capacity exceeded; call takeContacts every frame");
    }
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
    r.previous = r.current = r.target = t;

    if (impl_->scene.alive(r.entity))
        impl_->scene.entity(r.entity).transform = t;
}

Transform PhysicsWorld::pose(BodyHandle h) const {
    return impl_->read(impl_->record(h));
}

void PhysicsWorld::setKinematicTarget(BodyHandle h, const Transform& t) {
    auto& r = impl_->record(h);

    if (r.motion != MotionType::Kinematic)
        throw std::invalid_argument("Body is not kinematic");

    if (glm::length(t.scale - r.current.scale) > 1e-5f)
        throw std::invalid_argument("Kinematic target cannot change scale");

    r.target = t;
}

std::optional<PhysicsRayHit> PhysicsWorld::rayQuery(glm::vec3 origin, glm::vec3 direction,
                                                    float max) const {
    if (max <= 0 || glm::length(direction) < 1e-8f)
        throw std::invalid_argument("Invalid physics ray");

    direction = glm::normalize(direction);
    JPH::RRayCast ray(pos(origin), vec(direction * max));
    JPH::RayCastResult hit;

    if (!impl_->system.GetNarrowPhaseQuery().CastRay(ray, hit))
        return {};
    JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), hit.mBodyID);

    if (!lock.Succeeded())
        return {};
    auto p = origin + direction * (hit.mFraction * max);
    auto& b = lock.GetBody();
    return PhysicsRayHit{decode(b.GetUserData()), p,
                         from(b.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, pos(p))),
                         hit.mFraction * max};
}

std::vector<ContactEvent> PhysicsWorld::takeContacts() {
    std::lock_guard lock(impl_->listener.mutex);
    std::vector<ContactEvent> out;
    out.swap(impl_->listener.events);
    impl_->listener.overflow = false;
    return out;
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
