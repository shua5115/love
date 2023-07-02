#include "wrap_Assimp.h"
#include "Assimp.h"

namespace love
{
namespace assimp
{

#define instance() (Module::getInstance<Assimp>(Module::M_ASSIMP))

int w_foo(lua_State *L)
{
    Assimp* mod = instance();
    mod->foo();
    return 0;
}

// List of functions to wrap.
static const luaL_Reg functions[] =
{
	{"foo", w_foo},

	{ 0, 0 }
};

// contains wrappers for c++ classes to be represented as love types
static const lua_CFunction types[] =
{
	nullptr
};

extern "C" LOVE_EXPORT int luaopen_love_assimp(lua_State *L)
{
    Assimp *instance = instance();
	if (instance == nullptr)
	{
		luax_catchexcept(L, [&](){ instance = new Assimp(); });
	}
	else
		instance->retain();
    
    WrappedModule w;
	w.module = instance;
	w.name = "assimp";
	w.type = &Assimp::type;
	w.functions = functions;
	w.types = types;

	int n = luax_register_module(L, w);
	return n;
}

} // mod
} // love