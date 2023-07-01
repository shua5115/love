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