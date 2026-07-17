#include "ImguiManager.h"
#include "imgui.h"
#include "main.h"
#include "backends/imgui_impl_android.h"
#include "backends/imgui_impl_opengl3.h"
#include "font/font.h"
#include "MinecraftGame.h"
#include "Minecraft.h"
#include "LevelData.h"
#include "ImGuiInputIME.h"
#include <mutex>
#include <thread>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <jni.h>
#include <android/input.h>
#include <android/native_window_jni.h>
#include <android/sensor.h>

static ANativeWindow *imgui_Window = nullptr;
static bool imgui_Running = false;
int imgui_Width = 0;
int imgui_Height = 0;

std::mutex imgui_ImGuiMutex;

EGLDisplay imgui_display = EGL_NO_DISPLAY;
EGLSurface imgui_eglSurface = EGL_NO_SURFACE;
EGLContext imgui_eglContext = EGL_NO_CONTEXT;
static std::thread g_RenderThread;
char input[256] = {0};

void renderThreadLoop();

void ImGui_OnSurfaceCreated(ANativeWindow *window) {
    if (imgui_Running) return;
    imgui_Window = window;
    imgui_Running = true;
    g_RenderThread = std::thread(renderThreadLoop);
}

void ImGui_OnSurfaceChanged(int width, int height) {
    std::lock_guard<std::mutex> lock(imgui_ImGuiMutex);
    imgui_Width = width;
    imgui_Height = height;
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui::GetIO().DisplaySize = ImVec2((float) width, (float) height);
    }
}

void ImGui_OnSurfaceDestroyed() {
    imgui_Running = false;
    if (g_RenderThread.joinable()) {
        g_RenderThread.join();
    }
    imgui_Window = nullptr;
}

static bool s_TouchCapturedByImGui = false;

bool ImGui_HandleInput(AInputEvent *event) {
    std::lock_guard<std::mutex> lock(imgui_ImGuiMutex);
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui_ImplAndroid_HandleInputEvent(event);
        if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
            int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
            if (action == AMOTION_EVENT_ACTION_DOWN) {
                s_TouchCapturedByImGui = ImGui::GetIO().WantCaptureMouse;
            } else if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
                bool ret = s_TouchCapturedByImGui;
                s_TouchCapturedByImGui = false;
                return ret;
            }
        }
        return s_TouchCapturedByImGui;
    }
    return false;
}

