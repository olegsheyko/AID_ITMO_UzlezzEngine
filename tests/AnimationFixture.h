#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdint>

// Technical import fixture, not a demo character: one triangle, two joints, one clip.
inline std::filesystem::path writeAnimationFixture(const std::filesystem::path& directory) {
    using nlohmann::json;
    std::filesystem::create_directories(directory);
    std::vector<char> bytes;
    json views=json::array(), accessors=json::array();
    auto add = [&](const auto& values, int component, const char* type, int count) {
        while (bytes.size()%4) bytes.push_back(0);
        const size_t start=bytes.size();
        const auto* data=reinterpret_cast<const char*>(values.data());
        bytes.insert(bytes.end(),data,data+values.size()*sizeof(values[0]));
        views.push_back({{"buffer",0},{"byteOffset",start},{"byteLength",bytes.size()-start}});
        accessors.push_back({{"bufferView",views.size()-1},{"componentType",component},{"count",count},{"type",type}});
        return accessors.size()-1;
    };
    const auto position=add(std::vector<float>{-.6f,0,0,-.2f,0,0,-.4f,0,.5f},5126,"VEC3",3);
    accessors[position]["min"]={-.6f,0,0}; accessors[position]["max"]={-.2f,0,.5f};
    const auto normal=add(std::vector<float>{0,-1,0,0,-1,0,0,-1,0},5126,"VEC3",3);
    const auto joints=add(std::vector<uint16_t>{0,1,0,0,0,1,0,0,0,1,0,0},5123,"VEC4",3);
    const auto weights=add(std::vector<float>{.25f,.75f,0,0,.25f,.75f,0,0,.25f,.75f,0,0},5126,"VEC4",3);
    const auto inverseBind=add(std::vector<float>{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,
        1,0,0,0,0,1,0,0,0,0,1,0,-.5f,0,0,1},5126,"MAT4",2);
    const auto time=add(std::vector<float>{0,1,2},5126,"SCALAR",3);
    accessors[time]["min"]={0}; accessors[time]["max"]={2};
    const auto translation=add(std::vector<float>{.5f,0,0,1.5f,0,0,.5f,0,0},5126,"VEC3",3);
    json file;
    file["asset"]={{"version","2.0"},{"generator","Uzlezz animation tests"}};
    file["scene"]=0; file["scenes"]=json::array({{{"nodes",{0}}}});
    file["nodes"]=json::array({
        {{"name","Root"},{"children",{1,2}}},
        {{"name","Triangle"},{"mesh",0},{"skin",0}},
        {{"name","Joint0"},{"children",{3}}},
        {{"name","Joint1"},{"translation",{.5,0,0}}}});
    file["meshes"]=json::array({{{"primitives",json::array({{
        {"attributes",{{"POSITION",position},{"NORMAL",normal},{"JOINTS_0",joints},{"WEIGHTS_0",weights}}},
        {"mode",4}}})}}});
    file["skins"]=json::array({{{"joints",{2,3}},{"skeleton",2},{"inverseBindMatrices",inverseBind}}});
    file["animations"]=json::array({{{"name","Slide"},
        {"samplers",json::array({{{"input",time},{"output",translation},{"interpolation","LINEAR"}}})},
        {"channels",json::array({{{"sampler",0},{"target",{{"node",3},{"path","translation"}}}}})}}});
    file["buffers"]=json::array({{{"uri","skin.bin"},{"byteLength",bytes.size()}}});
    file["bufferViews"]=views; file["accessors"]=accessors;
    std::ofstream binary(directory/"skin.bin",std::ios::binary); binary.write(bytes.data(),bytes.size());
    std::ofstream(directory/"skin.gltf") << file.dump(2);
    return directory/"skin.gltf";
}
