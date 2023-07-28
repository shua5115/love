#pragma once

// LOVE
#include "common/config.h"
#include "common/runtime.h"
#include "AssimpModule.h"

namespace love
{
namespace love_assimp
{

// Wrapped module function
// int w_foo(lua_State *L);

int w_import(lua_State *L);

// Loads module into lua environment
extern "C" LOVE_EXPORT int luaopen_love_assimp(lua_State *L);

} // assimp
} // love