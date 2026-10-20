#pragma once

// Perlin compiles Direct3D shaders once, as DXBC (shader model 5.0). bgfx's Direct3D 12 renderer runs
// that same bytecode, but bgfx's BGFX_EMBEDDED_SHADER only offers Direct3D 12 the DXIL data slot, which
// Perlin never builds. Without this the Direct3D 12 backend found no embedded shaders at all and could
// not be used. Point that slot at the DXBC data so both Direct3D backends share one set of binaries.
#include <bgfx/embedded_shader.h>

#undef BGFX_EMBEDDED_SHADER_DXIL
#define BGFX_EMBEDDED_SHADER_DXIL(_renderer, _name) BGFX_EMBEDDED_SHADER_DXBC(_renderer, _name)
