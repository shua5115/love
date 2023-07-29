#include "wrap_Assimp.h"
#include "modules/filesystem/Filesystem.h"
#include "modules/filesystem/wrap_Filesystem.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>

namespace love
{
namespace love_assimp
{

using namespace filesystem;

#define instance() (Module::getInstance<AssimpModule>(Module::M_ASSIMP))

int w_version(lua_State *L)
{
	lua_pushfstring(L, "%d.%d.%d", aiGetVersionMajor(), aiGetVersionMinor(), aiGetVersionPatch());
	return 1;
}

int w_postprocess_options(lua_State *L)
{
	const auto mod = instance();
	
	lua_createtable(L, mod->post_process_strings.size(), 0);
	{
		int i = 1;
		for(auto e : mod->post_process_strings) {
			lua_pushinteger(L, i);
			luax_pushstring(L, e.first);
			lua_settable(L, -3);
			i++;
		}
	}

	return 1;
}

// function import(file, postprocess_flags)
// file can be a string (filename), FileData, or Data, and will be read accordingly
// postprocess_flags is an optional table with array entries
// pushes a table upon success, pushes (nil, errormsg) on failure
int w_import(lua_State *L)
{
	AssimpModule* mod = instance();
	Filesystem *lfs = Module::getInstance<Filesystem>(Module::M_FILESYSTEM);
	if(lfs == nullptr) {
		lua_pushnil(L);
		lua_pushstring(L, "love.filesystem is not loaded");
		return 2;
	};
	Assimp::Importer importer;

	Data *data = nullptr;
	std::string extension = "";
	// When parsing the first argument, there are two possibilities:
	// 1. We parse arguments to get a Data object and maybe a file extension.
	//	  In this case, there will exist a reference to the data object at a negative stack index.
	// 2. We fail to obtain a Data object which points to the asset data in memory
	//	  In this case, we return from this function in the form of nil, errmsg.
	if (lua_isstring(L, 1)) {
		lua_getglobal(L, "love"); // pushes love table
		lua_getfield(L, -1, "filesystem"); // pushes filesystem module table
		lua_getfield(L, -1, "newFileData"); // pushes function to be called
		lua_pushvalue(L, 1); // pushes the first argument to the import function (the filename)
		lua_call(L, 1, 2); // Equivalent to `love.filesystem.newFileData(arg1)` which returns nil, errmsg on failure
		if (lua_isnoneornil(L, -2)) {
			// The stack is already populated with nil, errmsg at the top, so return them.
			return 2;
		}
		lua_pop(L, 1); // If the first return value is non-nil, then we can pop the extra value at the top of the stack.
		FileData *fd = luax_checktype<FileData>(L, -1);
		if (fd == nullptr) { // Probably a redundant check, but now we know we're not going to dereference a nullptr.
			lua_pushnil(L);
			lua_pushstring(L, "Could not read file data.");
			return 2;
		}
		data = fd;
		extension = fd->getExtension();
	} else if (luax_istype(L, 1, FileData::type)) {
		lua_pushvalue(L, 1); // Push a reference to the FileData for consistency with the string argument case
		FileData *fd = luax_totype<FileData>(L, -1); // Safe because we used istype before this
		data = fd;
		extension = fd->getExtension();
	} else if (luax_istype(L, 1, Data::type)) {
		lua_pushvalue(L, 1);
		Data *d = luax_totype<Data>(L, 1);
		data = d;
	} else {
		lua_pushnil(L);
		lua_pushstring(L, "Expected type string, FileData, or Data");
		return 2;
	}
	// At this point there exists a Data object at stack index -1

	unsigned int opt_post_process = aiProcess_Triangulate; // Always triangulate meshes for simplicity
	if (lua_istable(L, 2)) {
		// get options by iterating through every item in the list
		int i = 1;
		while (true) {
			lua_pushinteger(L, i);
			lua_gettable(L, 2); // pushes 1 value onto the stack
			if (lua_isnoneornil(L, -1)) {
				lua_pop(L, 1);
				break;
			}
			if (lua_isstring(L, -1)) {
				const std::string key = luax_tostring(L, -1);
				if(mod->post_process_strings.find(key) != mod->post_process_strings.end()) {
					// printf("Applying post process step: %s\n", key.c_str());
					const unsigned int val = mod->post_process_strings.at(key);
					opt_post_process |= val;
				}
			}
			lua_pop(L, 1);
			i++;
		}

	}

	// At this point all stack modification done by the post-process option parsing should be cleared
	// and the Data object should remain at stack index -1

	const aiScene* scene = importer.ReadFileFromMemory((const char *)data->getData(), data->getSize(), opt_post_process, extension.c_str());
	if (scene == nullptr) {
		lua_pushnil(L);
		lua_pushstring(L, "Could not import the asset from provided data");
		return 2;
	} else {
		mod->convert(L, scene); // pushes a table
	}
	// At this point, we should have two values at the top of the stack at these indices:
	// -1: table containing converted scene
	// -2: Data object reference
	// After this function returns, the data object should be automatically cleared by lua's stack management.

	// printf("Pushed table address: %p\n", lua_topointer(L, -1));
	// printf("Contents of the stack after conversion:\n");
	// for(int i = 0; i <= lua_gettop(L); i++) {
	// 	const char *valstr = lua_tostring(L, i);
	// 	const void *p = lua_topointer(L, i);
	// 	printf("%d. type: %s, value: %s, addr: %p\n", i, luaL_typename(L, i), valstr == nullptr ? "N/A" : valstr, p);
	// }

	// return the scene table
	return 1;
}

// List of functions to wrap.
static const luaL_Reg functions[] =
{
	{"getVersion", w_version},
	// {"foo", w_foo},
	{"import", w_import},
	{"getPostProcessOptions", w_postprocess_options},

	{ 0, 0 }
};

// contains wrappers for c++ classes to be represented as love types
static const lua_CFunction types[] =
{
	nullptr
};

extern "C" LOVE_EXPORT int luaopen_love_assimp(lua_State *L)
{
    AssimpModule *instance = instance();
	if (instance == nullptr)
	{
		luax_catchexcept(L, [&](){ instance = new AssimpModule(); });
	}
	else
		instance->retain();
    
    WrappedModule w;
	w.module = instance;
	w.name = "assimp";
	w.type = &AssimpModule::type;
	w.functions = functions;
	w.types = types;

	int n = luax_register_module(L, w);
	return n;
}

} // mod
} // love