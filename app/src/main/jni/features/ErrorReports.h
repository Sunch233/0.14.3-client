#pragma once
#include <string>

namespace features { namespace errorreports {
void InstallVerified(void* library);
void RecordUiError(const std::string& file,const std::string& function,const std::string& details);
}}
