#pragma once
#include <string>
namespace features { namespace fpscounter {
void BeginFrame(bool enabled);
bool Enabled();
const std::string& Text();
}}
