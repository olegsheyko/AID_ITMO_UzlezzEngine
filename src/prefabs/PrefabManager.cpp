#include "prefabs/PrefabManager.h"
#include "resources/ResourceManager.h"
#include "ecs/Components.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <limits>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace {
using Json=nlohmann::json;
Vec3 vector(const Json& j,Vec3 fallback){
    if(j.is_null())return fallback;
    if(!j.is_array()||j.size()!=3)throw std::runtime_error("Expected vector of three numbers");
    Vec3 v{j[0].get<float>(),j[1].get<float>(),j[2].get<float>()};
    if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))throw std::runtime_error("Non-finite vector");
    return v;
}
ScriptValue value(const Json& j){
    if(j.is_boolean())return j.get<bool>();
    if(j.is_number_integer()) {
        const auto x=j.get<double>();
        if(x<std::numeric_limits<int>::min()||x>std::numeric_limits<int>::max())throw std::runtime_error("Prefab integer exceeds int32 range");
        return j.get<int>();
    }
    if(j.is_number_float()) {
        const double x=j.get<double>();
        if(!std::isfinite(x))throw std::runtime_error("Prefab number must be finite");
        return x;
    }
    if(j.is_string())return j.get<std::string>();
    throw std::runtime_error("Prefab fields must be bool, int, float or string");
}
}
Entity PrefabManager::spawn(World& world,const std::string& path,bool renderResources){
    std::ifstream in(path);if(!in)throw std::runtime_error("Cannot open prefab: "+path);
    Json document;in>>document;
    const auto& c=document.at("components");
    const auto t=c.value("transform",Json::object());
    Transform transform{vector(t.value("position",Json()),{}),vector(t.value("rotation",Json()),{}),vector(t.value("scale",Json()),{1,1,1})};
    ScriptComponent script;
    if(c.contains("script")) {
        const auto& s=c.at("script");
        script.path=s.at("path").get<std::string>();script.className=s.at("class").get<std::string>();script.prefab=path;
        const auto fields=s.value("fields",Json::object());
        if(!fields.is_object())throw std::runtime_error("Prefab fields must be an object");
        for(const auto& [name,v]:fields.items())script.fields[name]=value(v);
    }
    Entity e=world.createEntity();
    try {
        world.addComponent<Tag>(e,Tag{c.value("tag",std::filesystem::path(path).stem().string())});
        world.addComponent<Transform>(e,transform);
        if(c.contains("script"))world.addComponent<ScriptComponent>(e,script);
        if(c.contains("animator")) {
            auto& a=world.addComponent<Animator>(e);a.speed=c.at("animator").value("speed",1.0f);
            a.paused=c.at("animator").value("paused",false);
            a.inPlaceNode=c.at("animator").value("inPlaceNode",std::string{});
        }
        if(c.contains("collider")) {
            const auto& j=c.at("collider");Collider collider;
            const auto type=j.value("type",std::string("box"));
            if(type!="box"&&type!="sphere")throw std::runtime_error("Unknown collider type");
            collider.type=type=="sphere"?ColliderType::Sphere:ColliderType::Box;
            collider.halfExtents=vector(j.value("halfExtents",Json()),{.5f,.5f,.5f});
            collider.offset=vector(j.value("offset",Json()),{});collider.radius=j.value("radius",.5f);
            world.addComponent<Collider>(e,collider);
        }
        if(c.contains("rigidbody")) {
            const auto& j=c.at("rigidbody");Rigidbody body;
            body.mass=j.value("mass",1.0f);body.useGravity=j.value("useGravity",true);
            body.velocity=vector(j.value("velocity",Json()),{});world.addComponent<Rigidbody>(e,body);
        }
        if(renderResources&&c.contains("meshRenderer")) {
            const auto& j=c.at("meshRenderer");auto& rm=ResourceManager::getInstance();
            MeshRenderer mesh;mesh.meshId=j.at("mesh").get<std::string>();
            const auto vertex=j.value("vertex",std::string("assets/shaders/mesh_vertex.glsl"));
            const auto fragment=j.value("fragment",std::string("assets/shaders/mesh_fragment_simple_texture.glsl"));
            mesh.shaderId=vertex+"|"+fragment;mesh.cachedMesh=rm.loadMeshAsync(mesh.meshId);
            mesh.cachedShader=rm.loadShader(vertex,fragment);
            if(j.contains("texture")){mesh.baseColorTextureId=j.at("texture").get<std::string>();mesh.cachedBaseColorTexture=rm.loadTextureAsync(mesh.baseColorTextureId);}
            mesh.colliderBoundsInitialized=true; // prefab collider is explicitly authored
            if(!mesh.cachedMesh||!mesh.cachedShader)throw std::runtime_error("Prefab resource request failed: "+path);
            world.addComponent<MeshRenderer>(e,mesh);
        }
    } catch(...) {world.destroyEntity(e);throw;}
    return e;
}
bool PrefabManager::saveFields(const ScriptComponent& component,std::string& error){
    try {
        if(component.prefab.empty())throw std::runtime_error("Entity has no source prefab");
        std::ifstream in(component.prefab);if(!in)throw std::runtime_error("Cannot read prefab");
        Json doc;in>>doc;in.close();
        auto& script=doc.at("components").at("script");
        if(script.at("path")!=component.path||script.at("class")!=component.className)throw std::runtime_error("Prefab script changed on disk; reload before saving");
        auto fields=Json::object();for(const auto& item:component.fields)std::visit([&](const auto& x){fields[item.first]=x;},item.second);
        script["fields"]=fields;
        const std::filesystem::path target(component.prefab),temp(component.prefab+".tmp");
        {std::ofstream out(temp,std::ios::binary|std::ios::trunc);out<<doc.dump(2)<<'\n';out.flush();if(!out)throw std::runtime_error("Cannot save prefab temporary file");}
#ifdef _WIN32
        if(!MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace prefab file");
#else
        std::filesystem::rename(temp,target);
#endif
        error.clear();return true;
    }catch(const std::exception& x){error=x.what();return false;}
}
