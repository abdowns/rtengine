#include <rtengine/assets.hpp>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#define STB_IMAGE_IMPLEMENTATION
#include <functional>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <stb_image.h>
#include <stdexcept>
#include <unordered_map>

namespace rt {

namespace {
Transform decompose(const glm::mat4& m) {
    Transform t;
    glm::vec3 skew;
    glm::vec4 perspective;

    if (!glm::decompose(m, t.scale, t.rotation, t.position, skew, perspective) ||
        glm::length(skew) > 1e-4f)
        throw std::runtime_error("Sheared or singular glTF node transforms are unsupported");

    t.rotation = glm::normalize(t.rotation);
    return t;
}
} // namespace
struct AssetLoader::Impl {
    Scene& scene;

    struct Part {
        std::string name;
        Transform transform;
        MeshHandle mesh;
        MaterialHandle material;
    };

    struct Cached {
        std::vector<Part> parts;
        std::vector<std::string> warnings;
    };

    std::unordered_map<std::string, Cached> models;
    std::unordered_map<std::string, std::shared_ptr<const TextureData>> textures;

    explicit Impl(Scene& s) : scene(s) {}
    std::shared_ptr<const TextureData> fileTexture(const std::filesystem::path& p) {
        auto key = p.string();

        if (auto it = textures.find(key); it != textures.end())
            return it->second;
        int w, h, c;
        auto pixels = stbi_load(key.c_str(), &w, &h, &c, 4);

        if (!pixels)
            throw std::runtime_error("Cannot load image " + key + ": " + stbi_failure_reason());

        auto t = std::make_shared<TextureData>();
        t->width = w;
        t->height = h;
        t->name = key;
        t->rgba.assign(pixels, pixels + size_t(w) * h * 4);
        stbi_image_free(pixels);
        return textures[key] = t;
    }

    Cached gltf(const std::filesystem::path& path) {
        cgltf_options options{};
        cgltf_data* raw = nullptr;

        if (cgltf_parse_file(&options, path.string().c_str(), &raw) != cgltf_result_success)
            throw std::runtime_error("glTF parse failed: " + path.string());
        std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, cgltf_free);

        if (cgltf_load_buffers(&options, data.get(), path.string().c_str()) !=
            cgltf_result_success)
            throw std::runtime_error("glTF buffer load failed: " + path.string());

        Cached out;

        if (data->animations_count || data->skins_count)
            out.warnings.push_back("Animation/skinning omitted; only rigid meshes are supported");
        auto image = [&](const cgltf_texture_view& v) -> std::shared_ptr<const TextureData> {
            if (!v.texture || !v.texture->image)
                return {};

            auto uri = v.texture->image->uri;

            if (!uri || std::string_view(uri).rfind("data:", 0) == 0)
                throw std::runtime_error("Embedded glTF images are unsupported");

            return fileTexture(path.parent_path() / uri);
        };

        std::vector<MaterialHandle> mats;

        for (size_t i = 0; i < data->materials_count; ++i) {
            auto& src = data->materials[i];
            Material m;
            m.name = src.name ? src.name : "material";

            if (src.has_pbr_metallic_roughness) {
                auto& p = src.pbr_metallic_roughness;
                m.baseColor = glm::make_vec4(p.base_color_factor);
                m.metallic = p.metallic_factor;
                m.roughness = p.roughness_factor;
                m.baseColorMap = image(p.base_color_texture);
                m.metallicRoughnessMap = image(p.metallic_roughness_texture);
            }

            m.normalMap = image(src.normal_texture);
            m.normalScale = src.normal_texture.scale;
            m.occlusionMap = image(src.occlusion_texture);
            m.occlusionStrength = src.occlusion_texture.scale;
            m.emissive = glm::make_vec3(src.emissive_factor);
            m.emissiveMap = image(src.emissive_texture);
            m.alphaMode = src.alpha_mode == cgltf_alpha_mode_mask    ? AlphaMode::Mask
                          : src.alpha_mode == cgltf_alpha_mode_blend ? AlphaMode::Blend
                                                                     : AlphaMode::Opaque;
            m.alphaCutoff = src.alpha_cutoff;
            m.doubleSided = src.double_sided;
            mats.push_back(scene.addMaterial(std::move(m)));
        }

        Material defaultMat;
        defaultMat.metallic = 1;
        defaultMat.roughness = 1;
        auto fallback = scene.addMaterial(defaultMat);
        std::function<void(cgltf_node*)> visit = [&](cgltf_node* node) {
            if (node->mesh) {
                float matrix[16];
                cgltf_node_transform_world(node, matrix);
                auto transform = decompose(glm::make_mat4(matrix));

                for (size_t p = 0; p < node->mesh->primitives_count; ++p) {
                    auto& primitive = node->mesh->primitives[p];

                    if (primitive.type != cgltf_primitive_type_triangles)
                        throw std::runtime_error(
                            "Only triangle-list glTF primitives are supported");

                    cgltf_accessor *position = nullptr, *normal = nullptr, *uv = nullptr;

                    for (size_t a = 0; a < primitive.attributes_count; ++a) {
                        auto& attr = primitive.attributes[a];

                        if (attr.type == cgltf_attribute_type_position)
                            position = attr.data;

                        if (attr.type == cgltf_attribute_type_normal)
                            normal = attr.data;

                        if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0)
                            uv = attr.data;
                    }

                    if (!position)
                        throw std::runtime_error("glTF primitive missing POSITION");

                    Mesh m;
                    m.name = node->mesh->name ? node->mesh->name : "gltf mesh";
                    m.vertices.resize(position->count);

                    for (size_t v = 0; v < m.vertices.size(); ++v) {
                        auto& dst = m.vertices[v];
                        cgltf_accessor_read_float(position, v, glm::value_ptr(dst.position), 3);

                        if (normal)
                            cgltf_accessor_read_float(normal, v, glm::value_ptr(dst.normal), 3);

                        if (uv)
                            cgltf_accessor_read_float(uv, v, glm::value_ptr(dst.uv), 2);
                    }

                    if (primitive.indices) {
                        m.indices.resize(primitive.indices->count);

                        for (size_t k = 0; k < m.indices.size(); ++k)
                            m.indices[k] =
                                uint32_t(cgltf_accessor_read_index(primitive.indices, k));
                    } else {
                        m.indices.resize(m.vertices.size());

                        for (size_t k = 0; k < m.indices.size(); ++k)
                            m.indices[k] = uint32_t(k);
                    }

                    generateNormalsAndTangents(m, !normal, true);
                    auto mat = primitive.material ? mats.at(primitive.material - data->materials)
                                                  : fallback;
                    out.parts.push_back({node->name ? node->name : "gltf node", transform,
                                         scene.addMesh(std::move(m)), mat});
                }
            }

