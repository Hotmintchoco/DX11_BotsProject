#pragma once
#include <DirectXMath.h>
#include <filesystem>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>
namespace studio
{
namespace fs = std::filesystem;
using namespace DirectX;
struct Vertex
{
    XMFLOAT3 position, normal;
    XMFLOAT2 uv;
};
struct Mesh
{
    std::string bone;
    uint32_t material{};
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
};
struct Model
{
    fs::path path;
    uint32_t materials{};
    std::vector<Mesh> meshes;
};
struct Clip
{
    uint32_t start{}, count{};
};
struct Animation
{
    fs::path path;
    uint32_t frames{};
    std::vector<Clip> clips;
    std::unordered_map<std::string, std::vector<XMFLOAT4X4>> bones;
    XMMATRIX matrix(const std::string &name, uint32_t frame) const;
};
std::string lower(std::string s);
std::vector<uint8_t> readBytes(const fs::path &p);
Model loadModel(const fs::path &p);
Animation loadAnimation(const fs::path &p);
std::vector<std::string> missingBones(const Model &m, const Animation &a);
fs::path companionTexture(const fs::path &mesh, int material);
fs::path companionAnimation(const fs::path &mesh);
std::string characterClass(const fs::path &p);
} // namespace studio
