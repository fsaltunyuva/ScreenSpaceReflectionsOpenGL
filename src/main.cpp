#include <array>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>

#include "utility.h"

#include <GLFW/glfw3.h>
#include <glm/ext.hpp> // for matrix calculation

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// Adjustable parameters for SSR in ImGui
static float gSSRStepSize = 0.25f;
static int gSSRMaxSteps = 80;
static float gSSRThickness = 0.5f;
static float gCamSpeed = 0.03f;
static int gDebugMode = 0; // 0 Normal Blend, 1 Reflection Only, 2 SSR Hit Visualization
static float gSSRBlendFactor = 0.8f;

void WindowPositionCallback(GLFWwindow* wnd, int x, int y)
{
    GLState& state = *static_cast<GLState*>(glfwGetWindowUserPointer(wnd));
    state.curWndParams.pos[0] = x;
    state.curWndParams.pos[1] = y;
}

void WindowSizeCallback(GLFWwindow* wnd, int x, int y)
{
    GLState& state = *static_cast<GLState*>(glfwGetWindowUserPointer(wnd));
    state.curWndParams.size[0] = x;
    state.curWndParams.size[1] = y;
}

void MouseMoveCallback(GLFWwindow* wnd, double x, double y)
{
    GLState& state = *static_cast<GLState*>(glfwGetWindowUserPointer(wnd));

    if (state.cam.isLeftButtonPressed) {
        // how much we moved
        float deltaX = (float)(x - state.cam.lastMousePos.x);
        float deltaY = (float)(y - state.cam.lastMousePos.y);
        state.cam.lastMousePos = glm::dvec2(x, y); // update

        float sensitivity = 0.002f;
        float yawAngle = -deltaX * sensitivity;
        float pitchAngle = -deltaY * sensitivity;

        // Pitch Quaternion
        glm::vec3 right = state.cam.orientation * glm::vec3(1, 0, 0);
        glm::quat pitchQuaternion = glm::angleAxis(pitchAngle, right);

        // Yaw Quaternion
        glm::vec3 up = glm::vec3(0, 1, 0);
        glm::quat yawQuaternion = glm::angleAxis(yawAngle, up);

        state.cam.orientation = yawQuaternion * pitchQuaternion * state.cam.orientation;
        state.cam.orientation = glm::normalize(state.cam.orientation);
    }
    else { // Update last mouse position even when not dragging to prevent jumps when starting to drag
        state.cam.lastMousePos = glm::dvec2(x, y);
    }
}

void MouseButtonCallback(GLFWwindow* wnd, int button, int action, int)
{
    GLState& state = *static_cast<GLState*>(glfwGetWindowUserPointer(wnd));

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) {
                return;
            }
            state.cam.isLeftButtonPressed = true;
            double x, y;
            glfwGetCursorPos(wnd, &x, &y);
            state.cam.lastMousePos = glm::dvec2(x, y);
        }
        else if (action == GLFW_RELEASE) {
            state.cam.isLeftButtonPressed = false;
        }
    }
}

void MouseScrollCallback(GLFWwindow*, double, double)
{
}

void FramebufferChangeCallback(GLFWwindow* wnd, int w, int h)
{
    GLState& state = *static_cast<GLState*>(glfwGetWindowUserPointer(wnd));
    state.curWndParams.fbSize[0] = w;
    state.curWndParams.fbSize[1] = h;
}

void KeyboardCallback(GLFWwindow* wnd, int key, int scancode, int action, int modifier)
{
    GLState& state = *static_cast<GLState*>(glfwGetWindowUserPointer(wnd));

    if (action != GLFW_RELEASE) return;

    if (key == GLFW_KEY_ESCAPE) {
        glfwSetWindowShouldClose(wnd, GLFW_TRUE);
    }

    if (key == GLFW_KEY_ENTER) {
        state.isFullscreen = !state.isFullscreen;
        if (state.isFullscreen) {
            GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);
            glfwSetWindowMonitor(wnd, primaryMonitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        }
        else {
            glfwSetWindowMonitor(wnd, nullptr, 200, 200, 1280, 720, GLFW_DONT_CARE);
        }
    }
}

