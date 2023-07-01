# How To Make A New Module

This fork exists because I want to add a new module(s) to love2d. I have very little experience with this codebase,
let alone c++, so I am using this document to detail my findings regarding how to make a new module.

## Table of Contents

1. [Instructions](#instructions)
2. [Next Steps](#next-steps)
3. [Notes and References](#notes-and-references) 

## Instructions

0. Strongly consider if your idea needs to be a module. You may have the option of making the module a shared lua library.
Assuming you do have an idea that would best be a new module, continue on.

1. In the `src/common/module.h` file, find the `ModuleType` definition and add an entry for your module in the form `M_MOD`.

2. Create a new folder in the `src/modules` folder with the name of your module.
I will be using "mod", and other variations like "Mod" or "MOD", as the placeholder for your module name throughout this guide.

3. Create a file `Mod.h` in `src/modules/mod/` with the following structure:
```c++
/**
 * Copyright (c) 2006-2023 LOVE Development Team
 *
 * Be sure to include the fully copyright notice for your file, it is abbreviated here to save space.
 **/

#ifndef LOVE_MOD_H
#define LOVE_MOD_H

 // LOVE
#include "common/Module.h"

namespace love
{
namespace mod
{

class Mod: public Module
{
public:

	Mod() {} // Empty constructor, we don't need anything fancy.
	virtual ~Mod() {} // Empty destructor

	// Implements Module.
	ModuleType getModuleType() const override { return M_MOD; } // M_MOD is a custom entry in the ModuleType enum
    // Note: referring to the code style guide, functions that have a short, 1-line implementation (like this getter) should be in the .h file.

    // Define the name for this module
    const char *getName() const override { return "love.mod"; }

    // We define most functions in modules like this:
	virtual void foo() const = 0;
    // For those unfamiliar with c++ (like me), I will spell out the meaning of features of the function header:
    // "virtual" -> this enables polymorphism. Generally always included in modules.
    // "const" -> disallows mutating the class instance within the function.
    //            The "const" specifier depends on the purpose of the function,
    //            you may need to mutate data for some functions.
    // "= 0" -> function must be implemented by a child class, not the base class.
    //          This would be specified if there could be multiple implementations of your class.
    //          For example, the Audio library could be implemented with a different backend framework,
    //          so all of its functions are like this. However, the data module only has 1 implementation,
    //          so all of its functions omit the "= 0".
    // "override" -> explicitly states that the function overrides a function from a parent class.
    

}; // Mod

} // mod
} // love

#endif
```

4. Once you have defined all the functions that will be used in your module, you can add a `Mod.cpp` file in the same folder.
   What goes in this file depends on the `Mod.h` file. Generally, all functions not ending in a "= 0" are implemented.

   I will modify the `foo()` declaration to be `virtual void foo() const;` and implement it as such in `Mod.cpp`:
```c++
#include "Mod.h"
#include <iostream>

namespace love
{
namespace mod
{

void Mod::foo() const
{
    std::cout << "Hello, world!" << std::endl;
}

} // mod
} // love
```

5. At this point, or even at step 3, if you are using code completion you may 
   have noticed errors saying that your file can't import other love files.
   This is because nowhere in the compilation instructions do you tell the compiler
   that your file is used in this codebase.
   So, we need to modify the `CMakeLists.txt` file that is in the root of the repository.

   First, find the section "Third-party libraries". Just above it is the end of the module definitions.
   You can insert your new module's files there.

   Basic modules are defined in the cmake file by setting a variable to a list of source file paths, like so:
```cmake
set(LOVE_SRC_MODULE_MOD
    src/modules/mod/Mod.h
	src/modules/mod/Mod.cpp
)
```
   This is enough for now, we can add more files later as the new module grows in size.

6. Next, we need to tell the compiler that the new module is included in the love library.

   In the root `CMakeLists.txt` file, find the `LOVE_LIB_SRC` variable definition.
   It should be pretty clear how your module fits in here, just add another line with the variable we defined in the previous step:
```cmake
set(LOVE_LIB_SRC
	${LOVE_SRC_COMMON}
	# Modules
	${LOVE_SRC_MODULE_AUDIO}
    # ...etc.
    ${LOVE_SRC_MODULE_MOD}
)
```
   After saving this file, cmake the errors about importing other files should go away.
   If the errors persist, try compiling and read the error messages to debug.

7. Next up is getting your module accessable to lua.
   In this codebase, the lua wrapper for modules is done through files starting with "wrap_". 
   So, start by making a `wrap_Mod.h` and a `wrap_Mod.cpp` file in your module directory.

8. The "wrap_Mod.h" file:
```c++
#pragma once

// LOVE
#include "common/config.h"
#include "common/runtime.h"
#include "Mod.h"

namespace love
{
namespace mod
{

// Wrapped module function
int w_foo(lua_State *L);

// Loads module into lua environment
extern "C" LOVE_EXPORT int luaopen_love_mod(lua_State *L);

} // mod
} // love
```
9. The "wrap_Mod.cpp" file:
```c++
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

// contains wrappers for c++ classes to be represented as love types
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
```

10. Be sure to add the "wrap_Mod.h" and "wrap_Mod.cpp" files to the CMakeLists.txt file in the LOVE_SRC_MODULE_MOD variable definition

11. We will make the new module accessible in lua by modifying the `src/modules/love/love.cpp` file.
    
    First, find a `extern C` code block containing function headers in the form `extern int luaopen_love_module(lua_State*);`.
    This is where the function definitions for our module loaders will be referenced.
    Add an entry to the code block:

```c++
extern "C"
{
#if defined(LOVE_ENABLE_AUDIO)
    extern int luaopen_love_audio(lua_State*);
#endif
    // ...etc.
    extern int luaopen_love_mod(lua_State*);
}
```
   Don't worry about the `#if defined(LOVE_ENABLE_*)` checks, they seem to be dynamically generated.
   I don't know how they work, and you're making this version of love expressely because you need
   this custom module, so I don't think making the module optional is too important currently.

   Now, we can access the new module by calling `require("love.mod")` in lua,
   which will load it into the global love table, but this is not done automatically.

12. To fix that, we can modify `src/modules/love/boot.lua` to require the module for us when love boots up.

    In the file, a table called "c" (likely for "config") is defined. Within that table is a sub-table called "module".
    This table is a mapping from module name to boolean determining which modules are loaded at boot time.
    Add an entry:
```lua
    local c = {
        title = "Untitled",
        ---etc.
        modules = {
            data = true,
            ---etc.
            mod = true, -- added!
        }
    }
```

Further down in the file, there is a for loop with a comment "Gets desired modules".
In the list of names, add the name of the new module, "mod":

```lua
-- Gets desired modules.
	for k,v in ipairs{
		"data",
        ---ect.
        "mod", -- added!
    } do
        if c.modules[v] then
			require("love." .. v)
		end
	end
```

13. With that, your module should be a part of love. You can try it out in a test love file:

main.lua
```lua
-- require "love.mod" -- not needed anymore!

function love.load()
	love.mod.foo()
	love.event.push("quit")
end
```

## Next Steps

There are multiple different module structures, and they get different treatment in the `CMakeLists.txt` file 
depending on their file structure and if they use 3rd party libraries.
I will give examples of libraries which have interesting structures,
and you can refer to them if the structure applies to your module:

- love.data: A "flat" module with no subdirectories and uses no 3rd party libraries.
    - File structure:
```
src/modules/data
|   DataModule.h
|   DataModule.cpp
|   (other files): more module features
|   wrap_DataModule.h
|   wrap_DataModule.cpp
```


## Notes and References

### [Adding a new module to Love, help!](https://love2d.org/forums/viewtopic.php?t=83369)

Things to do when adding a new module:
- I copied how other modules are registered, so my module has an extern "C" int luaopen_love_myModule(lua_State *L)
    - Inside that function call `return luax_register_module(L, w);`
- Define `#define instance() (Module::getInstance<MyModule>(Module::M_MYMODULE))`
- Add new module in `static const luaL_Reg modules[]`
- When `int luaopen_love(lua_State *L)` gets called, check if the new is being preloaded in the for loop `for (int i = 0; modules.name != nullptr; i++)`
- Add module in boot.lua

### [Editing LOVE's source .lua files](https://love2d.org/forums/viewtopic.php?t=54754)

Seems outdated, the new repo doesn't have a boot.lua.h. After modifying the raw boot.lua and recompiling as a test, it seems that this conversion step is no longer necessary. I will keep this here as a reference in case it becomes useful.

