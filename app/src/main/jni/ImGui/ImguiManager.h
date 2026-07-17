#pragma once
#include <android/native_window.h>
#include <android/input.h>

void ImGui_OnSurfaceCreated(ANativeWindow* window);
void ImGui_OnSurfaceChanged(int width, int height);
void ImGui_OnSurfaceDestroyed();
bool ImGui_HandleInput(AInputEvent* event);