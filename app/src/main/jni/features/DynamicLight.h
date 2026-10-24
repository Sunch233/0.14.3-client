#pragma once
namespace features { namespace dynamiclight {
// The caller verifies the engine SHA-256 before installing this adapter.
void InstallVerified(void* library);
bool Available();
}
} // namespace features
