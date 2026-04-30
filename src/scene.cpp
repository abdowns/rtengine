#include <glm/gtc/matrix_transform.hpp>
#include <rtengine/scene.hpp>

namespace rt {

glm::mat4 Transform::matrix() const {
    glm::mat4 m = glm::translate(glm::mat4(1), position);
    m *= glm::mat4_cast(rotation);
    return glm::scale(m, scale);
}

glm::mat4 Camera::view() const {
    return glm::inverse(transform.matrix());
}

glm::mat4 Camera::projection(float aspect) const {
    return glm::perspective(verticalFov, aspect, nearPlane, farPlane);
}

Entity& Scene::createEntity(std::string name) {
    auto& e = entities_.emplace_back();
    e.name = std::move(name);
    return e;
}

void Scene::destroyEntity(size_t index) {
    if (index < entities_.size())
        entities_.erase(entities_.begin() + index);
}

std::vector<Entity>& Scene::entities() {
    return entities_;
}

const std::vector<Entity>& Scene::entities() const {
    return entities_;
}
} // namespace rt
