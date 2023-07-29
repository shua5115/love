#ifndef LOVE_ASSIMP_WRAP_ASSIMP
#define LOVE_ASSIMP_WRAP_ASSIMP

// LOVE
#include "common/config.h"
#include "common/runtime.h"
#include "AssimpModule.h"

namespace love
{
namespace love_assimp
{

int w_import(lua_State *L);

// Loads module into lua environment
extern "C" LOVE_EXPORT int luaopen_love_assimp(lua_State *L);

} // assimp
} // love

#endif