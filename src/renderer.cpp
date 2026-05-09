#include "shaders.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cstring>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <numeric>
#include <rtengine/bvh.hpp>
#include <rtengine/renderer.hpp>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace rt {

namespace {
struct Texture {
    GLuint id = 0;
    GLenum target = GL_TEXTURE_2D;
    uint64_t bytes = 0;
    Texture() = default;

    explicit Texture(GLenum t) : target(t) {
        glGenTextures(1, &id);
    }

    ~Texture() {
        if (id)
            glDeleteTextures(1, &id);
    }

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& o) noexcept : id(o.id), target(o.target), bytes(o.bytes) {
        o.id = 0;
    }

    Texture& operator=(Texture&& o) noexcept {
        if (this != &o) {
            if (id)
                glDeleteTextures(1, &id);
            id = o.id;
            target = o.target;
            bytes = o.bytes;
            o.id = 0;
        }

        return *this;
    }

    void bind(int unit) const {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(target, id);
    }
};

struct BufferTexture {
    GLuint buffer = 0;
    Texture texture{GL_TEXTURE_BUFFER};
    uint64_t bytes = 0;
    BufferTexture() {
        glGenBuffers(1, &buffer);
    }

    ~BufferTexture() {
        glDeleteBuffers(1, &buffer);
    }

    void upload(const std::vector<glm::vec4>& data, GLenum usage = GL_STATIC_DRAW) {
        GLint max;
        glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &max);

        if (data.size() > size_t(max) || data.size() > 16777216)
            throw std::runtime_error("Acceleration/material buffer exceeds OpenGL texture-buffer "
                                     "or float-index capacity; reduce scene complexity");
        glm::vec4 empty(0);
        bytes = std::max<size_t>(1, data.size()) * sizeof(glm::vec4);
        glBindBuffer(GL_TEXTURE_BUFFER, buffer);
        glBufferData(GL_TEXTURE_BUFFER, GLsizeiptr(bytes), data.empty() ? &empty : data.data(),
                     usage);
        glBindTexture(GL_TEXTURE_BUFFER, texture.id);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, buffer);
    }
};

struct Program {
    GLuint id = 0;
    std::unordered_map<std::string, GLint> locations;
    Program() = default;
    Program(const char* vs, const char* fs) {
        auto compile = [](GLenum type, const char* source) {
            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);
            GLint success;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

            if (!success) {
                GLint size;
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &size);
                std::string log(size, '\0');
                glGetShaderInfoLog(shader, size, nullptr, log.data());
                glDeleteShader(shader);
                throw std::runtime_error("GLSL compilation failed: " + log);
            }

            return shader;
        };

        auto v = compile(GL_VERTEX_SHADER, vs);
        GLuint f = 0;
        try {
            f = compile(GL_FRAGMENT_SHADER, fs);
            id = glCreateProgram();
            glAttachShader(id, v);
            glAttachShader(id, f);
            glLinkProgram(id);
            GLint success;
            glGetProgramiv(id, GL_LINK_STATUS, &success);

            if (!success) {
                GLint size;
                glGetProgramiv(id, GL_INFO_LOG_LENGTH, &size);
                std::string log(size, '\0');
                glGetProgramInfoLog(id, size, nullptr, log.data());
                throw std::runtime_error("GLSL link failed: " + log);
            }
        } catch (...) {
            glDeleteShader(v);

            if (f)
                glDeleteShader(f);

            if (id)
                glDeleteProgram(id);
            id = 0;
            throw;
        }

        glDeleteShader(v);
        glDeleteShader(f);
    }

    ~Program() {
        if (id)
            glDeleteProgram(id);
    }

    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;

    void use() {
        glUseProgram(id);
    }

    GLint location(const std::string& name) {
        auto it = locations.find(name);

        if (it != locations.end())
            return it->second;
        GLint l = glGetUniformLocation(id, name.c_str());
        locations[name] = l;
        return l;
    }

    void set(const std::string& name, int v) {
        glUniform1i(location(name), v);
    }

    void set(const std::string& name, float v) {
        glUniform1f(location(name), v);
    }

    void set(const std::string& name, glm::vec3 v) {
        glUniform3fv(location(name), 1, glm::value_ptr(v));
    }

    void set(const std::string& name, glm::vec4 v) {
        glUniform4fv(location(name), 1, glm::value_ptr(v));
    }

    void set(const std::string& name, const glm::mat4& m) {
        glUniformMatrix4fv(location(name), 1, GL_FALSE, glm::value_ptr(m));
    }

    void sampler(const char* name, const Texture& t, int unit) {
        t.bind(unit);
        set(name, unit);
    }
};

struct Target {
    GLuint fbo = 0;
    std::vector<Texture> colors;
    Texture depth;
    int width = 0, height = 0;
    Target() {
        glGenFramebuffers(1, &fbo);
    }

    ~Target() {
        glDeleteFramebuffers(1, &fbo);
    }

    void init(int w, int h, std::initializer_list<GLenum> formats, bool withDepth = false) {
        width = w;
        height = h;
        colors.clear();
        depth = Texture{};
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        std::vector<GLenum> attachments;
        int index = 0;

        for (GLenum format : formats) {
            Texture t(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, t.id);
            glTexImage2D(GL_TEXTURE_2D, 0, GLint(format), w, h, 0,
                         format == GL_R32F ? GL_RED : GL_RGBA,
                         format == GL_RGBA8 ? GL_UNSIGNED_BYTE : GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            t.bytes = uint64_t(w) * h * (format == GL_RGBA16F ? 8 : 4);
            colors.push_back(std::move(t));
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index, GL_TEXTURE_2D,
                                   colors.back().id, 0);
            attachments.push_back(GL_COLOR_ATTACHMENT0 + index++);
        }

        glDrawBuffers(GLsizei(attachments.size()), attachments.data());

        if (withDepth) {
            depth = Texture(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, depth.id);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT,
                         GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            depth.bytes = uint64_t(w) * h * 4;
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth.id, 0);
        } else
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
        check();
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT | (withDepth ? GL_DEPTH_BUFFER_BIT : 0));
    }

    void check() const {
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("OpenGL framebuffer incomplete");
    }

    void bind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, width, height);
    }

    uint64_t bytes() const {
        uint64_t total = depth.bytes;

        for (auto& t : colors)
            total += t.bytes;
        return total;
    }
};

