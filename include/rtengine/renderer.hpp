#pragma once

#include "scene.hpp"
#include <functional>

namespace rt {
struct RenderSettings {
    bool reflections = true, globalIllumination = true, shadows = true, fxaa = true;
    float rayResolutionScale = .5f, exposure = 1, historyWeight = .9f;
    uint32_t maxLights = 32, maxShadowMaps = 8, shadowResolution = 2048,
             materialTextureResolution = 512;
    uint64_t memoryBudgetBytes = 512ull * 1024 * 1024;
};

struct FrameStats {
    double cpuMilliseconds = 0, gpuMilliseconds = 0;
    double geometryMilliseconds = 0, shadowMilliseconds = 0, lightingMilliseconds = 0,
           rayMilliseconds = 0, filterMilliseconds = 0, compositeMilliseconds = 0;
    uint64_t gpuAllocatedBytes = 0;
    uint32_t triangles = 0, instances = 0, lights = 0;
    bool gpuTimingsValid = false;
};

struct Environment {
    // linear rgb equirectangular north pole first row empty uses sky
    uint32_t width = 0, height = 0;
    std::vector<glm::vec3> pixels;
    glm::vec3 zenith{.18f, .28f, .5f}, horizon{.55f, .6f, .65f}, ground{.06f, .055f, .05f};
    float intensity = 1;
};

class Renderer {
  public:
    using ProcAddress = void (*)();
    using ProcLoader = ProcAddress (*)(const char*);
    explicit Renderer(ProcLoader, const RenderSettings& = {});
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void render(const Scene&, const Camera&, uint32_t width, uint32_t height);
    void setSettings(const RenderSettings&);
    const RenderSettings& settings() const;
    void setEnvironment(const Environment&);
    void resetHistory();
    const FrameStats& stats() const;
    const std::string& deviceDescription() const;
    // top to bottom rgb bytes requires owning gl context
    std::vector<uint8_t> readback() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace rt