// Helper function to read positions from obj file and compute bounding box
void getMeshBoundingBox(const std::string& path, glm::vec3& bbMin, glm::vec3& bbMax) {
    bbMin = glm::vec3(1e9f);
    bbMax = glm::vec3(-1e9f);
    std::ifstream file(path);

    if (!file) {
        std::cerr << "Cannot open mesh file: " << path << std::endl;
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.rfind("v ", 0) == 0) { // starts with "v "
            std::istringstream ss(line.substr(2));
            float x, y, z;
            if (ss >> x >> y >> z) {
                bbMin = glm::min(bbMin, glm::vec3(x, y, z));
                bbMax = glm::max(bbMax, glm::vec3(x, y, z));
            }
        }
    }
}

int main(int argc, const char* argv[])
{
    CallbackPointersGLFW callbacks;
    callbacks.mMoveCallback = MouseMoveCallback;
    callbacks.mButtonCallback = MouseButtonCallback;
    callbacks.mScrollCallback = MouseScrollCallback;
    callbacks.keyCallback = KeyboardCallback;
    callbacks.fboCallback = FramebufferChangeCallback;
    callbacks.winPosCallback = WindowPositionCallback;
    callbacks.winSizeCallback = WindowSizeCallback;

    GLState state = GLState("SSR Implementation", 1280, 720, callbacks);

    // Shaders
    ShaderGL cathedralVShader(ShaderGL::VERTEX, "shaders/cathedral.vert"); // shared vertex shader for both cathedral and floor
    ShaderGL cathedralFShader(ShaderGL::FRAGMENT, "shaders/cathedral.frag");
    GLuint cathedralPipeline;
    glGenProgramPipelines(1, &cathedralPipeline);
    glUseProgramStages(cathedralPipeline, GL_VERTEX_SHADER_BIT, cathedralVShader.shaderId);
    glUseProgramStages(cathedralPipeline, GL_FRAGMENT_SHADER_BIT, cathedralFShader.shaderId);

    ShaderGL floorFShader(ShaderGL::FRAGMENT, "shaders/floor.frag");
    GLuint floorPipeline;
    glGenProgramPipelines(1, &floorPipeline);
    glUseProgramStages(floorPipeline, GL_VERTEX_SHADER_BIT, cathedralVShader.shaderId);
    glUseProgramStages(floorPipeline, GL_FRAGMENT_SHADER_BIT, floorFShader.shaderId);

    // Textures
    TextureGL cathedralTex("sibenik/kamen.png", TextureGL::LINEAR, TextureGL::REPEAT);
    TextureGL floorTex("sibenik/mramor6x6.png", TextureGL::LINEAR, TextureGL::REPEAT);

    // Mesh
    std::cout << "Loading Sibenik Cathedral model." << std::endl;
    MeshGL sibenikMesh("sibenik/sibenik.obj");

    // Bounding Box computation to place floor and camera
    glm::vec3 bbMin, bbMax;
    getMeshBoundingBox("sibenik/sibenik.obj", bbMin, bbMax);

    glm::vec3 center = 0.5f * (bbMin + bbMax);
    
    state.cam.pos = glm::vec3(center.x, bbMin.y + 3.0f, bbMax.z - 5.0f);
    state.cam.orientation = glm::quat(1.0f, 0.0f, 0.45f, 0.0f);

    // a flat reflective plane for the floor slightly above the lowest y coordinate
    float floorHeight = bbMin.y + 0.15f;
    float fsX = (bbMax.x - bbMin.x) * 1.2f;
    float fsZ = (bbMax.z - bbMin.z) * 1.2f;

    struct FloorVertex {
        glm::vec3 pos;
        glm::vec3 normal;
        glm::vec2 uv;
    };

    std::vector<FloorVertex> floorVertices = {
        {{center.x - fsX * 0.5f, floorHeight, center.z - fsZ * 0.5f}, {0, 1, 0}, {0.0f, 0.0f}},
        {{center.x + fsX * 0.5f, floorHeight, center.z - fsZ * 0.5f}, {0, 1, 0}, {5.0f, 0.0f}},
        {{center.x + fsX * 0.5f, floorHeight, center.z + fsZ * 0.5f}, {0, 1, 0}, {5.0f, 5.0f}},
        {{center.x - fsX * 0.5f, floorHeight, center.z + fsZ * 0.5f}, {0, 1, 0}, {0.0f, 5.0f}}
    };

    std::vector<uint32_t> floorIndices = {0, 2, 1, 0, 3, 2};

    GLuint floorVAO, floorVBO, floorEBO;
    glGenVertexArrays(1, &floorVAO);
    glGenBuffers(1, &floorVBO);
    glGenBuffers(1, &floorEBO);

    glBindVertexArray(floorVAO);
    glBindBuffer(GL_ARRAY_BUFFER, floorVBO);
    glBufferData(GL_ARRAY_BUFFER, floorVertices.size() * sizeof(FloorVertex), floorVertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, floorEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, floorIndices.size() * sizeof(uint32_t), floorIndices.data(), GL_STATIC_DRAW);

    // telling shader that "Location 0: Pos, Location 1: Normal, Location 2: UV"
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(FloorVertex), (void*)offsetof(FloorVertex, pos));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(FloorVertex), (void*)offsetof(FloorVertex, normal));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(FloorVertex), (void*)offsetof(FloorVertex, uv));

    // Framebuffer setup for rendering opaque cathedral scene
    GLuint sceneFBO, colorBuffer, depthTexture;
    int fbWidth = state.curWndParams.fbSize[0];
    int fbHeight = state.curWndParams.fbSize[1];
    int currentFBWidth = fbWidth;
    int currentFBHeight = fbHeight;

    glGenFramebuffers(1, &sceneFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);

    glGenTextures(1, &colorBuffer);
    glBindTexture(GL_TEXTURE_2D, colorBuffer);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fbWidth, fbHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorBuffer, 0);

    glGenTextures(1, &depthTexture);
    glBindTexture(GL_TEXTURE_2D, depthTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, fbWidth, fbHeight, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexture, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Clear settings
    glEnable(GL_DEPTH_TEST);
    glFrontFace(GL_CCW);

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(state.window, true);
    ImGui_ImplOpenGL3_Init("#version 430");

    // =============== //
    //   RENDER LOOP   //
    // =============== //
    while (!glfwWindowShouldClose(state.window))
    {
        glfwPollEvents();

        // ImGui Frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Camera movement
        glm::vec3 forward = state.cam.orientation * glm::vec3(0, 0, -1);
        glm::vec3 right = state.cam.orientation * glm::vec3(1, 0, 0);

        if (ImGui::GetCurrentContext() && !ImGui::GetIO().WantCaptureKeyboard) { // Only move camera if ImGui is not focused on keyboard input
            if (glfwGetKey(state.window, GLFW_KEY_W) == GLFW_PRESS) state.cam.pos += forward * gCamSpeed;
            if (glfwGetKey(state.window, GLFW_KEY_S) == GLFW_PRESS) state.cam.pos -= forward * gCamSpeed;
            if (glfwGetKey(state.window, GLFW_KEY_A) == GLFW_PRESS) state.cam.pos -= right * gCamSpeed;
            if (glfwGetKey(state.window, GLFW_KEY_D) == GLFW_PRESS) state.cam.pos += right * gCamSpeed;
        }

        float aspectRatio = float(state.curWndParams.fbSize[0]) / float(state.curWndParams.fbSize[1]);
        glm::mat4 proj = glm::perspective(glm::radians(60.0f), aspectRatio, 0.1f, 1000.0f);
        glm::mat4 view = glm::lookAt(state.cam.pos, state.cam.pos + forward, glm::vec3(0, 1, 0));

        // Resizing checks (in case framebuffer size changes due to window resizing or going fullscreen, we need to resize our FBO textures to match the new framebuffer size)
        int newWidth = state.curWndParams.fbSize[0];
        int newHeight = state.curWndParams.fbSize[1];
        if (newWidth != currentFBWidth || newHeight != currentFBHeight) {
            currentFBWidth = newWidth;
            currentFBHeight = newHeight;
            fbWidth = newWidth;
            fbHeight = newHeight;

            glBindTexture(GL_TEXTURE_2D, colorBuffer);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, newWidth, newHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

            glBindTexture(GL_TEXTURE_2D, depthTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, newWidth, newHeight, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        }

        // Render scene to Framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
        glViewport(0, 0, state.curWndParams.fbSize[0], state.curWndParams.fbSize[1]);

        glClearColor(0.15f, 0.20f, 0.30f, 1.0f); // basic blue color like unity background (looks awful but who cares)
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Draw scene
        glBindProgramPipeline(cathedralPipeline);
        glm::mat4 model = glm::identity<glm::mat4>();
        glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(model));

        glProgramUniformMatrix4fv(cathedralVShader.shaderId, glGetUniformLocation(cathedralVShader.shaderId, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
        glProgramUniformMatrix4fv(cathedralVShader.shaderId, glGetUniformLocation(cathedralVShader.shaderId, "uView"), 1, GL_FALSE, glm::value_ptr(view));
        glProgramUniformMatrix4fv(cathedralVShader.shaderId, glGetUniformLocation(cathedralVShader.shaderId, "uProj"), 1, GL_FALSE, glm::value_ptr(proj));
        glProgramUniformMatrix3fv(cathedralVShader.shaderId, glGetUniformLocation(cathedralVShader.shaderId, "uNormalMatrix"), 1, GL_FALSE, glm::value_ptr(normalMatrix));
        
        glProgramUniform3fv(cathedralFShader.shaderId, glGetUniformLocation(cathedralFShader.shaderId, "uViewPos"), 1, glm::value_ptr(state.cam.pos));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, cathedralTex.textureId);
        glProgramUniform1i(cathedralFShader.shaderId, glGetUniformLocation(cathedralFShader.shaderId, "tAlbedo"), 0);

        glBindVertexArray(sibenikMesh.vaoId);
        glDrawElements(GL_TRIANGLES, sibenikMesh.indexCount, GL_UNSIGNED_INT, nullptr);

        // Blit cathedral FBO to default framebuffer
        glBindFramebuffer(GL_READ_FRAMEBUFFER, sceneFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, fbWidth, fbHeight, 0, 0, fbWidth, fbHeight, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);

        // Render Reflective Floor directly to screen using FBO textures for SSR
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDepthMask(GL_FALSE); // Disable depth writes to allow blending of SSR results with the already rendered cathedral scene without depth conflicts
        glBindProgramPipeline(floorPipeline);

        glProgramUniformMatrix4fv(cathedralVShader.shaderId, glGetUniformLocation(cathedralVShader.shaderId, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
        glProgramUniformMatrix4fv(cathedralVShader.shaderId, glGetUniformLocation(cathedralVShader.shaderId, "uView"), 1, GL_FALSE, glm::value_ptr(view));
        glProgramUniformMatrix4fv(cathedralVShader.shaderId, glGetUniformLocation(cathedralVShader.shaderId, "uProj"), 1, GL_FALSE, glm::value_ptr(proj));
        glProgramUniformMatrix3fv(cathedralVShader.shaderId, glGetUniformLocation(cathedralVShader.shaderId, "uNormalMatrix"), 1, GL_FALSE, glm::value_ptr(normalMatrix));

        // Matrices for SSR (used for reconstructing world position and view direction in the floor fragment shader)
        glProgramUniformMatrix4fv(floorFShader.shaderId, glGetUniformLocation(floorFShader.shaderId, "uProj"), 1, GL_FALSE, glm::value_ptr(proj));
        glProgramUniformMatrix4fv(floorFShader.shaderId, glGetUniformLocation(floorFShader.shaderId, "uView"), 1, GL_FALSE, glm::value_ptr(view));
        glm::mat4 fInvProj = glm::inverse(proj);
        glm::mat4 fInvView = glm::inverse(view);
        glProgramUniformMatrix4fv(floorFShader.shaderId, glGetUniformLocation(floorFShader.shaderId, "uInvProj"), 1, GL_FALSE, glm::value_ptr(fInvProj));
        glProgramUniformMatrix4fv(floorFShader.shaderId, glGetUniformLocation(floorFShader.shaderId, "uInvView"), 1, GL_FALSE, glm::value_ptr(fInvView));

        glProgramUniform3fv(floorFShader.shaderId, glGetUniformLocation(floorFShader.shaderId, "uViewPos"), 1, glm::value_ptr(state.cam.pos));
        glProgramUniform1f(floorFShader.shaderId, glGetUniformLocation(floorFShader.shaderId, "uSSRStepSize"), gSSRStepSize);
        glProgramUniform1i(floorFShader.shaderId, glGetUniformLocation(floorFShader.shaderId, "uSSRMaxSteps"), gSSRMaxSteps);
        glProgramUniform1f(floorFShader.shaderId, glGetUniformLocation(floorFShader.shaderId, "uSSRThickness"), gSSRThickness);
        glProgramUniform1i(floorFShader.shaderId, glGetUniformLocation(floorFShader.shaderId, "uDebugMode"), gDebugMode);
        glProgramUniform1f(floorFShader.shaderId, glGetUniformLocation(floorFShader.shaderId, "uSSRBlendFactor"), gSSRBlendFactor);

        // Bind Textures
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, floorTex.textureId);
        glProgramUniform1i(floorFShader.shaderId, glGetUniformLocation(floorFShader.shaderId, "tFloorAlbedo"), 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, colorBuffer);
        glProgramUniform1i(floorFShader.shaderId, glGetUniformLocation(floorFShader.shaderId, "tOpaqueColor"), 1);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, depthTexture);
        glProgramUniform1i(floorFShader.shaderId, glGetUniformLocation(floorFShader.shaderId, "tDepth"), 2);

        glBindVertexArray(floorVAO);
        glDrawElements(GL_TRIANGLES, (GLsizei)floorIndices.size(), GL_UNSIGNED_INT, nullptr);

        glDepthMask(GL_TRUE); // Reenable depth writes after rendering the floor so that ImGui and any future objects are not blended with the floor's SSR results

        // ImGui parameters
        ImGui::SetNextWindowSize(ImVec2(450, 0), ImGuiCond_FirstUseEver);
        ImGui::Begin("SSR Parameters");

        ImGui::SliderFloat("Step Size", &gSSRStepSize, 0.02f, 1.0f, "%.2f");
        ImGui::SliderInt("Max Steps", &gSSRMaxSteps, 10, 250);
        ImGui::SliderFloat("Depth Thickness", &gSSRThickness, 0.05f, 10.0f, "%.2f");

        ImGui::Separator();
        ImGui::Text("Render Mode:");
        ImGui::RadioButton("Normal Blend", &gDebugMode, 0);
        ImGui::RadioButton("Reflection Only", &gDebugMode, 1);
        ImGui::RadioButton("SSR Hit Visualization", &gDebugMode, 2);

        if (gDebugMode == 0) {
            ImGui::SliderFloat("Blend Percent", &gSSRBlendFactor, 0.0f, 1.0f, "%.2f");
        }

        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(state.window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    return 0;
}
