#pragma once
namespace features { namespace vsync {
void Initialize(void* library);
bool Available();
void Apply(bool enabled);
}}
