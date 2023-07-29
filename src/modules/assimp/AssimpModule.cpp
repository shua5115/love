#include "AssimpModule.h"
#include <iostream>
#include <vector>
#include <unordered_map>
#include "modules/math/Transform.h"
#include "modules/data/ByteData.h"

#include "modules/image/ImageData.h"

using love::math::Transform;

namespace love
{
namespace love_assimp
{

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
    // replacing all references to nodes with references to tables in this list
    lua_createtable(L, nodelist.size(), nodelist.size()/2);
    for(int i = 0; i < nodelist.size(); i++) {
        auto node = nodelist[i];
        convert(L, node);
        // refer to the node in the array
        lua_pushinteger(L, i+1);
        lua_pushvalue(L, -2);
        lua_settable(L, -4);
        // and make it accessable by name
        lua_pushlstring(L, node->mName.data, node->mName.length);
        lua_pushvalue(L, -2);
        lua_settable(L, -4);
        // Stack:
        // -2: node list table
        // -1: current node table
        lua_pop(L, 1); // remove the extra reference to the node table
    }

    for(int i = 0; i < nodelist.size(); i++) {
        auto node = nodelist[i];
        lua_pushinteger(L, i+1);
        lua_gettable(L, -2);
        // Stack:
        // -2: node list table
        // -1: current node table
        if (node->mParent != nullptr) {
            unsigned int parent_idx = node_indices[node->mParent];
            lua_pushinteger(L, parent_idx);
            // Stack:
            // -3: node list table
            // -2: current node table
            // -1: parent node index
            lua_gettable(L, -3); // push the node table of the parent
            // Stack:
            // -3: node list table
            // -2: current node table
            // -1: parent node table
            lua_setfield(L, -2, "parent");
        }

        lua_createtable(L, node->mNumChildren, 0); // children table list
        for(unsigned int j = 0; j < node->mNumChildren; j++) {
            unsigned int child_idx = node_indices[node->mChildren[j]];
            lua_pushinteger(L, j+1); // list insertion index
            lua_pushinteger(L, child_idx);
            // Stack:
            // -5: node list table
            // -4: current node table
            // -3: children table
            // -2: insertion index
            // -1: child node index
            lua_gettable(L, -5);
            // Stack:
            // -5: node list table
            // -4: current node table
            // -3: children table
            // -2: insertion index
            // -1: child node table
            lua_settable(L, -3);
        }
        // Stack:
        // -3: node list table
        // -2: current node table
        // -1: children table
        lua_setfield(L, -2, "children");
        lua_pop(L, 1); // pop the current node table
        // Stack:
        // -1: node list table
        // We made it! We didn't bloat the stack!
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

    // Stack:
    // -2: node list table
    // -1: scene table
    lua_pushvalue(L, -2);
    lua_setfield(L, -2, "nodes");

    lua_pushinteger(L, 1); // root node index will always be 1
    lua_gettable(L, -3); // push the root node table from the node list table
    lua_setfield(L, -2, "root_node");

    lua_remove(L, -2); // remove the nodes table, since it bloats the stack. We don't want to leak memory.

    lua_createtable(L, scene->mNumMeshes, 0);
    for(unsigned int i = 0; i < scene->mNumMeshes; i++) {
        lua_pushinteger(L, i+1);
        convert(L, scene->mMeshes[i]);
        lua_settable(L, -3);
    }
    lua_setfield(L, -2, "meshes");

    lua_createtable(L, scene->mNumTextures, 0);
    for (unsigned int i = 0; i < scene->mNumTextures; i++) {
        lua_pushinteger(L, i+1);
        convert(L, scene->mTextures[i]);
        lua_settable(L, -3);
    }
    lua_setfield(L, -2, "textures");
    
    lua_createtable(L, scene->mNumMaterials, 0);
    for (unsigned int i = 0; i < scene->mNumMaterials; i++) {
        lua_pushinteger(L, i+1);
        convert(L, scene->mMaterials[i]);
        lua_settable(L, -3);
    }
    lua_setfield(L, -2, "materials");
    
    lua_createtable(L, scene->mNumLights, 0);
    for (unsigned int i = 0; i < scene->mNumLights; i++) {
        lua_pushinteger(L, i+1);
        convert(L, scene->mLights[i]);
        lua_settable(L, -3);
    }
    lua_setfield(L, -2, "lights");

    lua_createtable(L, scene->mNumCameras, 0);
    for (unsigned int i = 0; i < scene->mNumCameras; i++) {
        lua_pushinteger(L, i+1);
        convert(L, scene->mCameras[i]);
        lua_settable(L, -3);
    }
    lua_setfield(L, -2, "cameras");

    convert(L, scene->mMetaData);
    lua_setfield(L, -2, "metadata");
    
    return 1;
}

int AssimpModule::convert(lua_State *L, const aiNode *node)
{
    lua_createtable(L, 0, 4);
    
    lua_pushlstring(L, node->mName.data, node->mName.length);
    lua_setfield(L, -2, "name");

    convert(L, &node->mTransformation);
    lua_setfield(L, -2, "transform");

    convert(L, node->mMetaData);
    lua_setfield(L, -2, "metadata");

    lua_createtable(L, node->mNumMeshes, 0);
    for(int i = 0; i < node->mNumMeshes; i++) {
        lua_pushinteger(L, i+1);
        lua_pushinteger(L, node->mMeshes[i]);
        lua_settable(L, -3);
    }
    lua_setfield(L, -2, "meshes");

    return 1;
}

int AssimpModule::convert(lua_State *L, const aiMesh *mesh)
{
    using namespace love::graphics;
    Graphics *g = Module::getInstance<Graphics>(Module::M_GRAPHICS);

    lua_newtable(L); // Mesh
    
    lua_pushlstring(L, mesh->mName.data, mesh->mName.length);
    lua_setfield(L, -2, "name");

    convert(L, &mesh->mAABB);
    lua_setfield(L, -2, "aabb");

    lua_newtable(L);
    if ((mesh->mPrimitiveTypes & aiPrimitiveType_POINT) != 0) {
        lua_pushstring(L, "point");
        lua_pushboolean(L, true);
        lua_settable(L, -3);
    }
    if ((mesh->mPrimitiveTypes & aiPrimitiveType_LINE) != 0) {
        lua_pushstring(L, "line");
        lua_pushboolean(L, true);
        lua_settable(L, -3);
    }
    if ((mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) != 0) {
        lua_pushstring(L, "triangle");
        lua_pushboolean(L, true);
        lua_settable(L, -3);
    }
    if ((mesh->mPrimitiveTypes & aiPrimitiveType_POLYGON) != 0) {
        lua_pushstring(L, "polygon");
        lua_pushboolean(L, true);
        lua_settable(L, -3);
    }
    if ((mesh->mPrimitiveTypes & aiPrimitiveType_NGONEncodingFlag) != 0) {
        lua_pushstring(L, "ngon");
        lua_pushboolean(L, true);
        lua_settable(L, -3);
    }
    lua_setfield(L, -2, "primitives");

    lua_pushinteger(L, mesh->mMaterialIndex);
    lua_setfield(L, -2, "material_index");

    // TODO test
    
    // To construct a love mesh manually, we need this data:
    // 1. vertexformat: vector<AttribFormat>
    // 2. data: pointer to contiguous memory holding data according to vertexformat 
    // 3. datasize: length of data in bytes
    // 4. drawmode: triangles, fan, strip, or points
    // 5. usage: stream, dynamic, static
    //
    // For #2, the data memory is copied with memcpy (in the opengl implementation),
    // so you are safe to free the memory after creating the mesh.

    struct aiLoveVertex
    {
        float x, y, z;
        float u, v;
        float nx, ny, nz;
        float tx, ty, tz;
        float bx, by, bz;
        uint8_t r, g, b, a;
    };

    // size_t data_size = mesh->mNumVertices * sizeof(aiLoveVertex);
    // void *data = calloc(mesh->mNumVertices, sizeof(aiLoveVertex));
    // if (data == nullptr) {
    //     throw love::Exception("Out of memory.");
    // }
    auto vertices = std::vector<aiLoveVertex>();
    vertices.reserve(mesh->mNumVertices);
    
    for(unsigned int i = 0; i < mesh->mNumVertices; i++) {
        aiLoveVertex v;
        // Position
        v.x = mesh->mVertices[i].x;
        v.y = mesh->mVertices[i].y;
        v.z = mesh->mVertices[i].z;
        // TexCoord
        if (mesh->mTextureCoords[0] != nullptr) {
            v.u = mesh->mTextureCoords[0][i].x;
            v.v = mesh->mTextureCoords[0][i].y;
        } else {
            v.u = 0;
            v.v = 0;
        }
        // Normal
        if (mesh->mNormals != nullptr) {
            aiVector3D aiv = mesh->mNormals[i];
            v.nx = aiv.x;
            v.ny = aiv.y;
            v.nz = aiv.z;
        } else {
            v.nx = 0;
            v.ny = 0;
            v.nz = 0;
        }
        if (mesh->mTangents != nullptr) {
            // Tangent
            aiVector3D t = mesh->mTangents[i];
            v.tx = t.x;
            v.ty = t.y;
            v.tz = t.z;
            // Bitangent
            t = mesh->mBitangents[i];
            v.bx = t.x;
            v.by = t.y;
            v.bz = t.z;
        } else {
            v.tx = 0;
            v.ty = 0;
            v.tz = 0;
            v.bx = 0;
            v.by = 0;
            v.bz = 0;
        }

        // Color
        if (mesh->mColors != nullptr && mesh->HasVertexColors(0)) {
            aiColor4D col = mesh->mColors[0][i];
            v.r = col.r;
            v.g = col.g;
            v.b = col.b;
            v.a = col.a;
        } else {
            v.r = 1;
            v.g = 1;
            v.b = 1;
            v.a = 1;
        }

        vertices.push_back(v);
    }

    Mesh *lovemesh = g->newMesh(mesh_format, vertices.data(), vertices.size()*sizeof(aiLoveVertex), PRIMITIVE_TRIANGLES, vertex::USAGE_STATIC);
    luax_pushtype(L, lovemesh);
    lua_setfield(L, -2, "mesh");
    lovemesh->release();

    return 1;
}

int AssimpModule::convert(lua_State *L, const aiFace *face)
{
    lua_createtable(L, face->mNumIndices, 0);
    for(unsigned int i = 0; i < face->mNumIndices; i++) {
        lua_pushinteger(L, i+1);
        lua_pushinteger(L, face->mIndices[i]);
        lua_settable(L, -3);
    }
    return 1;
}

int AssimpModule::convert(lua_State *L, const aiAABB *aabb)
{
    lua_createtable(L, 0, 2);

    convert(L, &aabb->mMin);
    lua_setfield(L, -2, "min");

    convert(L, &aabb->mMax);
    lua_setfield(L, -2, "max");

    return 1;
}

int AssimpModule::convert(lua_State *L, const aiMaterial *mat)
{
    lua_createtable(L, mat->mNumProperties, mat->mNumProperties);
    for(unsigned int i = 0; i < mat->mNumProperties; i++) {
        auto prop = mat->mProperties[i];
        convert(L, prop); // +1
        // First add this property to the array
        lua_pushinteger(L, i+1);
        lua_pushvalue(L, -2);
        lua_settable(L, -4);
        // Then add it to the dictionary
        lua_pushlstring(L, prop->mKey.data, prop->mKey.length);
        lua_pushvalue(L, -2);
        lua_settable(L, -4);
        // Pop the extra property table reference
        lua_pop(L, 1);
    }
    return 1;
}

int AssimpModule::convert(lua_State *L, const aiMaterialProperty *prop)
{
    lua_newtable(L);

    lua_pushlstring(L, prop->mKey.data, prop->mKey.length);
    lua_setfield(L, -2, "name");

    lua_pushinteger(L, prop->mIndex);
    lua_setfield(L, -2, "index");

    switch (prop->mType) {
        case aiPropertyTypeInfo::aiPTI_Float:
        case aiPropertyTypeInfo::aiPTI_Double:
        case aiPropertyTypeInfo::aiPTI_Integer:
            lua_pushstring(L, "number");
            break;
        case aiPropertyTypeInfo::aiPTI_String:
            lua_pushstring(L, "string");
            break;
        default:
            lua_pushstring(L, "raw");
            break;
    }
    lua_setfield(L, -2, "type");
    
    switch (prop->mSemantic) {
        case aiTextureType_DIFFUSE:
            lua_pushstring(L, "diffuse");
            break;
        case aiTextureType_SPECULAR:
            lua_pushstring(L, "specular");
            break;
        case aiTextureType_AMBIENT:
            lua_pushstring(L, "ambient");
            break;
        case aiTextureType_EMISSIVE:
            lua_pushstring(L, "emissive");
            break;
        case aiTextureType_HEIGHT:
            lua_pushstring(L, "height");
            break;
        case aiTextureType_NORMALS:
            lua_pushstring(L, "normals");
            break;
        case aiTextureType_SHININESS:
            lua_pushstring(L, "shininess");
            break;
        case aiTextureType_OPACITY:
            lua_pushstring(L, "opacity");
            break;
        case aiTextureType_DISPLACEMENT:
            lua_pushstring(L, "displacement");
            break;
        case aiTextureType_LIGHTMAP:
            lua_pushstring(L, "lightmap");
            break;
        case aiTextureType_REFLECTION:
            lua_pushstring(L, "reflection");
            break;
        case aiTextureType_BASE_COLOR:
            lua_pushstring(L, "base_color");
            break;
        case aiTextureType_NORMAL_CAMERA:
            lua_pushstring(L, "normal_camera");
            break;
        case aiTextureType_EMISSION_COLOR:
            lua_pushstring(L, "emission_color");
            break;
        case aiTextureType_METALNESS:
            lua_pushstring(L, "metalness");
            break;
        case aiTextureType_DIFFUSE_ROUGHNESS:
            lua_pushstring(L, "diffuse_roughness");
            break;
        case aiTextureType_AMBIENT_OCCLUSION:
            lua_pushstring(L, "ambient_occlusion");
            break;
        case aiTextureType_SHEEN:
            lua_pushstring(L, "sheen");
            break;
        case aiTextureType_CLEARCOAT:
            lua_pushstring(L, "clearcoat");
            break;
        case aiTextureType_TRANSMISSION:
            lua_pushstring(L, "transmission");
            break;
        case aiTextureType_UNKNOWN:
            lua_pushstring(L, "unknown");
            break;
        default:
            lua_pushboolean(L, false);
            break;
    }
    lua_setfield(L, -2, "texture_type");
    auto bytedata = new love::data::ByteData(prop->mData, prop->mDataLength);
    luax_pushtype(L, bytedata);
    bytedata->release();
    lua_setfield(L, -2, "data");

    return 1;
}

int AssimpModule::convert(lua_State *L, const aiTexture *texture)
{
    using namespace graphics;

    Graphics *g = Module::getInstance<Graphics>(Module::M_GRAPHICS);
    
    if(g == nullptr) {
        throw new love::Exception("Graphics module must be loaded to load textures");
    }

    if (texture->mHeight > 0) {
        Image::Settings settings;
        Image *img = g->newImage(TEXTURE_2D, PIXELFORMAT_RGBA8, texture->mWidth, texture->mHeight, 1, settings);
        const unsigned int pixelcount = texture->mWidth * texture->mHeight;
        unsigned char *newdata = (unsigned char *) calloc(pixelcount, 4);
        if(newdata == nullptr) {
            throw love::Exception("Out of memory.");
        }
        for (unsigned int i = 0; i < pixelcount; i++) {
            auto texel = texture->pcData[i];
            *(newdata+i*4+0) = texel.r;
            *(newdata+i*4+1) = texel.g;
            *(newdata+i*4+2) = texel.b;
            *(newdata+i*4+3) = texel.a;
        }
        img->replacePixels(newdata, pixelcount*4, 0, 0, Rect{0, 0, (int) texture->mWidth, (int) texture->mHeight}, true);
        free(newdata);
        luax_pushtype(L, img);
        img->release();
        return 1;
    } else {
        data::ByteData data(texture->pcData, texture->mWidth, false);
        image::ImageData *imgdata = new image::ImageData(&data);
        Image::Slices slices(TEXTURE_2D);
        slices.set(0, 0, imgdata);
        Image::Settings settings;
        Image *img = g->newImage(slices, settings);
        luax_pushtype(L, img);
        img->release();
        return 1;
    }
    // lua_pushnil(L);
    // return 1;
}

int AssimpModule::convert(lua_State *L, const aiAnimation *anim)
{
    lua_newtable(L);

    lua_pushlstring(L, anim->mName.data, anim->mName.length);
    lua_setfield(L, -2, "name");

    lua_pushnumber(L, anim->mDuration);
    lua_setfield(L, -2, "duration");

    lua_pushnumber(L, anim->mTicksPerSecond);
    lua_setfield(L, -2, "fps");

    lua_createtable(L, anim->mNumChannels, 0);
    for(unsigned int i = 0; i < anim->mNumChannels; i++) {
        lua_pushinteger(L, i+1);
        convert(L, anim->mChannels[i]);
        lua_settable(L, -3);
    }
    lua_setfield(L, -2, "node_channels");

    lua_createtable(L, anim->mNumMeshChannels, 0);
    for(unsigned int i = 0; i < anim->mNumMeshChannels; i++) {
        lua_pushinteger(L, i+1);
        convert(L, anim->mMeshChannels[i]);
        lua_settable(L, -3);
    }
    lua_setfield(L, -2, "mesh_channels");

    lua_createtable(L, anim->mNumMorphMeshChannels, 0);
    for(unsigned int i = 0; i < anim->mNumMorphMeshChannels; i++) {
        lua_pushinteger(L, i+1);
        convert(L, anim->mMorphMeshChannels[i]);
        lua_settable(L, -3);
    }
    lua_setfield(L, -2, "morph_channels");

    return 1;
}

int AssimpModule::convert(lua_State *L, const aiAnimBehaviour behavior) {
    switch (behavior) {
        case aiAnimBehaviour_CONSTANT:
            lua_pushstring(L, "constant");
            break;
        case aiAnimBehaviour_LINEAR:
            lua_pushstring(L, "linear");
            break;
        case aiAnimBehaviour_REPEAT:
            lua_pushstring(L, "repeat");
            break;
        default:
            lua_pushstring(L, "default");
            break;
    }
    return 1;
}

int AssimpModule::convert(lua_State *L, const aiNodeAnim *anim)
{
    lua_newtable(L);

    lua_pushlstring(L, anim->mNodeName.data, anim->mNodeName.length);
    lua_setfield(L, -2, "node_name");

    convert(L, anim->mPreState);
    lua_setfield(L, -2, "pre_state");

    convert(L, anim->mPostState);
    lua_setfield(L, -2, "post_state");

    lua_createtable(L, anim->mNumPositionKeys, 0); // times
    lua_createtable(L, anim->mNumPositionKeys, 0); // values
    for(unsigned int i = 0; i < anim->mNumPositionKeys; i++) {
        auto key = anim->mPositionKeys[i];
        lua_pushinteger(L, i+1);
        lua_pushnumber(L, key.mTime);
        lua_settable(L, -4);

        lua_pushinteger(L, i+1);
        convert(L, &key.mValue);
        lua_settable(L, -3);
    }
    lua_setfield(L, -3, "position_keys");
    lua_setfield(L, -2, "position_times");

    lua_createtable(L, anim->mNumRotationKeys, 0); // times
    lua_createtable(L, anim->mNumRotationKeys, 0); // values
    for(unsigned int i = 0; i < anim->mNumRotationKeys; i++) {
        auto key = anim->mRotationKeys[i];
        lua_pushinteger(L, i+1);
        lua_pushnumber(L, key.mTime);
        lua_settable(L, -4);

        lua_pushinteger(L, i+1);
        convert(L, &key.mValue);
        lua_settable(L, -3);
    }
    lua_setfield(L, -3, "rotation_keys");
    lua_setfield(L, -2, "rotation_times");

    lua_createtable(L, anim->mNumScalingKeys, 0); // times
    lua_createtable(L, anim->mNumScalingKeys, 0); // values
    for(unsigned int i = 0; i < anim->mNumScalingKeys; i++) {
        auto key = anim->mPositionKeys[i];
        lua_pushinteger(L, i+1);
        lua_pushnumber(L, key.mTime);
        lua_settable(L, -4);

        lua_pushinteger(L, i+1);
        convert(L, &key.mValue);
        lua_settable(L, -3);
    }
    lua_setfield(L, -3, "scale_keys");
    lua_setfield(L, -2, "scale_times");

    return 1;
}

int AssimpModule::convert(lua_State *L, const aiMeshAnim *anim)
{
    lua_newtable(L);
    
    lua_pushlstring(L, anim->mName.data, anim->mName.length);
    lua_setfield(L, -2, "mesh_name");

    lua_createtable(L, anim->mNumKeys, 0); // times
    lua_createtable(L, anim->mNumKeys, 0); // values
    for(unsigned int i = 0; i < anim->mNumKeys; i++) {
        auto key = anim->mKeys[i];
        lua_pushinteger(L, i+1);
        lua_pushnumber(L, key.mTime);
        lua_settable(L, -4);

        lua_pushinteger(L, i+1);
        lua_pushinteger(L, key.mValue);
        lua_settable(L, -3);
    }
    lua_setfield(L, -3, "keys");
    lua_setfield(L, -2, "times");

    return 1;
}

int AssimpModule::convert(lua_State *L, const aiMeshMorphAnim *anim)
{
    lua_newtable(L);

    lua_pushlstring(L, anim->mName.data, anim->mName.length);
    lua_setfield(L, -2, "mesh_name");
    
    lua_createtable(L, anim->mNumKeys, 0);
    for (unsigned int i = 0; i < anim->mNumKeys; i++) {
        lua_pushinteger(L, i+1);
        lua_pushnumber(L, anim->mKeys[i].mTime);
        lua_settable(L, -3);
    }
    lua_setfield(L, -2, "times");
    
    lua_createtable(L, anim->mNumKeys, 0);
    for (unsigned int i = 0; i < anim->mNumKeys; i++) {
        auto key = anim->mKeys[i];
        lua_pushinteger(L, i+1);
        lua_createtable(L, key.mNumValuesAndWeights, 0);
        for(unsigned int j; j < key.mNumValuesAndWeights; j++) {
            lua_pushinteger(L, j+1);
            lua_pushinteger(L, key.mValues[j]);
            lua_settable(L, -3);
        }
        lua_settable(L, -3);
    }
    lua_setfield(L, -2, "values");
    
    lua_createtable(L, anim->mNumKeys, 0);
    for (unsigned int i = 0; i < anim->mNumKeys; i++) {
        auto key = anim->mKeys[i];
        lua_pushinteger(L, i+1);
        lua_createtable(L, key.mNumValuesAndWeights, 0);
        for(unsigned int j; j < key.mNumValuesAndWeights; j++) {
            lua_pushinteger(L, j+1);
            lua_pushnumber(L, key.mWeights[j]);
            lua_settable(L, -3);
        }
        lua_settable(L, -3);
    }
    lua_setfield(L, -2, "weights");

    return 1;
}

int AssimpModule::convert(lua_State *L, const aiBone *bone)
{
    lua_newtable(L);
    
    lua_pushlstring(L, bone->mName.data, bone->mName.length);
    lua_setfield(L, -2, "name");

    convert(L, &bone->mOffsetMatrix);
    lua_setfield(L, -2, "offset");

    lua_createtable(L, bone->mNumWeights, 0);
    for (unsigned int i = 0; i < bone->mNumWeights; i++) {
        lua_pushinteger(L, i+1);
        lua_pushinteger(L, bone->mWeights[i].mVertexId);
        lua_settable(L, -3);
    }
    lua_setfield(L, -2, "vertex_ids");

    lua_createtable(L, bone->mNumWeights, 0);
    for (unsigned int i = 0; i < bone->mNumWeights; i++) {
        lua_pushinteger(L, i+1);
        lua_pushnumber(L, bone->mWeights[i].mWeight);
        lua_settable(L, -3);
    }
    lua_setfield(L, -2, "weights");

    return 1;
}

int AssimpModule::convert(lua_State *L, const aiLight *light)
{
    lua_newtable(L);

    lua_pushlstring(L, light->mName.data, light->mName.length);
    lua_setfield(L, -2, "node_name");

    switch(light->mType) {
        case aiLightSource_DIRECTIONAL:
        lua_pushstring(L, "directional");
        break;
        case aiLightSource_POINT:
        lua_pushstring(L, "point");
        break;
        case aiLightSource_SPOT:
        lua_pushstring(L, "spot");
        break;
        case aiLightSource_AMBIENT:
        lua_pushstring(L, "ambient");
        break;
        case aiLightSource_AREA:
        lua_pushstring(L, "area");
        break;
        default:
        lua_pushstring(L, "");
    }
    lua_setfield(L, -2, "type");

    convert(L, &light->mPosition);
    lua_setfield(L, -2, "position");
    
    convert(L, &light->mSize);
    lua_setfield(L, -2, "size");

    convert(L, &light->mDirection);
    lua_setfield(L, -2, "forward");

    convert(L, &light->mUp);
    lua_setfield(L, -2, "up");

    convert(L, &light->mColorAmbient);
    lua_setfield(L, -2, "ambient");

    convert(L, &light->mColorDiffuse);
    lua_setfield(L, -2, "diffuse");

    convert(L, &light->mColorSpecular);
    lua_setfield(L, -2, "specualar");

    lua_pushnumber(L, light->mAngleInnerCone);
    lua_setfield(L, -2, "inner_cone_angle");

    lua_pushnumber(L, light->mAngleOuterCone);
    lua_setfield(L, -2, "outer_cone_angle");

    lua_pushnumber(L, light->mAttenuationConstant);
    lua_setfield(L, -2, "attenuation_constant");

    lua_pushnumber(L, light->mAttenuationLinear);
    lua_setfield(L, -2, "attenuation_linear");

    lua_pushnumber(L, light->mAttenuationQuadratic);
    lua_setfield(L, -2, "attenuation_quadratic");

    return 1;
}

int AssimpModule::convert(lua_State *L, const aiCamera *camera)
{
    lua_newtable(L);
    
    lua_pushlstring(L, camera->mName.data, camera->mName.length);
    lua_setfield(L, -2, "node_name");

    convert(L, &camera->mPosition);
    lua_setfield(L, -2, "position");

    convert(L, &camera->mLookAt);
    lua_setfield(L, -2, "forward");

    convert(L, &camera->mUp);
    lua_setfield(L, -2, "up");

    lua_pushnumber(L, camera->mAspect);
    lua_setfield(L, -2, "aspect");

    bool isOrthographic = camera->mOrthographicWidth != 0.0f;
    if (isOrthographic) {
        lua_pushnumber(L, camera->mOrthographicWidth);
        lua_setfield(L, -2, "fov");
    } else {
        lua_pushnumber(L, camera->mHorizontalFOV);
        lua_setfield(L, -2, "fov");
    }
    
    luax_pushboolean(L, isOrthographic);
    lua_setfield(L, -2, "orthographic");

    lua_pushnumber(L, camera->mClipPlaneNear);
    lua_setfield(L, -2, "nearclip");

    lua_pushnumber(L, camera->mClipPlaneFar);
    lua_setfield(L, -2, "farclip");
    
    return 1;
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
    luax_pushtype(L, t);
    t->release();
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
    luax_pushtype(L, t);
    t->release();
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