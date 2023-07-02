#ifndef LOVE_MOD_H
#define LOVE_MOD_H

 // LOVE
#include "common/Module.h"
#include <assimp/scene.h>

namespace love
{
namespace assimp
{

class Assimp: public Module
{
public:

	Assimp() {} // Empty constructor, we don't need anything fancy.
	virtual ~Assimp() {} // Empty destructor

	// Implements Module.
	ModuleType getModuleType() const override { return M_ASSIMP; }

    // Define the name for this module
    const char *getName() const override { return "love.assimp"; }

	virtual void foo() const;

};

} // assimp
} // love

#endif