            for (size_t i = 0; i < node->children_count; ++i)
                visit(node->children[i]);
        };

        if (data->scene)
            for (size_t n = 0; n < data->scene->nodes_count; ++n)
                visit(data->scene->nodes[n]);
        else
            for (size_t n = 0; n < data->nodes_count; ++n)
                if (!data->nodes[n].parent)
                    visit(&data->nodes[n]);

        if (out.parts.empty())
            throw std::runtime_error("glTF contains no supported meshes");

        return out;
    }

    Cached obj(const std::filesystem::path& path) {
        tinyobj::ObjReader reader;
        tinyobj::ObjReaderConfig config;
        config.mtl_search_path = path.parent_path().string();
        config.triangulate = true;

        if (!reader.ParseFromFile(path.string(), config))
            throw std::runtime_error("OBJ import failed: " + reader.Error());

        Cached out;

        if (!reader.Warning().empty())
            out.warnings.push_back(reader.Warning());
        std::vector<MaterialHandle> mats;
        auto tex = [&](const std::string& name) {
            return name.empty() ? std::shared_ptr<const TextureData>{}
                                : fileTexture(path.parent_path() / name);
        };

        for (auto& src : reader.GetMaterials()) {
            Material m;
            m.name = src.name;
            m.baseColor = glm::vec4(glm::make_vec3(src.diffuse), 1);
            m.roughness = src.roughness;
            m.metallic = src.metallic;
            m.baseColorMap = tex(src.diffuse_texname);
            m.normalMap = tex(src.normal_texname);
            mats.push_back(scene.addMaterial(std::move(m)));
        }

        auto fallback = scene.addMaterial(Material{});
        auto& attrib = reader.GetAttrib();

        for (auto& shape : reader.GetShapes()) {
            Mesh mesh;
            mesh.name = shape.name;
            bool missingNormals = false;

            for (auto idx : shape.mesh.indices) {
                if (idx.vertex_index < 0 ||
                    size_t(idx.vertex_index) * 3 + 2 >= attrib.vertices.size())
                    throw std::runtime_error("Invalid OBJ vertex index");

                Vertex v;
                v.position = glm::make_vec3(&attrib.vertices[size_t(idx.vertex_index) * 3]);

                if (idx.normal_index >= 0)
                    v.normal = glm::make_vec3(&attrib.normals[size_t(idx.normal_index) * 3]);
                else
                    missingNormals = true;

                if (idx.texcoord_index >= 0)
                    v.uv = glm::make_vec2(&attrib.texcoords[size_t(idx.texcoord_index) * 2]);

                mesh.indices.push_back(uint32_t(mesh.vertices.size()));
                mesh.vertices.push_back(v);
            }

            int mat = shape.mesh.material_ids.empty() ? -1 : shape.mesh.material_ids[0];
            generateNormalsAndTangents(mesh, missingNormals, true);
            out.parts.push_back({shape.name,
                                 {},
                                 scene.addMesh(std::move(mesh)),
                                 mat >= 0 ? mats.at(mat) : fallback});
        }

        if (out.parts.empty())
            throw std::runtime_error("OBJ contains no meshes");

        return out;
    }
};

AssetLoader::AssetLoader(Scene& s) : impl_(std::make_unique<Impl>(s)) {}
AssetLoader::~AssetLoader() = default;
AssetLoader::AssetLoader(AssetLoader&&) noexcept = default;
AssetLoader& AssetLoader::operator=(AssetLoader&&) noexcept = default;

ImportResult AssetLoader::import(const std::filesystem::path& path) {
    auto key = std::filesystem::weakly_canonical(path).string();
    auto it = impl_->models.find(key);

    if (it == impl_->models.end()) {
        auto ext = path.extension().string();
        Impl::Cached cached;

        if (ext == ".gltf")
            cached = impl_->gltf(path);
        else if (ext == ".obj")
            cached = impl_->obj(path);
        else
            throw std::runtime_error("Unsupported mesh format: " + ext);

        it = impl_->models.emplace(key, std::move(cached)).first;
    }

    ImportResult result;
    result.warnings = it->second.warnings;

    for (auto& part : it->second.parts) {
        auto h = impl_->scene.createEntity(part.name);
        auto& e = impl_->scene.entity(h);
        e.transform = part.transform;
        e.mesh = part.mesh;
        e.material = part.material;
        result.entities.push_back(h);
    }

    return result;
}

std::shared_ptr<const TextureData> AssetLoader::loadTexture(const std::filesystem::path& p) {
    return impl_->fileTexture(p);
}

void AssetLoader::clearCache() {
    impl_->models.clear();
}
} // namespace rt