void renderThreadLoop() {
    imgui_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (imgui_display == EGL_NO_DISPLAY) {
        LOGE("无法获取 EGL Display!");
        return;
    }

    eglInitialize(imgui_display, nullptr, nullptr);

    const EGLint attribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_BLUE_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_RED_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 16,
            EGL_STENCIL_SIZE, 8,
            EGL_NONE
    };

    EGLint num_configs = 0;
    EGLConfig egl_config;
    eglChooseConfig(imgui_display, attribs, &egl_config, 1, &num_configs);

    EGLint egl_format;
    eglGetConfigAttrib(imgui_display, egl_config, EGL_NATIVE_VISUAL_ID, &egl_format);
    ANativeWindow_setBuffersGeometry(imgui_Window, 0, 0, egl_format);

    const EGLint egl_context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    imgui_eglContext = eglCreateContext(imgui_display, egl_config, EGL_NO_CONTEXT,
                                        egl_context_attributes);
    if (imgui_eglContext == EGL_NO_CONTEXT) {
        LOGE("EGL Context 创建失败: %d", eglGetError());
        return;
    }

    imgui_eglSurface = eglCreateWindowSurface(imgui_display, egl_config, imgui_Window, nullptr);
    if (imgui_eglSurface == EGL_NO_SURFACE) {
        LOGE("EGL Surface 创建失败: %d", eglGetError());
        return;
    }

    if (!eglMakeCurrent(imgui_display, imgui_eglSurface, imgui_eglSurface, imgui_eglContext)) {
        LOGE("eglMakeCurrent 绑定上下文失败!");
        return;
    }


    {
        std::lock_guard<std::mutex> lock(imgui_ImGuiMutex);
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.ConfigWindowsMoveFromTitleBarOnly = false;
        io.WantSaveIniSettings = true;

        ImGui_ImplAndroid_Init(imgui_Window);
        //ImGui_ImplOpenGL3_Init("#version 100");
        ImGui_ImplOpenGL3_Init("#version 300 es");
        ImGui::StyleColorsDark();
        ImGuiStyle *style = &ImGui::GetStyle();
        style->FramePadding = ImVec2(5, 5);
        style->WindowRounding = 3.0f;
        style->ScrollbarRounding = 2.0f;
        style->FrameRounding = 2.0f;
        style->GrabRounding = 2.0f;

        style->ScaleAllSizes(2.3f);

        ImFontConfig fontConf;
        fontConf.FontDataOwnedByAtlas = false;
        fontConf.OversampleH = 2;
        fontConf.OversampleV = 2;
        fontConf.PixelSnapH = true;

        io.Fonts->AddFontFromMemoryTTF((void *) OPPOSans_H, (int) OPPOSans_H_size, 32.0f,
                                       &fontConf, io.Fonts->GetGlyphRangesChineseFull());
        io.Fonts->Build();
    }

    while (imgui_Running) {
        if (imgui_Width <= 0 || imgui_Height <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        glViewport(0, 0, imgui_Width, imgui_Height);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        {
            std::lock_guard<std::mutex> lock(imgui_ImGuiMutex);
            if (ImGui::GetCurrentContext() != nullptr) {
                //ImGui::GetIO().DisplaySize = ImVec2((float)imgui_Width, (float)imgui_Height);
                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplAndroid_NewFrame();
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(100.f, 100.f),ImGuiCond_Once);
                ImGui::SetNextWindowSize(ImVec2(300.f, 200.f),ImGuiCond_Once);
                ImGui::Begin("Client Demo");
                ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
                ImGui::Text("Size: %dx%d", imgui_Width, imgui_Height);
                ImGuiIME::InputText("##cardkey", input, sizeof(input), 500);
                if(ImGui::Button("Button")){
                    auto* mcc = MinecraftClient::getInstance();
                    auto* guidata = mcc->getGuiData();
                    if (guidata)guidata->displayClientMessage("Hello World!");
                }


                ImGui::End();

                ImGui::Render();
            }
        }

        if (imgui_Running) {
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            if (!eglSwapBuffers(imgui_display, imgui_eglSurface)) {
                LOGE("eglSwapBuffers error: %d", eglGetError());
            }
        }
    }


    {
        std::lock_guard<std::mutex> lock(imgui_ImGuiMutex);
        if (ImGui::GetCurrentContext() != nullptr) {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplAndroid_Shutdown();
            ImGui::DestroyContext();
        }
    }

    eglMakeCurrent(imgui_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (imgui_eglContext != EGL_NO_CONTEXT) eglDestroyContext(imgui_display, imgui_eglContext);
    if (imgui_eglSurface != EGL_NO_SURFACE) eglDestroySurface(imgui_display, imgui_eglSurface);
    eglTerminate(imgui_display);

    imgui_eglContext = EGL_NO_CONTEXT;
    imgui_eglSurface = EGL_NO_SURFACE;
    imgui_display = EGL_NO_DISPLAY;
}

extern "C" JNIEXPORT void JNICALL
Java_com_irene_algui_ImguiView_ImguiSurfaceCreated(JNIEnv *env, jobject thiz, jobject surface) {
    ANativeWindow *window = ANativeWindow_fromSurface(env, surface);
    if (window) {
        ANativeWindow_acquire(window);
        ImGui_OnSurfaceCreated(window);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_irene_algui_ImguiView_ImguiSurfaceDestroyed(JNIEnv *env, jobject thiz) {
    ImGui_OnSurfaceDestroyed();
    if (imgui_Window) {
        ANativeWindow_release(imgui_Window);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_irene_algui_ImguiView_ImguiSurfaceChanged(JNIEnv *env, jobject thiz, jint width,
                                                   jint height) {
    ImGui_OnSurfaceChanged(width, height);

}