struct GpuMesh {
    GLuint vao = 0, vbo = 0, ibo = 0;
    uint32_t indexCount = 0, blasRoot = 0;
    Bvh bvh;
    uint64_t bytes = 0;
    GpuMesh() {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ibo);
    }

    ~GpuMesh() {
        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &vbo);
        glDeleteBuffers(1, &ibo);
    }

    void init(const Mesh& m) {
        indexCount = uint32_t(m.indices.size());
        bytes = m.vertices.size() * sizeof(Vertex) + m.indices.size() * sizeof(uint32_t);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(m.vertices.size() * sizeof(Vertex)),
                     m.vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(m.indices.size() * sizeof(uint32_t)),
                     m.indices.data(), GL_STATIC_DRAW);
        size_t offsets[] = {offsetof(Vertex, position), offsetof(Vertex, normal),
                            offsetof(Vertex, uv), offsetof(Vertex, tangent)};
        int sizes[] = {3, 3, 2, 4};

        for (int i = 0; i < 4; ++i) {
            glEnableVertexAttribArray(i);
            glVertexAttribPointer(i, sizes[i], GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  reinterpret_cast<void*>(offsets[i]));
        }

        bvh.build(triangleBounds(m));
    }

    void draw() const {
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, GLsizei(indexCount), GL_UNSIGNED_INT, nullptr);
    }
};

std::vector<glm::vec4> packNodes(const Bvh& bvh, uint32_t nodeOffset, uint32_t firstOffset) {
    std::vector<glm::vec4> out;
    out.reserve(bvh.nodes().size() * 3);

    for (auto& n : bvh.nodes()) {
        out.emplace_back(n.bounds.min, float(n.left + nodeOffset));
        out.emplace_back(n.bounds.max, float(n.right + nodeOffset));
        out.emplace_back(float(n.first + firstOffset), float(n.count), 0, 0);
    }

    return out;
}

uint64_t key(EntityHandle h) {
    return uint64_t(h.generation) << 32 | h.index;
}

bool sameMatrix(const glm::mat4& a, const glm::mat4& b, float tolerance = 1e-5f) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (std::abs(a[c][r] - b[c][r]) > tolerance)
                return false;
    return true;
}

void validateSettings(const RenderSettings& s) {
    if (s.rayResolutionScale < .125f || s.rayResolutionScale > 1 ||
        !std::isfinite(s.rayResolutionScale) || s.exposure <= 0 || !std::isfinite(s.exposure) ||
        s.historyWeight < 0 || s.historyWeight > .98f || !std::isfinite(s.historyWeight) ||
        s.maxLights == 0 || s.maxLights > 32 || s.maxShadowMaps > 16 || s.shadowResolution < 64 ||
        s.shadowResolution > 4096 || s.materialTextureResolution < 16 ||
        s.materialTextureResolution > 2048 || s.memoryBudgetBytes < 16 * 1024 * 1024)
        throw std::invalid_argument(
            "Invalid renderer settings: lights 1..32, shadow layers 0..16, ray scale .125..1, "
            "history 0.. .98, material size 16..2048, shadow size 64..4096");
}
} // namespace
struct Renderer::Impl {
    RenderSettings settings;
    FrameStats stats;
    Environment env;
    std::string device;
    Program geometry{shaders::geometry_vert, shaders::geometry_frag},
        shadow{shaders::shadow_vert, shaders::shadow_frag},
        lighting{shaders::fullscreen_vert, shaders::lighting_frag},
        ray{shaders::fullscreen_vert, shaders::ray_frag},
        temporal{shaders::fullscreen_vert, shaders::temporal_frag},
        filter{shaders::fullscreen_vert, shaders::filter_frag},
        transparent{shaders::geometry_vert, shaders::transparent_frag},
        present{shaders::fullscreen_vert, shaders::present_frag},
        environmentProgram{shaders::fullscreen_vert, shaders::environment_frag};
    BufferTexture materials, blasNodes, triangles, tlasNodes, instances, instanceOrder;
    std::array<Texture, 5> arrays;
    Texture shadowMaps, environmentMap, irradianceMap, prefilterMap, brdfMap;
    uint32_t allocatedShadowResolution = 0;
    int allocatedShadowLayers = 0;
    GLuint fullscreenVAO = 0, shadowFBO = 0, environmentFBO = 0;
    Target gbuffer, rawRays, histories[2], filters[2], hdr, output;
    int width = 0, height = 0, rayWidth = 0, rayHeight = 0, historyIndex = 0, frameIndex = 0;
    const Scene* scene = nullptr;
    uint64_t materialRevision = 0;
    bool historyValid = false, environmentDirty = true;
    std::vector<std::unique_ptr<GpuMesh>> meshes;
    std::vector<glm::vec4> packedBlas, packedTriangles;
    std::unordered_map<uint64_t, glm::mat4> previousModels;

    struct Instance {
        EntityHandle entity;
        MeshHandle mesh;
        MaterialHandle material;
        glm::mat4 world, previous;
    };

    std::vector<Instance> rayInstances;
    std::vector<uint64_t> lastMembership;
    Bvh topLevel;
    std::vector<Aabb> worldBounds;
    glm::mat4 previousVP{1};
    Camera previousCamera;
    std::vector<glm::vec4> lastLightState;
    std::array<glm::mat4, 16> shadowMatrices;

    struct PreparedLight {
        Light light;
        glm::vec3 position, direction;
        int shadowBase = -1, shadowCount = 0;
    };

    std::vector<PreparedLight> lights;

    struct Timers {
        std::array<GLuint, 14> queries{};
        bool pending = false;
        Timers() {
            glGenQueries(14, queries.data());
        }

        ~Timers() {
            glDeleteQueries(14, queries.data());
        }
    } timers[3];
    int timerIndex = 0;
    bool recording = false, elapsedTimers = false;

    explicit Impl(const RenderSettings& s) : settings(s) {
        GLint units;
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);

