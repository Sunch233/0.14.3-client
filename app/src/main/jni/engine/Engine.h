#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace engine {
// Shared ABI types for the verified MCPE 0.14.3 ARMv7 / GNU STL build.
struct Color {float r,g,b,a;};
template<class T> T& At(void* object,unsigned offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object)+offset);
}
inline std::string Number(int value) {
    char buffer[32];
    ::snprintf(buffer,sizeof(buffer),"%d",value);
    return buffer;
}
struct Hook {
    const char* name;
    uintptr_t rva;
    const char* prefix;
    void* replace;
    void** original;
    // For verified internal callbacks without an exported symbol.
    bool byRva=false;
};

// Shared identity for binary verification and diagnostic report metadata.
constexpr const char* binarySha256="b854edd48d8cc02fbd2e1f0974842fb0f8519b0afff6460af12b9cb48cf59029";
bool Sha256File(const char* path,std::string& digest);
// Must succeed before resolving feature or settings UI object layouts.
bool VerifyBinary(void* library);
// Validate every entry before installing any hook in this module.
bool InstallHooks(void* library,const Hook* hooks,std::size_t count);
}
