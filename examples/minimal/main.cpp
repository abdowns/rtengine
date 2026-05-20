#include <iostream>
#include <rtengine/scene.hpp>

int main() {
    rt::Scene scene;
    auto mesh = scene.addMesh(rt::makeBox());
    auto material = scene.addMaterial(rt::Material{});
    auto entity = scene.createEntity("Reusable engine example");
    scene.entity(entity).mesh = mesh;
    scene.entity(entity).material = material;
    std::cout << scene.entity(entity).name << ": " << scene.mesh(mesh).indices.size() / 3
              << " triangles\n";
}
