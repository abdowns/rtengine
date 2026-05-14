#include "camera_input.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <iostream>
#include <rtengine/assets.hpp>
#include <rtengine/renderer.hpp>

using namespace rt;

namespace {
struct Demo {
    Scene scene;
    AssetLoader loader{scene};
    Camera camera;

    Demo() {
        auto add = [&](std::string name, MeshHandle mesh, MaterialHandle mat, glm::vec3 position,
                       glm::vec3 scale = glm::vec3(1)) {
            auto& e = scene.entity(scene.createEntity(name));
            e.mesh = mesh;
            e.material = mat;
            e.transform.position = position;
            e.transform.scale = scale;
        };

        auto box = scene.addMesh(makeBox());
        Material floor;
        floor.baseColor = {.55, .56, .58, 1};
        floor.roughness = .32f;
        auto gray = scene.addMaterial(floor);
        Material red;
        red.baseColor = {.7, .025, .025, 1};
        red.roughness = .8f;
        auto redMat = scene.addMaterial(red);
        Material blue = red;
        blue.baseColor = {.02, .06, .7, 1};
        auto blueMat = scene.addMaterial(blue);
        Material metal;
        metal.baseColor = {.8, .86, .95, 1};
        metal.metallic = 1;
        metal.roughness = .12f;
        auto metalMat = scene.addMaterial(metal);
        Material glass;
        glass.baseColor = {.08, .7, .75, .32};
        glass.roughness = .18f;
        glass.alphaMode = AlphaMode::Blend;
        glass.doubleSided = true;
        auto glassMat = scene.addMaterial(glass);
        add("ground", box, gray, {0, -.5f, 0}, {14, 1, 14});
        add("red bounce wall", box, redMat, {-6, 2, 0}, {.25f, 4, 10});
        add("blue bounce wall", box, blueMat, {6, 2, 0}, {.25f, 4, 10});
        add("back wall", box, gray, {0, 2, -5}, {12, 4, .25f});
        auto sphere = scene.addMesh(makeSphere(.7f, 48, 32));
        add("reflective sphere", sphere, metalMat, {1.8f, 1.1f, -.3f});
        add("transparent panel", box, glassMat, {2, 1.6f, 2.4f}, {2.5f, 2.8f, .08f});
        add("red box", box, redMat, {-1.5f, .5f, -1.5f});
        add("tall box", box, gray, {.5f, 1, -2}, {1, 2, 1});

        for (auto h : loader.import(std::string(RT_DEMO_ASSET_DIR) + "/crystal.gltf").entities) {
            (void)h;
        }

        auto obj = loader.import(std::string(RT_DEMO_ASSET_DIR) + "/wedge.obj");

        for (auto h : obj.entities)
            scene.entity(h).transform.position = {-1, 0, 2};

        auto light = [&](std::string name, Light l, glm::vec3 p,
                         glm::quat rotation = glm::quat(1, 0, 0, 0)) {
            auto h = scene.createEntity(name);
            auto& e = scene.entity(h);
            e.light = l;
            e.transform.position = p;
            e.transform.rotation = rotation;
        };

        Light sun;
        sun.type = LightType::Directional;
        sun.color = {1, .9f, .76f};
        sun.intensity = 2;
        sun.castsShadow = true;
        light("sun", sun, {0, 7, 0},
              glm::quat(glm::vec3(glm::radians(-50.f), glm::radians(-30.f), 0)));
        Light point;
        point.color = {1, .18f, .06f};
        point.intensity = 35;
        point.range = 15;
        point.castsShadow = true;
        light("red light", point, {-3, 3, 1});
        point.castsShadow = false;
        point.color = {.06f, .2f, 1};
        point.intensity = 25;
        light("blue light", point, {3, 2, -2});

        for (int i = 0; i < 5; ++i) {
            point.color = {.4f + float(i % 2) * .4f, .55f, .35f + float(i % 3) * .2f};
            point.intensity = 5;
            light("fill", point, {float(i) - 2, 3, -3.5f});
        }

        camera.transform.position = {9, 6, 11};
        camera.transform.rotation = glm::quatLookAt(
            glm::normalize(glm::vec3(0, 1, 0) - camera.transform.position), glm::vec3(0, 1, 0));
    }
};
} // namespace
int main() {
    GLFWwindow* window = nullptr;
    try {
        glfwSetErrorCallback([](int code, const char* text) {
            std::cerr << "GLFW " << code << ": " << text << '\n';
        });

        if (!glfwInit())
            throw std::runtime_error("GLFW initialization failed");

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        window = glfwCreateWindow(1600, 900, "rtengine demo", nullptr, nullptr);

        if (!window)
            throw std::runtime_error("OpenGL 4.1 context creation failed");

        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);
        {
            Demo demo;
            RenderSettings settings;
            Renderer renderer(glfwGetProcAddress, settings);
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGui::GetIO().IniFilename = nullptr;
            ImGui::StyleColorsDark();
            ImGui_ImplGlfw_InitForOpenGL(window, true);
            ImGui_ImplOpenGL3_Init("#version 410 core");
            CameraController cameraController;
            GlfwCameraInput cameraInput(window);
            auto previous = std::chrono::steady_clock::now();

            while (!glfwWindowShouldClose(window)) {
                auto start = std::chrono::steady_clock::now();
                double dt = std::chrono::duration<double>(start - previous).count();
                previous = start;
                dt = std::min(dt, .1);
                glfwPollEvents();

                if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
                    glfwSetWindowShouldClose(window, 1);

                auto input = cameraInput.poll(!ImGui::GetIO().WantCaptureKeyboard,
                                              !ImGui::GetIO().WantCaptureMouse);
                cameraController.update(demo.camera, input, float(dt));
                int width, height;
                glfwGetFramebufferSize(window, &width, &height);

                if (width <= 0 || height <= 0) {
                    glfwWaitEventsTimeout(.05);
                    continue;
                }

                renderer.render(demo.scene, demo.camera, uint32_t(width), uint32_t(height));
                auto stats = renderer.stats();
                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();
                ImGui::Begin("rtengine");
                const float framerate = ImGui::GetIO().Framerate;
                ImGui::Text("FPS %.1f | Frame %.2f ms", framerate, 1000.f / framerate);
                ImGui::Text("WASD: move | Q/E: down/up | Shift: sprint");
                ImGui::Text("Hold right mouse: look around");

                if (ImGui::CollapsingHeader("Camera controls")) {
                    auto& controls = cameraController.settings;
                    ImGui::SliderFloat("Move speed", &controls.movementSpeed, .1f, 30.f);
                    ImGui::SliderFloat("Sprint multiplier", &controls.sprintMultiplier, 1.f, 10.f);
                    ImGui::SliderFloat("Mouse sensitivity", &controls.mouseSensitivity, .0005f,
                                       .01f, "%.4f");
                    ImGui::Checkbox("Invert mouse Y", &controls.invertY);
                    ImGui::Checkbox("Use world up for Q/E", &controls.worldUpMovement);
                }

                ImGui::Text("%u triangles | %u instances | %u lights", stats.triangles,
                            stats.instances, stats.lights);
                ImGui::Text("CPU %.2f ms | GPU %.2f ms%s", stats.cpuMilliseconds,
                            stats.gpuMilliseconds, stats.gpuTimingsValid ? "" : " (pending)");
                ImGui::Text("GPU allocations %.1f MiB", stats.gpuAllocatedBytes / 1048576.);
                ImGui::Text("Shadow %.2f | Geometry %.2f", stats.shadowMilliseconds,
                            stats.geometryMilliseconds);
                ImGui::Text("Rays %.2f | Filtering %.2f", stats.rayMilliseconds,
                            stats.filterMilliseconds);
                ImGui::Text("Lighting %.2f | Composite %.2f", stats.lightingMilliseconds,
                            stats.compositeMilliseconds);
                bool changed = ImGui::Checkbox("Ray-traced reflections", &settings.reflections);
                changed |= ImGui::Checkbox("One-bounce GI", &settings.globalIllumination);
                changed |= ImGui::Checkbox("Shadows", &settings.shadows);
                changed |= ImGui::Checkbox("FXAA", &settings.fxaa);
                const char* shadowSizes[] = {"512", "1024", "2048"};
                int shadowQuality = settings.shadowResolution <= 512    ? 0
                                    : settings.shadowResolution <= 1024 ? 1
                                                                        : 2;
                if (ImGui::Combo("Shadow resolution", &shadowQuality, shadowSizes, 3)) {
                    settings.shadowResolution = 512u << shadowQuality;
                    changed = true;
                }

                changed |=
                    ImGui::SliderFloat("Ray resolution", &settings.rayResolutionScale, .125f, 1.f);
                changed |= ImGui::SliderFloat("Exposure", &settings.exposure, .1f, 3.f);

                if (changed)
                    renderer.setSettings(settings);

                if (ImGui::Button("Reset lighting history"))
                    renderer.resetHistory();
                ImGui::End();
                ImGui::Render();
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                glfwSwapBuffers(window);
            }

            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
        }

        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "rtengine: " << e.what() << '\n';

        if (window)
            glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
}
