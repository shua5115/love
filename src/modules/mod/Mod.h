/**
 * Copyright (c) 2006-2023 LOVE Development Team
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
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
	virtual void foo() const;
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