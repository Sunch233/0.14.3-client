#pragma once
namespace features { namespace titlebranding {
// Called after the existing engine SHA-256 guard.
void InstallVerified(void* library);
void RenderDevelopmentOverlay(void* client);
}
} // namespace features
