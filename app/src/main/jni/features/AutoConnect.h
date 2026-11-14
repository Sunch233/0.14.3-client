#pragma once
namespace features { namespace autoconnect {
// Joins one configured server right after the start menu appears.
// The target is read from advanced-graphics.cfg (autoConnect=host[:port]) once
// the settings bridge has run, which happens after the native library loads.
void Initialize(void* library);
// Call once per rendered frame with the MinecraftClient instance.
void Tick(void* minecraft);
bool Armed();
}}
