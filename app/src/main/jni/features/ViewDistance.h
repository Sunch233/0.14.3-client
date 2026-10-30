#pragma once

namespace features { namespace viewdistance {
void InstallVerified(void* library);
bool IsUnlockedOption(const void* option);
int GetChunks(void* client);
}}
