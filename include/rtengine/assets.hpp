#pragma once

#include "scene.hpp"
#include <filesystem>

namespace rt {
struct ImportResult {
    std::vector<EntityHandle> entities;
};

class AssetLoader {
  public:
    explicit AssetLoader(Scene&);
    ImportResult import(const std::filesystem::path&);

  private:
    ImportResult importGltf(const std::filesystem::path&);
    ImportResult importObj(const std::filesystem::path&);

    Scene* scene_;
};
} // namespace rt
