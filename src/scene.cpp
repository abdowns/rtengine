#include <climits>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <rtengine/scene.hpp>
#include <stdexcept>

namespace rt {

namespace {
void validateMaterial(const Material& m) {
    auto finite = [](float f) { return std::isfinite(f); };

    for (int i = 0; i < 4; ++i)
        if (!finite(m.baseColor[i]) || m.baseColor[i] < 0 || m.baseColor[i] > 1)
            throw std::invalid_argument("Base color must be finite and in [0,1]");

    for (int i = 0; i < 3; ++i)
        if (!finite(m.emissive[i]) || m.emissive[i] < 0)
            throw std::invalid_argument("Emission must be finite and nonnegative");

    for (float f : {m.metallic, m.roughness, m.occlusionStrength, m.alphaCutoff})
        if (!finite(f) || f < 0 || f > 1)
            throw std::invalid_argument("PBR factors must be finite and in [0,1]");

    if (!finite(m.normalScale) || m.normalScale < 0)
        throw std::invalid_argument("Normal scale must be finite and nonnegative");

    for (auto texture :
         {m.baseColorMap, m.normalMap, m.metallicRoughnessMap, m.occlusionMap, m.emissiveMap})
        if (texture && (texture->width <= 0 || texture->height <= 0 ||
                        texture->rgba.size() != size_t(texture->width) * texture->height * 4))
            throw std::invalid_argument("Texture requires complete RGBA8 pixels");
}
} // namespace

glm::mat4 Transform::matrix() const {
    return glm::translate(glm::mat4(1), position) * glm::mat4_cast(rotation) *
           glm::scale(glm::mat4(1), scale);
}

glm::mat4 Camera::view() const {
    return glm::inverse(transform.matrix());
}

glm::mat4 Camera::projection(float aspect) const {
    return glm::perspective(verticalFov, aspect, nearPlane, farPlane);
}

EntityHandle Scene::createEntity(std::string name) {
    uint32_t i;

    if (free_.empty()) {
        i = uint32_t(slots_.size());
        slots_.push_back({});
    } else {
        i = free_.back();
        free_.pop_back();
    }

    auto& s = slots_[i];
    s.active = true;
    s.value = Entity{};
    s.value.name = std::move(name);
    ++topology_;
    return {i, s.generation};
}

bool Scene::alive(EntityHandle h) const {
    return h.index < slots_.size() && slots_[h.index].active &&
           slots_[h.index].generation == h.generation;
}

void Scene::destroyEntity(EntityHandle h) {
    if (!alive(h))
        throw std::invalid_argument("Stale entity handle");

    auto& s = slots_[h.index];
    s.active = false;
    s.value = {};
    ++s.generation;
    free_.push_back(h.index);
    ++topology_;
}

Entity& Scene::entity(EntityHandle h) {
    if (!alive(h))
        throw std::invalid_argument("Stale entity handle");

    return slots_[h.index].value;
}

const Entity& Scene::entity(EntityHandle h) const {
    if (!alive(h))
        throw std::invalid_argument("Stale entity handle");

    return slots_[h.index].value;
}

MeshHandle Scene::addMesh(Mesh m) {
    if (m.vertices.empty() || m.indices.empty() || m.indices.size() % 3 ||
        m.indices.size() > INT_MAX || m.vertices.size() > UINT32_MAX)
        throw std::invalid_argument("Mesh requires indexed triangles");

    for (auto i : m.indices)
        if (i >= m.vertices.size())
            throw std::invalid_argument("Mesh index out of bounds");

    for (auto& v : m.vertices) {
        for (int i = 0; i < 3; ++i)
            if (!std::isfinite(v.position[i]) || !std::isfinite(v.normal[i]))
                throw std::invalid_argument("Mesh has non-finite positions/normals");

        for (int i = 0; i < 4; ++i)
            if (!std::isfinite(v.tangent[i]))
                throw std::invalid_argument("Mesh has non-finite tangents");

        if (!std::isfinite(v.uv.x) || !std::isfinite(v.uv.y) || glm::length(v.normal) < 1e-6f ||
            glm::length(glm::vec3(v.tangent)) < 1e-6f)
            throw std::invalid_argument("Mesh has invalid UVs or zero-length normal/tangent");
    }

    meshes_.push_back(std::move(m));
    ++topology_;
    return {uint32_t(meshes_.size() - 1), 1};
}

MaterialHandle Scene::addMaterial(Material m) {
    validateMaterial(m);
    materials_.push_back(std::move(m));
    ++materialsRevision_;
    return {uint32_t(materials_.size() - 1), 1};
}

const Mesh& Scene::mesh(MeshHandle h) const {
    if (h.generation != 1 || h.index >= meshes_.size())
        throw std::invalid_argument("Invalid mesh handle");

    return meshes_[h.index];
}

const Material& Scene::material(MaterialHandle h) const {
    if (h.generation != 1 || h.index >= materials_.size())
        throw std::invalid_argument("Invalid material handle");

    return materials_[h.index];
}

void Scene::updateMaterial(MaterialHandle h, Material m) {
    (void)material(h);
    validateMaterial(m);
    materials_[h.index] = std::move(m);
    ++materialsRevision_;
}

std::vector<EntityHandle> Scene::entities() const {
    std::vector<EntityHandle> out;

    for (uint32_t i = 0; i < slots_.size(); ++i)
        if (slots_[i].active)
            out.push_back({i, slots_[i].generation});
    return out;
}

void generateNormalsAndTangents(Mesh& m, bool normals, bool tangents) {
    std::vector<glm::vec3> ns(m.vertices.size(), glm::vec3(0)), ts = ns, bs = ns;

    for (size_t i = 0; i < m.indices.size(); i += 3) {
        auto a = m.indices[i], b = m.indices[i + 1], c = m.indices[i + 2];
        auto& v0 = m.vertices[a];
        auto& v1 = m.vertices[b];
        auto& v2 = m.vertices[c];
        auto e1 = v1.position - v0.position, e2 = v2.position - v0.position;
        auto n = glm::cross(e1, e2);
        auto d1 = v1.uv - v0.uv, d2 = v2.uv - v0.uv;
        float det = d1.x * d2.y - d1.y * d2.x;
        glm::vec3 t(0), bt(0);

        if (std::abs(det) > 1e-10f) {
            t = (e1 * d2.y - e2 * d1.y) / det;
            bt = (e2 * d1.x - e1 * d2.x) / det;
        }

        for (auto idx : {a, b, c}) {
            ns[idx] += n;
            ts[idx] += t;
            bs[idx] += bt;
        }
    }

    for (size_t i = 0; i < m.vertices.size(); ++i) {
        auto& v = m.vertices[i];

        if (normals)
            v.normal = glm::length(ns[i]) > 1e-8f ? glm::normalize(ns[i]) : glm::vec3(0, 1, 0);

        if (tangents) {
            glm::vec3 t = ts[i] - v.normal * glm::dot(v.normal, ts[i]);

            if (glm::length(t) < 1e-8f)
                t = glm::cross(std::abs(v.normal.y) < 0.9f ? glm::vec3(0, 1, 0)
                                                           : glm::vec3(1, 0, 0),
                               v.normal);
            t = glm::normalize(t);
            v.tangent = glm::vec4(t, glm::dot(glm::cross(v.normal, t), bs[i]) < 0 ? -1.f : 1.f);
        }
    }
}

Mesh makeBox(glm::vec3 h) {
    Mesh m;
    m.name = "box";
    glm::vec3 n[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    glm::vec3 u[6] = {{0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {-1, 0, 0}};

    for (int f = 0; f < 6; ++f) {
        auto v = glm::cross(n[f], u[f]);
        uint32_t start = uint32_t(m.vertices.size());

        for (int k = 0; k < 4; ++k) {
            glm::vec2 uv(k == 1 || k == 2 ? 1 : 0, k >= 2 ? 1 : 0);
            auto p = (n[f] + u[f] * (uv.x * 2 - 1) + v * (uv.y * 2 - 1)) * h;
            m.vertices.push_back({p, n[f], uv, glm::vec4(u[f], 1)});
        }

        m.indices.insert(m.indices.end(),
                         {start, start + 1, start + 2, start, start + 2, start + 3});
    }

    return m;
}

Mesh makeSphere(float r, unsigned segments, unsigned rings) {
    if (r <= 0 || segments < 3 || rings < 2)
        throw std::invalid_argument("Invalid sphere parameters");

    Mesh m;
    m.name = "sphere";

    for (unsigned y = 0; y <= rings; ++y)
        for (unsigned x = 0; x <= segments; ++x) {
            float u = float(x) / segments, v = float(y) / rings;
            float a = u * glm::two_pi<float>(), b = v * glm::pi<float>();
            glm::vec3 n(std::sin(b) * std::cos(a), std::cos(b), std::sin(b) * std::sin(a));
            m.vertices.push_back({n * r, n, {u, v}, {-std::sin(a), 0, std::cos(a), 1}});
        }

    for (unsigned y = 0; y < rings; ++y)
        for (unsigned x = 0; x < segments; ++x) {
            uint32_t a = y * (segments + 1) + x, b = a + segments + 1;
            m.indices.insert(m.indices.end(), {a, a + 1, b, a + 1, b + 1, b});
        }

    return m;
}
} // namespace rt
