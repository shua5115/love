#include "AssimpModule.h"
#include <iostream>
#include <vector>
#include <unordered_map>
#include "modules/math/Transform.h"

using love::math::Transform;

namespace love
{
namespace love_assimp
{

void AssimpModule::foo() const
{
    aiScene scene;
    scene.mName = aiString("Hello, assimp");
    std::cout << scene.mName.C_Str() << std::endl;
    printf("Assimp version: %d.%d.%d\n", aiGetVersionMajor(), aiGetVersionMinor(), aiGetVersionPatch());
}

int AssimpModule::convert(lua_State *L, const aiScene *scene)
{
    // make a table to temporarily store the node structure
    // store node tree in a flat list, breadth first order 
    size_t nodestack_i = 0;
    std::vector<const aiNode *> nodelist;
    std::unordered_map<const aiNode *, size_t> node_indices;
    nodelist.push_back(scene->mRootNode);
    while (nodestack_i < nodelist.size()) {
        const aiNode *current = nodelist[nodestack_i];
        node_indices[current] = nodestack_i;
        for (unsigned int i = 0; i < current->mNumChildren; i++) {
            nodelist.push_back(current->mChildren[i]);
        }
        nodestack_i += 1;
    }
    // then save all converted nodes in the temporary table,
    // replacing all references to nodes with indices for this table
    lua_newtable(L);
    for(int i = 0; i < nodelist.size(); i++) {
        lua_pushinteger(L, i+1);
        convert(L, nodelist[i]);
        lua_settable(L, -3);
    }

    // this table is the scene table
    lua_newtable(L);
    
    lua_pushlstring(L, scene->mName.data, scene->mName.length);
    lua_setfield(L, -2, "name");

    unsigned int flags = scene->mFlags;
    lua_createtable(L, 0, 6);
        lua_pushboolean(L, (flags & AI_SCENE_FLAGS_INCOMPLETE) != 0);
        lua_setfield(L, -2, "incomplete");
        lua_pushboolean(L, (flags & AI_SCENE_FLAGS_VALIDATED) != 0);
        lua_setfield(L, -2, "validated");
        lua_pushboolean(L, (flags & AI_SCENE_FLAGS_VALIDATION_WARNING) != 0);
        lua_setfield(L, -2, "warning");
        lua_pushboolean(L, (flags & AI_SCENE_FLAGS_NON_VERBOSE_FORMAT) != 0);
        lua_setfield(L, -2, "nonverbose");
        lua_pushboolean(L, (flags & AI_SCENE_FLAGS_TERRAIN) != 0);
        lua_setfield(L, -2, "terrain");
        lua_pushboolean(L, (flags & AI_SCENE_FLAGS_ALLOW_SHARED) != 0);
        lua_setfield(L, -2, "allow_shared");
    lua_setfield(L, -2, "flags");

    return 1;
}

int AssimpModule::convert(lua_State *L, const aiNode *node)
{
    lua_newtable(L);
    
    lua_pushlstring(L, node->mName.data, node->mName.length);
    lua_setfield(L, -2, "name");

    convert(L, &node->mTransformation);
    lua_setfield(L, -2, "transform");

    convert(L, node->mMetaData);
    lua_setfield(L, -2, "metadata");

    return 1;
}

int AssimpModule::convert(lua_State *L, const aiMesh *mesh)
{
    return 0;
}

int AssimpModule::convert(lua_State *L, const aiFace *face)
{
    return 0;
}

int AssimpModule::convert(lua_State *L, const aiAABB *aabb)
{
    return 0;
}

int AssimpModule::convert(lua_State *L, const aiMaterial *mat)
{
    return 0;
}

int AssimpModule::convert(lua_State *L, const aiMaterialProperty *prop)
{
    return 0;
}

int AssimpModule::convert(lua_State *L, const aiTexture *texture)
{
    return 0;
}

int AssimpModule::convert(lua_State *L, const aiAnimation *anim)
{
    return 0;
}

int AssimpModule::convert(lua_State *L, const aiNodeAnim *anim)
{
    return 0;
}

int AssimpModule::convert(lua_State *L, const aiMeshAnim *anim)
{
    return 0;
}

int AssimpModule::convert(lua_State *L, const aiMeshMorphAnim *anim)
{
    return 0;
}

int AssimpModule::convert(lua_State *L, const aiBone *bone)
{
    return 0;
}

int AssimpModule::convert(lua_State *L, const aiLight *light)
{
    return 0;
}

int AssimpModule::convert(lua_State *L, const aiCamera *camera)
{
    return 0;
}

int AssimpModule::convert(lua_State *L, const aiMetadata *metadata)
{
    if (metadata == nullptr) {
        lua_pushnil(L);
        return 1;
    }
    lua_newtable(L);
    for(unsigned int i = 0; i < metadata->mNumProperties; i++) {
        const aiString *key = metadata->mKeys + i;
        lua_pushlstring(L, key->data, key->length);
        convert(L, metadata->mValues + i);
        lua_settable(L, -3);
    }
    return 1;
}

int AssimpModule::convert(lua_State *L, const aiMetadataEntry *entry)
{
    if (entry->mData == nullptr) {
        lua_pushnil(L);
        return 1;
    }
    switch (entry->mType) {
        case aiMetadataType::AI_BOOL:
            lua_pushboolean(L, *((bool *)entry->mData));
            break;
        case aiMetadataType::AI_INT32:
            lua_pushinteger(L, *((int32_t *)entry->mData));
            break;
        case aiMetadataType::AI_UINT64:
            lua_pushinteger(L, *((uint64_t *)entry->mData));
            break;
        case aiMetadataType::AI_FLOAT:
            lua_pushnumber(L, *((float *)entry->mData));
            break;
        case aiMetadataType::AI_DOUBLE:
            lua_pushnumber(L, *((double *)entry->mData));
            break;
        case aiMetadataType::AI_AISTRING:
            lua_pushlstring(L, ((const aiString *)entry->mData)->data, ((const aiString *)entry->mData)->length);
            break;
        case aiMetadataType::AI_AIVECTOR3D:
            convert(L, (const aiVector3D *)entry->mData);
            break;
        case aiMetadataType::AI_AIMETADATA:
            convert(L, (const aiMetadata *)entry->mData);  // recursion, yay!
            break;
        default:
            lua_pushnil(L);
            break;
    }
    return 1;
}

int AssimpModule::convert(lua_State *L, const aiMatrix4x4 *mat4)
{
    float elems[] = {
        mat4->a1, mat4->b2, mat4->a3, mat4->a4,
        mat4->b1, mat4->b2, mat4->b3, mat4->b4,
        mat4->c1, mat4->b2, mat4->c3, mat4->c4,
        mat4->d1, mat4->b2, mat4->d3, mat4->d4
    };
    love::Matrix4 mat(elems);
    Transform *t = new Transform(mat);
    luax_pushtype<love::math::Transform>(L, t);
    return 1;
}

int AssimpModule::convert(lua_State *L, const aiMatrix3x3 *mat3)
{
    float elems[] = {
        mat3->a1, mat3->b2, mat3->a3, 0,
        mat3->b1, mat3->b2, mat3->b3, 0,
        mat3->c1, mat3->b2, mat3->c3, 0,
        0, 0, 0, 1
    };
    love::Matrix4 mat(elems);
    Transform *t = new Transform(mat);
    luax_pushtype<love::math::Transform>(L, t);
    return 1;
}

int AssimpModule::convert(lua_State *L, const aiVector3D *vec3)
{
    lua_newtable(L);
    lua_pushinteger(L, 1);
    lua_pushnumber(L, vec3->x);
    lua_settable(L, -3);
    lua_pushinteger(L, 2);
    lua_pushnumber(L, vec3->y);
    lua_settable(L, -3);
    lua_pushinteger(L, 3);
    lua_pushnumber(L, vec3->z);
    lua_settable(L, -3);
    return 1;
}

int AssimpModule::convert(lua_State *L, const aiVector2D *vec2)
{
    lua_newtable(L);
    lua_pushinteger(L, 1);
    lua_pushnumber(L, vec2->x);
    lua_settable(L, -3);
    lua_pushinteger(L, 2);
    lua_pushnumber(L, vec2->y);
    lua_settable(L, -3);
    return 1;
}

int AssimpModule::convert(lua_State *L, const aiQuaternion *quat)
{
    lua_newtable(L);
    lua_pushinteger(L, 1);
    lua_pushnumber(L, quat->x);
    lua_settable(L, -3);
    lua_pushinteger(L, 2);
    lua_pushnumber(L, quat->y);
    lua_settable(L, -3);
    lua_pushinteger(L, 3);
    lua_pushnumber(L, quat->z);
    lua_settable(L, -3);
    lua_pushinteger(L, 4);
    lua_pushnumber(L, quat->w);
    lua_settable(L, -3);
    return 1;
}

int AssimpModule::convert(lua_State *L, const aiColor4D *col4)
{
    lua_newtable(L);
    lua_pushinteger(L, 1);
    lua_pushnumber(L, col4->r);
    lua_settable(L, -3);
    lua_pushinteger(L, 2);
    lua_pushnumber(L, col4->g);
    lua_settable(L, -3);
    lua_pushinteger(L, 3);
    lua_pushnumber(L, col4->b);
    lua_settable(L, -3);
    lua_pushinteger(L, 4);
    lua_pushnumber(L, col4->a);
    lua_settable(L, -3);
    return 1;
}

int AssimpModule::convert(lua_State *L, const aiColor3D *col3)
{
    lua_newtable(L);
    lua_pushinteger(L, 1);
    lua_pushnumber(L, col3->r);
    lua_settable(L, -3);
    lua_pushinteger(L, 2);
    lua_pushnumber(L, col3->g);
    lua_settable(L, -3);
    lua_pushinteger(L, 3);
    lua_pushnumber(L, col3->b);
    lua_settable(L, -3);
    return 1;
}

} // mod
} // love