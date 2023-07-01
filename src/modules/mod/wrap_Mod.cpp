#include "wrap_Mod.h"
#include "Mod.h"

namespace love
{
namespace mod
{

#define instance() (Module::getInstance<Mod>(Module::M_MOD))

int w_foo(lua_State *L)
{
    Mod* mod = instance();
    mod->foo();
    return 0;
}

// List of functions to wrap.
static const luaL_Reg functions[] =
{
	{"foo", w_foo},

	{ 0, 0 }
};

static const lua_CFunction types[] =
{
	nullptr
};

extern "C" LOVE_EXPORT int luaopen_love_mod(lua_State *L)
{
    Mod *instance = instance();
	if (instance == nullptr)
	{
		luax_catchexcept(L, [&](){ instance = new Mod(); });
	}
	else
		instance->retain();
    
    WrappedModule w;
	w.module = instance;
	w.name = "mod";
	w.type = &Mod::type;
	w.functions = functions;
	w.types = types;

	int n = luax_register_module(L, w);
	return n;
}

} // mod
} // love