        if (units < 16)
            throw std::runtime_error("Renderer requires 16 fragment texture samplers");

        glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &units);

        if (units < 22)
            throw std::runtime_error("Renderer requires 22 combined texture binding slots");

        GLint attachments;
        glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &attachments);

        if (attachments < 4)
            throw std::runtime_error("Renderer requires four framebuffer color attachments");

        device = std::string(reinterpret_cast<const char*>(glGetString(GL_RENDERER))) + " / " +
                 reinterpret_cast<const char*>(glGetString(GL_VERSION));
        GLint bits = 0;
        glGetQueryiv(GL_TIME_ELAPSED, GL_QUERY_COUNTER_BITS, &bits);
        elapsedTimers = bits > 0;
        glGenVertexArrays(1, &fullscreenVAO);
        glGenFramebuffers(1, &shadowFBO);
        glGenFramebuffers(1, &environmentFBO);
        materials.upload({});
        blasNodes.upload({});
        triangles.upload({});
        tlasNodes.upload({});
        instances.upload({});
        instanceOrder.upload({});
    }

    ~Impl() {
        glDeleteVertexArrays(1, &fullscreenVAO);
        glDeleteFramebuffers(1, &shadowFBO);
        glDeleteFramebuffers(1, &environmentFBO);
    }

    uint64_t memory() const {
        uint64_t n = materials.bytes + blasNodes.bytes + triangles.bytes + tlasNodes.bytes +
                     instances.bytes + instanceOrder.bytes;

        for (auto& m : meshes)
            n += m->bytes;

        for (auto& a : arrays)
            n += a.bytes;
        n += shadowMaps.bytes + environmentMap.bytes + irradianceMap.bytes + prefilterMap.bytes +
             brdfMap.bytes;
        n += gbuffer.bytes() + rawRays.bytes() + histories[0].bytes() + histories[1].bytes() +
             filters[0].bytes() + filters[1].bytes() + hdr.bytes() + output.bytes();
        return n;
    }

    void budget(uint64_t additional = 0) const {
        if (memory() + additional > settings.memoryBudgetBytes)
            throw std::runtime_error(
                "Renderer GPU allocation budget exceeded; reduce texture/shadow resolution, "
                "viewport size, or scene complexity, or raise memoryBudgetBytes");
    }

    void fullScreen() {
        glBindVertexArray(fullscreenVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    void resize(int w, int h) {
        GLint max;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max);

        if (w > max || h > max)
            throw std::runtime_error("Render dimensions exceed GPU maximum texture size");

        int rw = std::max(1, int(std::ceil(w * settings.rayResolutionScale))),
            rh = std::max(1, int(std::ceil(h * settings.rayResolutionScale)));

        if (w == width && h == height && rw == rayWidth && rh == rayHeight)
            return;

        uint64_t newSize = uint64_t(w) * h * (8 + 4 + 8 + 8 + 4 + 8 + 4 + 16) +
                           uint64_t(rw) * rh * (16 + 2 * 28 + 16);
        uint64_t old = gbuffer.bytes() + rawRays.bytes() + histories[0].bytes() +
                       histories[1].bytes() + filters[0].bytes() + filters[1].bytes() +
                       hdr.bytes() + output.bytes();

        if (memory() - old + newSize > settings.memoryBudgetBytes)
            throw std::runtime_error("Framebuffer allocation exceeds memory budget");

        width = w;
        height = h;
        rayWidth = rw;
        rayHeight = rh;
        gbuffer.init(w, h, {GL_RGBA16F, GL_RGBA8, GL_RGBA16F, GL_RGBA16F}, true);
        rawRays.init(rw, rh, {GL_RGBA16F, GL_RGBA16F});

        for (auto& target : histories)
            target.init(rw, rh, {GL_RGBA16F, GL_RGBA16F, GL_RGBA16F, GL_R32F});
        filters[0].init(rw, rh, {GL_RGBA16F, GL_RGBA16F});
        filters[1].init(w, h, {GL_RGBA16F, GL_RGBA16F});
        hdr.init(w, h, {GL_RGBA16F});
        glBindFramebuffer(GL_FRAMEBUFFER, hdr.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gbuffer.depth.id,
                               0);
        hdr.check();
        output.init(w, h, {GL_RGBA8});
        historyValid = false;
    }

    void uploadMaterials(const Scene& s) {
        std::array<std::unordered_map<const TextureData*, int>, 5> layers;
        std::array<std::vector<std::shared_ptr<const TextureData>>, 5> textures;
        std::vector<glm::vec4> packed;

        for (uint32_t i = 0; i < s.materialCount(); ++i) {
            auto& m = s.material({i, 1});
            std::shared_ptr<const TextureData> maps[] = {
                m.baseColorMap, m.normalMap, m.metallicRoughnessMap, m.occlusionMap, m.emissiveMap};
            int indices[5] = {};

            for (int role = 0; role < 5; ++role)
                if (maps[role]) {
                    auto* ptr = maps[role].get();
                    auto it = layers[role].find(ptr);

                    if (it == layers[role].end()) {
                        indices[role] = int(textures[role].size() + 1);
                        layers[role][ptr] = indices[role];
                        textures[role].push_back(maps[role]);
                    } else
                        indices[role] = it->second;
                }

            packed.push_back(m.baseColor);
            packed.emplace_back(m.emissive, m.metallic);
            packed.emplace_back(m.roughness, m.normalScale, m.occlusionStrength, m.alphaCutoff);
            packed.emplace_back(indices[0], indices[1], indices[2], indices[3]);
            packed.emplace_back(indices[4], int(m.alphaMode), m.doubleSided ? 1 : 0, 0);
        }

        GLint maxLayers;
        glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxLayers);
        uint64_t allocation = 0;
        auto size = settings.materialTextureResolution;

        for (auto& tex : textures) {
            if (tex.size() + 1 > size_t(maxLayers))
                throw std::runtime_error(
                    "Material texture array exceeds GPU layer limit; reduce distinct textures");
            allocation += uint64_t(size) * size * 4 * (tex.size() + 1) * 4 / 3;
        }

        uint64_t old = materials.bytes;

        for (auto& a : arrays)
            old += a.bytes;

        if (memory() - old + allocation + packed.size() * 16 > settings.memoryBudgetBytes)
            throw std::runtime_error("Material textures exceed GPU allocation budget");

        uint8_t defaults[5][4] = {{255, 255, 255, 255},
                                  {128, 128, 255, 255},
                                  {255, 255, 255, 255},
                                  {255, 255, 255, 255},
                                  {255, 255, 255, 255}};
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        for (int role = 0; role < 5; ++role) {
            arrays[role] = Texture(GL_TEXTURE_2D_ARRAY);
            auto& a = arrays[role];
            glBindTexture(GL_TEXTURE_2D_ARRAY, a.id);
            int count = int(textures[role].size() + 1);
            GLenum format = role == 0 || role == 4 ? GL_SRGB8_ALPHA8 : GL_RGBA8;
            glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, format, size, size, count, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, nullptr);

            for (int layer = 0; layer < count; ++layer) {
                std::vector<uint8_t> pixels(size_t(size) * size * 4);

                if (layer == 0)
                    for (size_t p = 0; p < pixels.size(); p += 4)
                        std::memcpy(pixels.data() + p, defaults[role], 4);
                else {
                    auto& t = *textures[role][layer - 1];

                    if (t.width <= 0 || t.height <= 0 ||
                        t.rgba.size() != size_t(t.width) * t.height * 4)
                        throw std::invalid_argument("Invalid texture data");

                    for (uint32_t y = 0; y < size; ++y)
                        for (uint32_t x = 0; x < size; ++x) {
                            float fx = (x + .5f) * t.width / size - .5f,
                                  fy = (y + .5f) * t.height / size - .5f;
                            int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
                            float ax = fx - x0, ay = fy - y0;

                            for (int c = 0; c < 4; ++c) {
                                auto sample = [&](int sx, int sy) {
                                    float v =
                                        t.rgba[(size_t(std::clamp(sy, 0, t.height - 1)) * t.width +
                                                std::clamp(sx, 0, t.width - 1)) *
                                                   4 +
                                               c] /
                                        255.f;

                                    if ((role == 0 || role == 4) && c < 3)
                                        v = v <= .04045f ? v / 12.92f
                                                         : std::pow((v + .055f) / 1.055f, 2.4f);
                                    return v;
                                };

                                float value = glm::mix(
                                    glm::mix(sample(x0, y0), sample(x0 + 1, y0), ax),
                                    glm::mix(sample(x0, y0 + 1), sample(x0 + 1, y0 + 1), ax), ay);

                                if ((role == 0 || role == 4) && c < 3)
                                    value = value <= .0031308f
                                                ? 12.92f * value
                                                : 1.055f * std::pow(value, 1 / 2.4f) - .055f;
                                pixels[(size_t(y) * size + x) * 4 + c] =
                                    uint8_t(std::clamp(value * 255.f, 0.f, 255.f));
                            }
                        }
                }

                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, size, size, 1, GL_RGBA,
                                GL_UNSIGNED_BYTE, pixels.data());
            }

            glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
            a.bytes = uint64_t(size) * size * count * 4 * 4 / 3;
        }

        materials.upload(packed);
        materialRevision = s.materialRevision();
        historyValid = false;
    }

    void uploadMeshes(const Scene& s) {
        if (meshes.size() == s.meshCount())
            return;

        while (meshes.size() < s.meshCount()) {
            uint32_t i = uint32_t(meshes.size());
            auto& mesh = s.mesh({i, 1});
            budget(mesh.vertices.size() * sizeof(Vertex) + mesh.indices.size() * sizeof(uint32_t) +
                   mesh.indices.size() / 3 * 9 * 16 + mesh.indices.size() / 3 * 6 * 16);
            auto gpu = std::make_unique<GpuMesh>();
            gpu->init(mesh);
            gpu->blasRoot = uint32_t(packedBlas.size() / 3);
            uint32_t triOffset = uint32_t(packedTriangles.size() / 9);
            auto nodes = packNodes(gpu->bvh, gpu->blasRoot, triOffset);
            packedBlas.insert(packedBlas.end(), nodes.begin(), nodes.end());

            for (auto tri : gpu->bvh.order()) {
                for (int k = 0; k < 3; ++k) {
                    auto& v = mesh.vertices[mesh.indices[tri * 3 + k]];
                    packedTriangles.emplace_back(v.position, v.uv.x);
                }

                for (int k = 0; k < 3; ++k) {
                    auto& v = mesh.vertices[mesh.indices[tri * 3 + k]];
                    packedTriangles.emplace_back(v.normal, v.uv.y);
                }

                for (int k = 0; k < 3; ++k)
                    packedTriangles.push_back(mesh.vertices[mesh.indices[tri * 3 + k]].tangent);
            }

            meshes.push_back(std::move(gpu));
        }

        blasNodes.upload(packedBlas);
        triangles.upload(packedTriangles);
        historyValid = false;
    }

    void prepareInstances(const Scene& s, const std::vector<EntityHandle>& handles) {
        rayInstances.clear();
        worldBounds.clear();
        std::vector<uint64_t> membership;
        std::vector<glm::vec4> packed;
        stats.triangles = 0;
        bool moved = false;

        for (auto h : handles) {
            auto& e = s.entity(h);

            if (!e.visible || !e.mesh)
                continue;
            (void)s.mesh(e.mesh);
            auto& material = s.material(e.material);
            auto world = e.transform.matrix();

            if (!std::isfinite(glm::determinant(world)) ||
                std::abs(glm::determinant(world)) < 1e-8f)
                throw std::invalid_argument("Renderable entity has singular/non-finite transform");

            stats.triangles += meshes[e.mesh.index]->indexCount / 3;

            if (material.alphaMode == AlphaMode::Blend)
                continue;
            auto previous = previousModels.contains(key(h)) ? previousModels[key(h)] : world;
            moved |= !sameMatrix(previous, world);
            rayInstances.push_back({h, e.mesh, e.material, world, previous});
            worldBounds.push_back(meshes[e.mesh.index]->bvh.nodes()[0].bounds.transformed(world));
            membership.push_back(key(h));
            membership.push_back(uint64_t(e.mesh.index) << 32 | e.material.index);
            auto inverse = glm::inverse(world);

            for (int c = 0; c < 4; ++c)
                packed.push_back(inverse[c]);

            for (int c = 0; c < 4; ++c)
                packed.push_back(world[c]);

            for (int c = 0; c < 4; ++c)
                packed.push_back(previous[c]);
            packed.emplace_back(float(meshes[e.mesh.index]->blasRoot), float(e.material.index),
                                float(h.index), float(h.generation));
        }

        if (membership != lastMembership) {
            topLevel.build(worldBounds);
            lastMembership = std::move(membership);
            historyValid = false;
        } else
            topLevel.refit(worldBounds);
        // periodic rebuild keeps traversal fast
        if (moved && frameIndex % 120 == 0)
            topLevel.build(worldBounds);
        std::vector<glm::vec4> order;

        for (auto i : topLevel.order())
            order.emplace_back(float(i), 0, 0, 0);
        tlasNodes.upload(packNodes(topLevel, 0, 0), GL_DYNAMIC_DRAW);
        instanceOrder.upload(order, GL_DYNAMIC_DRAW);
        instances.upload(packed, GL_DYNAMIC_DRAW);
        stats.instances = uint32_t(rayInstances.size());
    }

    void environmentInit() {
        if (!environmentDirty)
            return;

        int w = env.pixels.empty() ? 512 : int(env.width),
            h = env.pixels.empty() ? 256 : int(env.height);
        std::vector<glm::vec3> pixels = env.pixels;

        if (pixels.empty()) {
            pixels.resize(size_t(w) * h);

            for (int y = 0; y < h; ++y) {
                float ny = std::cos((y + .5f) / h * glm::pi<float>());
                auto c = ny >= 0 ? glm::mix(env.horizon, env.zenith, std::pow(ny, .4f))
                                 : glm::mix(env.horizon, env.ground, std::pow(-ny, .25f));

                for (int x = 0; x < w; ++x)
                    pixels[y * w + x] = c;
            }
        }

        uint64_t old =
            environmentMap.bytes + irradianceMap.bytes + prefilterMap.bytes + brdfMap.bytes;
        uint64_t estimate = uint64_t(w) * h * 8 * 4 / 3 + uint64_t(64) * 32 * 8 +
                            uint64_t(512) * 256 * 8 * 4 / 3 + uint64_t(128) * 128 * 8;

        if (memory() - old + estimate > settings.memoryBudgetBytes)
            throw std::runtime_error("Environment map exceeds memory budget");

        auto initTexture = [](Texture& t, int width, int height, int levels, bool wrap) {
            t = Texture(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, t.id);

            for (int l = 0; l < levels; ++l)
                glTexImage2D(GL_TEXTURE_2D, l, GL_RGBA16F, std::max(1, width >> l),
                             std::max(1, height >> l), 0, GL_RGBA, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap ? GL_REPEAT : GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
            t.bytes = uint64_t(width) * height * 8 * (levels > 1 ? 4 : 3) / 3;
        };

        int levels = 1 + int(std::floor(std::log2(std::max(w, h))));
        initTexture(environmentMap, w, h, levels, true);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGB, GL_FLOAT, pixels.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        initTexture(irradianceMap, 64, 32, 1, true);
        initTexture(prefilterMap, 512, 256, 9, true);
        initTexture(brdfMap, 128, 128, 1, false);
        glBindFramebuffer(GL_FRAMEBUFFER, environmentFBO);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        environmentProgram.use();
        environmentProgram.sampler("sourceMap", environmentMap, 0);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        auto pass = [&](Texture& t, int mode, int level, int width, int height, float roughness) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.id,
                                   level);

            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                throw std::runtime_error("Environment framebuffer incomplete");

            glViewport(0, 0, width, height);
            environmentProgram.set("mode", mode);
            environmentProgram.set("roughness", roughness);
            fullScreen();
        };

        pass(irradianceMap, 0, 0, 64, 32, 0);

        for (int l = 0; l < 9; ++l)
            pass(prefilterMap, 1, l, std::max(1, 512 >> l), std::max(1, 256 >> l), l / 8.f);
        pass(brdfMap, 2, 0, 128, 128, 0);
        environmentDirty = false;
        historyValid = false;
    }

    void prepareLights(const Scene& s, const std::vector<EntityHandle>& handles) {
        lights.clear();
        std::vector<glm::vec4> state;

        for (auto h : handles) {
            auto& e = s.entity(h);

            if (!e.visible || !e.light)
                continue;
            if (lights.size() >= settings.maxLights)
                throw std::runtime_error(
                    "Scene exceeds maxLights; raise the budget (up to 32) or remove lights");
            auto light = *e.light;

            if (light.intensity < 0 || light.range <= .05f ||
                glm::any(glm::lessThan(light.color, glm::vec3(0))) || light.innerCone < 0 ||
                light.outerCone <= light.innerCone || light.outerCone >= glm::half_pi<float>())
                throw std::invalid_argument("Invalid light properties");

            auto direction = glm::normalize(e.transform.rotation * glm::vec3(0, 0, -1));
            lights.push_back({light, e.transform.position, direction});
            state.emplace_back(e.transform.position, light.intensity);
            state.emplace_back(direction, light.range);
            state.emplace_back(light.color, float(light.type));
            state.emplace_back(light.innerCone, light.outerCone, light.castsShadow ? 1 : 0, 0);
        }

        if (state != lastLightState) {
            historyValid = false;
            lastLightState = std::move(state);
        }

        stats.lights = uint32_t(lights.size());
        int layer = 0;
        Aabb bounds;

        for (auto& b : worldBounds)
            bounds.expand(b);
        glm::vec3 center = worldBounds.empty() ? glm::vec3(0) : (bounds.min + bounds.max) * .5f;
        float radius =
            worldBounds.empty() ? 10.f : std::max(2.f, glm::length(bounds.max - bounds.min) * .5f);
        glm::vec3 cubeDirections[6] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                       {0, -1, 0}, {0, 0, 1},  {0, 0, -1}},
                  cubeUp[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1},
                               {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};

        for (auto& l : lights) {
            int needed = l.light.type == LightType::Point ? 6 : 1;

            if (!settings.shadows || !l.light.castsShadow)
                continue;
            if (layer + needed > int(settings.maxShadowMaps))
                throw std::runtime_error(
                    "Shadow layer budget exceeded (point lights use six layers); raise "
                    "maxShadowMaps or disable a light's shadows");
            l.shadowBase = layer;
            l.shadowCount = needed;

            if (l.light.type == LightType::Directional) {
                auto up = std::abs(l.direction.y) < .99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                auto view = glm::lookAt(center - l.direction * radius * 2.f, center, up);
                Aabb lightBounds;
                for (const auto& b : worldBounds)
                    lightBounds.expand(b.transformed(view));

                // fit light space bounds and snap coverage to shadow texels
                if (worldBounds.empty()) {
                    lightBounds.min = {-radius, -radius, -radius * 4};
                    lightBounds.max = {radius, radius, -.1f};
                }
                auto extent =
                    glm::max(glm::vec2(lightBounds.max - lightBounds.min), glm::vec2(.1f));
                auto texel = extent / float(settings.shadowResolution - 4);
                auto midpoint = glm::vec2(lightBounds.min + lightBounds.max) * .5f;
                midpoint = glm::floor(midpoint / texel + .5f) * texel;
                auto halfExtent = extent * .5f + texel * 2.f;
                float nearPlane = std::max(.01f, -lightBounds.max.z - .5f);
                float farPlane = std::max(nearPlane + .1f, -lightBounds.min.z + .5f);
                shadowMatrices[layer] =
                    glm::ortho(midpoint.x - halfExtent.x, midpoint.x + halfExtent.x,
                               midpoint.y - halfExtent.y, midpoint.y + halfExtent.y, nearPlane,
                               farPlane) *
                    view;
            } else if (l.light.type == LightType::Spot) {
                auto up = std::abs(l.direction.y) < .99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                shadowMatrices[layer] =
                    glm::perspective(l.light.outerCone * 2, 1.f, .05f, l.light.range) *
                    glm::lookAt(l.position, l.position + l.direction, up);
            } else
                for (int f = 0; f < 6; ++f)
                    shadowMatrices[layer + f] =
                        glm::perspective(glm::half_pi<float>(), 1.f, .05f, l.light.range) *
                        glm::lookAt(l.position, l.position + cubeDirections[f], cubeUp[f]);
            layer += needed;
        }

        int count = std::max(1, layer);
        GLint maxLayers;
        glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxLayers);

        if (count > maxLayers)
            throw std::runtime_error("Shadow maps exceed GPU array capacity");

        uint64_t bytes =
            uint64_t(settings.shadowResolution) * settings.shadowResolution * count * 4;

        if (allocatedShadowResolution != settings.shadowResolution ||
            allocatedShadowLayers != count) {
            if (memory() - shadowMaps.bytes + bytes > settings.memoryBudgetBytes)
                throw std::runtime_error("Shadow allocation exceeds GPU budget");

            shadowMaps = Texture(GL_TEXTURE_2D_ARRAY);
            glBindTexture(GL_TEXTURE_2D_ARRAY, shadowMaps.id);
            glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F, settings.shadowResolution,
                         settings.shadowResolution, count, 0, GL_DEPTH_COMPONENT, GL_FLOAT,
                         nullptr);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE,
                            GL_COMPARE_REF_TO_TEXTURE);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
            shadowMaps.bytes = bytes;
            allocatedShadowResolution = settings.shadowResolution;
            allocatedShadowLayers = count;
        }
    }

    void bindMaterials(Program& p) {
        p.sampler("materials", materials.texture, 0);
        const char* names[] = {"baseMaps", "normalMaps", "mrMaps", "aoMaps", "emissiveMaps"};

        for (int i = 0; i < 5; ++i)
            p.sampler(names[i], arrays[i], i + 1);
    }

    void bindCommon(Program& p, const Camera& camera, const glm::mat4& vp) {
        bindMaterials(p);
        p.sampler("environmentMap", environmentMap, 6);
        p.sampler("irradianceMap", irradianceMap, 7);
        p.sampler("prefilterMap", prefilterMap, 8);
        p.sampler("brdfMap", brdfMap, 9);
        p.sampler("shadowMaps", shadowMaps, 10);
        p.set("cameraPosition", camera.transform.position);
        p.set("viewProjection", vp);
        p.set("invViewProjection", glm::inverse(vp));
        p.set("previousViewProjection", previousVP);
        p.set("environmentIntensity", env.intensity);
        p.set("lightCount", int(lights.size()));

        for (size_t i = 0; i < lights.size(); ++i) {
            auto& l = lights[i];
            auto index = "[" + std::to_string(i) + "]";
            p.set("lightPositions" + index, l.position);
            p.set("lightDirections" + index, l.direction);
            p.set("lightColors" + index, l.light.color * l.light.intensity);
            p.set("lightParameters" + index,
                  glm::vec4(float(l.light.type), l.light.range, std::cos(l.light.innerCone),
                            std::cos(l.light.outerCone)));
            p.set("lightShadowBase" + index, l.shadowBase);
            p.set("lightShadowCount" + index, l.shadowCount);
        }

        for (int i = 0; i < 16; ++i)
            p.set("shadowMatrices[" + std::to_string(i) + "]", shadowMatrices[i]);
    }

    void bindGbuffer(Program& p) {
        p.sampler("normalRoughness", gbuffer.colors[0], 11);
        p.sampler("baseMetallic", gbuffer.colors[1], 12);
        p.sampler("emissionAO", gbuffer.colors[2], 13);
        p.sampler("depthMap", gbuffer.depth, 14);
    }

    void drawEntity(Program& p, const Scene& s, EntityHandle h, bool motion) {
        auto& e = s.entity(h);
        auto& m = s.material(e.material);
        p.set("model", e.transform.matrix());
        p.set("materialIndex", int(e.material.index));

        if (motion)
            p.set("previousModel",
                  previousModels.contains(key(h)) ? previousModels[key(h)] : e.transform.matrix());

        if (m.doubleSided)
            glDisable(GL_CULL_FACE);
        else
            glEnable(GL_CULL_FACE);
        glFrontFace(glm::determinant(glm::mat3(e.transform.matrix())) < 0 ? GL_CW : GL_CCW);
        meshes[e.mesh.index]->draw();
    }

    void stamp(int i, bool end = false) {
        if (!recording || i == 0)
            return;

        if (end)
            glEndQuery(GL_TIME_ELAPSED);
        else
            glBeginQuery(GL_TIME_ELAPSED, timers[timerIndex].queries[i * 2]);
    }

    void beginTimers() {
        timerIndex = frameIndex % 3;
        auto& t = timers[timerIndex];

        if (t.pending) {
            GLint available = 0;
            glGetQueryObjectiv(t.queries[12], GL_QUERY_RESULT_AVAILABLE, &available);

            if (available) {
                std::array<double, 7> ms{};

                for (int i = 1; i < 7; ++i) {
                    GLuint64 ns;
                    glGetQueryObjectui64v(t.queries[i * 2], GL_QUERY_RESULT, &ns);
                    ms[i] = ns * 1e-6;
                    ms[0] += ms[i];
                }

                stats.gpuMilliseconds = ms[0];
                stats.shadowMilliseconds = ms[1];
                stats.geometryMilliseconds = ms[2];
                stats.rayMilliseconds = ms[3];
                stats.filterMilliseconds = ms[4];
                stats.lightingMilliseconds = ms[5];
                stats.compositeMilliseconds = ms[6];
                stats.gpuTimingsValid = ms[0] > 0;
                t.pending = false;
            }
        }

        recording = elapsedTimers && !t.pending;
    }

    void render(const Scene& s, const Camera& camera, int w, int h) {
        auto start = std::chrono::steady_clock::now();
        glDisable(GL_FRAMEBUFFER_SRGB);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glCullFace(GL_BACK);
        glClearDepth(1.);

        if (scene != &s) {
            meshes.clear();
            packedBlas.clear();
            packedTriangles.clear();
            previousModels.clear();
            lastMembership.clear();
            topLevel.build({});
            materialRevision = 0;
            scene = &s;
            historyValid = false;
        }

        resize(w, h);
        environmentInit();
        uploadMeshes(s);

        if (materialRevision != s.materialRevision())
            uploadMaterials(s);
        auto handles = s.entities();
        prepareInstances(s, handles);
        prepareLights(s, handles);
        budget();
        stats.gpuAllocatedBytes = memory();
        auto vp = camera.projection(float(w) / h) * camera.view();

        if (glm::length(camera.transform.position - previousCamera.transform.position) > 2 ||
            std::abs(glm::dot(camera.transform.rotation, previousCamera.transform.rotation)) <
                .85f ||
            std::abs(camera.verticalFov - previousCamera.verticalFov) > 1e-5f)
            historyValid = false;

        if (!historyValid)
            previousVP = vp;
        beginTimers();
        stamp(1);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glViewport(0, 0, settings.shadowResolution, settings.shadowResolution);
        glEnable(GL_DEPTH_TEST);
        shadow.use();
        bindMaterials(shadow);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.5f, 2.f);

        for (auto& l : lights)
            for (int f = 0; f < l.shadowCount; ++f) {
                int layer = l.shadowBase + f;
                glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadowMaps.id, 0,
                                          layer);

                if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                    throw std::runtime_error("Shadow framebuffer incomplete");

                glClear(GL_DEPTH_BUFFER_BIT);
                shadow.set("shadowMatrix", shadowMatrices[layer]);

                for (auto handle : handles) {
                    auto& e = s.entity(handle);

                    if (e.visible && e.mesh && s.material(e.material).alphaMode != AlphaMode::Blend)
                        drawEntity(shadow, s, handle, false);
                }
            }

        glDisable(GL_POLYGON_OFFSET_FILL);
        stamp(1, true);
        stamp(2);
        gbuffer.bind();
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        geometry.use();
        bindMaterials(geometry);
        geometry.set("viewProjection", vp);
        geometry.set("previousViewProjection", previousVP);

        for (auto handle : handles) {
            auto& e = s.entity(handle);

            if (e.visible && e.mesh && s.material(e.material).alphaMode != AlphaMode::Blend)
                drawEntity(geometry, s, handle, true);
        }

        stamp(2, true);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glFrontFace(GL_CCW);
        stamp(3);
        rawRays.bind();
        glClear(GL_COLOR_BUFFER_BIT);
        ray.use();
        bindCommon(ray, camera, vp);
        bindGbuffer(ray);
        ray.sampler("blasNodes", blasNodes.texture, 17);
        ray.sampler("triangles", triangles.texture, 18);
        ray.sampler("tlasNodes", tlasNodes.texture, 19);
        ray.sampler("instances", instances.texture, 20);
        ray.sampler("instanceOrder", instanceOrder.texture, 21);
        ray.set("instanceCount", int(rayInstances.size()));
        ray.set("frameIndex", frameIndex);
        ray.set("useReflections", int(settings.reflections));
        ray.set("useGI", int(settings.globalIllumination));

        if (settings.reflections || settings.globalIllumination)
            fullScreen();
        stamp(3, true);
        stamp(4);

        if (settings.reflections || settings.globalIllumination) {
            auto& current = histories[historyIndex];
            auto& old = histories[1 - historyIndex];
            current.bind();
            temporal.use();
            temporal.sampler("currentReflection", rawRays.colors[0], 0);
            temporal.sampler("currentGI", rawRays.colors[1], 1);
            temporal.sampler("historyReflection", old.colors[0], 2);
            temporal.sampler("historyGI", old.colors[1], 3);
            temporal.sampler("currentNormal", gbuffer.colors[0], 4);
            temporal.sampler("currentDepth", gbuffer.depth, 5);
            temporal.sampler("motionMap", gbuffer.colors[3], 6);
            temporal.sampler("historyNormal", old.colors[2], 7);
            temporal.sampler("historyDepth", old.colors[3], 8);
            temporal.set("historyValid", int(historyValid));
            temporal.set("historyWeight", settings.historyWeight);
            fullScreen();
            filter.use();
            filter.sampler("normalRoughness", gbuffer.colors[0], 2);
            filter.sampler("depthMap", gbuffer.depth, 3);

            for (int pass = 0; pass < 2; ++pass) {
                auto& source = pass == 0 ? current : filters[0];
                filters[pass].bind();
                filter.sampler("reflectionMap", source.colors[0], 0);
                filter.sampler("giMap", source.colors[1], 1);
                filter.set("stepWidth", pass == 0 ? 1.f : 2.f);
                fullScreen();
            }
        }

        stamp(4, true);
        stamp(5);
        hdr.bind();
        lighting.use();
        bindCommon(lighting, camera, vp);
        bindGbuffer(lighting);
        lighting.sampler("reflectionMap", filters[1].colors[0], 15);
        lighting.sampler("giMap", filters[1].colors[1], 16);
        lighting.set("useReflections", int(settings.reflections));
        lighting.set("useGI", int(settings.globalIllumination));
        fullScreen();
        stamp(5, true);
        stamp(6);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        std::vector<EntityHandle> blended;

        for (auto handle : handles) {
            auto& e = s.entity(handle);

            if (e.visible && e.mesh && s.material(e.material).alphaMode == AlphaMode::Blend)
                blended.push_back(handle);
        }

        std::sort(blended.begin(), blended.end(), [&](auto a, auto b) {
            auto pa = glm::vec3(camera.view() * glm::vec4(s.entity(a).transform.position, 1)),
                 pb = glm::vec3(camera.view() * glm::vec4(s.entity(b).transform.position, 1));
            return pa.z < pb.z;
        });
        transparent.use();
        bindCommon(transparent, camera, vp);

        for (auto handle : blended)
            drawEntity(transparent, s, handle, false);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glFrontFace(GL_CCW);
        output.bind();
        present.use();
        present.sampler("hdrMap", hdr.colors[0], 0);
        present.set("exposure", settings.exposure);
        present.set("useFXAA", int(settings.fxaa));
        fullScreen();
        glBindFramebuffer(GL_READ_FRAMEBUFFER, output.fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, w, h);
        stamp(6, true);
        stamp(0, true);

        if (recording)
            timers[timerIndex].pending = true;
        previousModels.clear();

        for (auto handle : handles)
            previousModels[key(handle)] = s.entity(handle).transform.matrix();
        previousVP = vp;
        previousCamera = camera;
        historyValid = true;
        historyIndex = 1 - historyIndex;
        ++frameIndex;
        GLenum error = glGetError();

        if (error != GL_NO_ERROR) {
            std::ostringstream message;
            message << "OpenGL error 0x" << std::hex << error;
            throw std::runtime_error(message.str());
        }

        stats.cpuMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
    }
};

