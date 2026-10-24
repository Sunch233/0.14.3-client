#pragma once
namespace features {
// Call after engine::VerifyBinary succeeds. Individual modules verify hook RVAs.
// Owns feature initialization only; settingsui installs its UI independently.
void InstallVerified(void* library);
}
