#pragma once

#include "scene.hpp"
#include <filesystem>

namespace rt {
struct ImportResult {
    std::vector<EntityHandle> entities;
    std::vector<std::string> warnings;
};

class AssetLoader {
  public:
    explicit AssetLoader(Scene&);
    ~AssetLoader();
    AssetLoader(AssetLoader&&) noexcept;
    AssetLoader& operator=(AssetLoader&&) noexcept;
    ImportResult import(const std::filesystem::path&);
    std::shared_ptr<const TextureData> loadTexture(const std::filesystem::path&);
    void clearCache();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace rt