Renderer::Renderer(ProcLoader loader, const RenderSettings& settings) {
    validateSettings(settings);

    if (!loader || !gladLoadGLLoader(reinterpret_cast<GLADloadproc>(loader)))
        throw std::runtime_error("Cannot load OpenGL functions");

    GLint major, minor;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);

    if (major < 4 || (major == 4 && minor < 1))
        throw std::runtime_error("OpenGL 4.1 core is required");

    impl_ = std::make_unique<Impl>(settings);
}

Renderer::~Renderer() = default;

void Renderer::render(const Scene& s, const Camera& c, uint32_t w, uint32_t h) {
    if (w == 0 || h == 0)
        return;

    if (!std::isfinite(c.verticalFov) || c.verticalFov <= 0 || c.verticalFov >= glm::pi<float>() ||
        !std::isfinite(c.nearPlane) || !std::isfinite(c.farPlane) || c.nearPlane <= 0 ||
        c.farPlane <= c.nearPlane)
        throw std::invalid_argument("Invalid camera projection");

    auto cameraMatrix = c.transform.matrix();

    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if (!std::isfinite(cameraMatrix[i][j]))
                throw std::invalid_argument("Camera transform must be finite");

    if (std::abs(glm::determinant(cameraMatrix)) < 1e-8f)
        throw std::invalid_argument("Camera transform must be invertible");

    if (w > INT_MAX || h > INT_MAX)
        throw std::invalid_argument("Viewport dimensions overflow");

    impl_->render(s, c, int(w), int(h));
}

