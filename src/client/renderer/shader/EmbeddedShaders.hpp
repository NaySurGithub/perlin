#pragma once

#include <bgfx/embedded_shader.h>

#undef BGFX_EMBEDDED_SHADER_DXIL
#define BGFX_EMBEDDED_SHADER_DXIL(_renderer, _name) BGFX_EMBEDDED_SHADER_DXBC(_renderer, _name)
