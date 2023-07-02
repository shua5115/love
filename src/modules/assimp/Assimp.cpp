#include "Assimp.h"
#include <iostream>

namespace love
{
namespace assimp
{

void Assimp::foo() const
{
    aiScene scene;
    scene.mName = aiString("Hello, assimp!");
    std::cout << scene.mName.C_Str() << std::endl;
}

} // mod
} // love