void Renderer::setSettings(const RenderSettings& s) {
    validateSettings(s);
    auto old = impl_->settings;

    if (old.materialTextureResolution != s.materialTextureResolution)
        impl_->materialRevision = 0;
    impl_->settings = s;

    if (old.reflections != s.reflections || old.globalIllumination != s.globalIllumination ||
        old.shadows != s.shadows || old.rayResolutionScale != s.rayResolutionScale ||
        old.shadowResolution != s.shadowResolution || old.maxShadowMaps != s.maxShadowMaps)
        impl_->historyValid = false;
}

const RenderSettings& Renderer::settings() const {
    return impl_->settings;
}

void Renderer::setEnvironment(const Environment& e) {
    if (e.intensity < 0 || !std::isfinite(e.intensity) ||
        (!e.pixels.empty() &&
         (!e.width || !e.height || e.pixels.size() != size_t(e.width) * e.height)))
        throw std::invalid_argument("Invalid environment map");

    GLint max;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max);

    if (e.width > uint32_t(max) || e.height > uint32_t(max))
        throw std::invalid_argument("Environment exceeds GPU texture size");

    impl_->env = e;
    impl_->environmentDirty = true;
    impl_->historyValid = false;
}

void Renderer::resetHistory() {
    impl_->historyValid = false;
}

const FrameStats& Renderer::stats() const {
    return impl_->stats;
}

const std::string& Renderer::deviceDescription() const {
    return impl_->device;
}

std::vector<uint8_t> Renderer::readback() const {
    int w = impl_->width, h = impl_->height;

    if (!w || !h)
        return {};
    std::vector<uint8_t> pixels(size_t(w) * h * 3), out(pixels.size());
    GLint previous;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, impl_->output.fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(previous));

    for (int y = 0; y < h; ++y)
        std::memcpy(out.data() + size_t(y) * w * 3, pixels.data() + size_t(h - 1 - y) * w * 3,
                    size_t(w) * 3);
    return out;
}
} // namespace rt
