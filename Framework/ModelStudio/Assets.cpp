#include "Assets.h"
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <algorithm>
namespace studio
{
std::string lower(std::string s)
{
    for (auto &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::vector<uint8_t> readBytes(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f)
        throw std::runtime_error("Cannot open file");
    auto n = f.tellg();
    if (n < 0 || n > 512 * 1024 * 1024)
        throw std::runtime_error("Invalid file size (limit: 512 MiB)");
    std::vector<uint8_t> b(static_cast<size_t>(n));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char *>(b.data()), n))
        throw std::runtime_error("File read failed");
    return b;
}
struct Reader
{
    const std::vector<uint8_t> &b;
    void range(size_t p, size_t n) const
    {
        if (p > b.size() || n > b.size() - p)
            throw std::runtime_error("Truncated resource");
    }
    template <class T> T get(size_t p) const
    {
        range(p, sizeof(T));
        T v;
        memcpy(&v, b.data() + p, sizeof(T));
        return v;
    }
    std::string name(size_t p) const
    {
        range(p, 64);
        std::string n;
        for (size_t i = 0; i < 64 && b[p + i]; ++i)
        {
            auto c = b[p + i];
            if (c < 32 || c > 126)
                throw std::runtime_error("Unsupported bone name");
            n += static_cast<char>(c);
        }
        if (n.empty())
            throw std::runtime_error("Empty bone name");
        return lower(n);
    }
    bool affine(size_t p) const
    {
        range(p, 64);
        for (size_t k = 0; k < 16; ++k)
        {
            float x = get<float>(p + k * 4);
            if (!std::isfinite(x) || fabsf(x) > 1e8f)
                return false;
        }
        return fabsf(get<float>(p + 12)) < .001f && fabsf(get<float>(p + 28)) < .001f &&
               fabsf(get<float>(p + 44)) < .001f && fabsf(get<float>(p + 60) - 1) < .001f;
    }
};
Model loadModel(const fs::path &p)
{
    auto bytes = readBytes(p);
    Reader r{bytes};
    r.range(0, 27);
    Model m;
    m.path = p;
    auto count = r.get<uint8_t>(0);
    m.materials = r.get<uint16_t>(1);
    if (!count || !m.materials || m.materials > 32)
        throw std::runtime_error("Unsupported BSC header");
    size_t o = 15 + 12 * m.materials;
    for (unsigned i = 0; i < count; ++i)
    {
        r.range(o, 98);
        Mesh s;
        s.bone = r.name(o);
        s.material = r.get<uint16_t>(o + 64);
        auto nv = r.get<uint32_t>(o + 66), ni = r.get<uint32_t>(o + 70);
        if (nv > 1000000 || ni > 3000000 || ni % 3 || s.material >= m.materials)
            throw std::runtime_error("Invalid BSC geometry header");
        o += 98;
        r.range(o, size_t(nv) * 44 + size_t(ni) * 2);
        s.vertices.resize(nv);
        for (auto &v : s.vertices)
        {
            v.position = r.get<XMFLOAT3>(o);
            v.normal = r.get<XMFLOAT3>(o + 12);
            v.uv = r.get<XMFLOAT2>(o + 28);
            o += 44;
            for (float x :
                 {v.position.x, v.position.y, v.position.z, v.normal.x, v.normal.y, v.normal.z, v.uv.x, v.uv.y})
                if (!std::isfinite(x))
                    throw std::runtime_error("Nonfinite vertex");
        }
        s.indices.resize(ni);
        for (auto &idx : s.indices)
        {
            idx = r.get<uint16_t>(o);
            o += 2;
            if (idx >= nv)
                throw std::runtime_error("BSC index out of bounds");
        }
        m.meshes.push_back(std::move(s));
    }
    if (o != bytes.size())
        throw std::runtime_error("Unsupported BSC variant: trailing data");
    return m;
}
Animation loadAnimation(const fs::path &p)
{
    auto bytes = readBytes(p);
    Reader r{bytes};
    r.range(0, 78);
    auto count = r.get<uint8_t>(8), nc = r.get<uint8_t>(11);
    if (!count || !nc)
        throw std::runtime_error("Invalid BON header");
    Animation a;
    a.path = p;
    for (unsigned i = 0; i < nc; ++i)
    {
        auto n = r.get<uint32_t>(74 + i * 4);
        if (!n || n > 100000 || a.frames > 100000 - n)
            throw std::runtime_error("Invalid BON clips");
        a.clips.push_back({a.frames, n});
        a.frames += n;
    }
    size_t stride = 68 + size_t(a.frames) * 64, begin = 74 + nc * 4, total = count * stride;
    if (total > bytes.size() || begin > bytes.size() - total)
        throw std::runtime_error("BON stream exceeds file");
    size_t start = SIZE_MAX;
    for (size_t pos = begin; pos <= bytes.size() - total; ++pos)
    {
        auto c = bytes[pos];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')))
            continue;
        try
        {
            if (r.name(pos).size() < 2 || !r.affine(pos + 68) || !r.affine(pos + (count - 1) * stride + 68))
                continue;
            bool good = true;
            for (size_t k = 0; k < count; ++k)
            {
                r.name(pos + k * stride);
                if (!r.affine(pos + k * stride + 68))
                {
                    good = false;
                    break;
                }
            }
            if (good)
            {
                start = pos;
                break;
            }
        }
        catch (const std::exception &)
        {
        }
    }
    if (start == SIZE_MAX)
        throw std::runtime_error("Unsupported BON matrix stream");
    for (size_t i = 0; i < count; ++i)
    {
        auto pos = start + i * stride;
        auto name = r.name(pos);
        if (a.bones.count(name))
            throw std::runtime_error("Duplicate BON bone");
        std::vector<XMFLOAT4X4> mats(a.frames);
        for (size_t f = 0; f < a.frames; ++f)
        {
            auto off = pos + 68 + f * 64;
            if (!r.affine(off))
                throw std::runtime_error("Invalid BON matrix");
            mats[f] = r.get<XMFLOAT4X4>(off);
        }
        a.bones.emplace(name, std::move(mats));
    }
    return a;
}
XMMATRIX Animation::matrix(const std::string &name, uint32_t frame) const
{
    auto it = bones.find(lower(name));
    return it == bones.end() ? XMMatrixIdentity() : XMLoadFloat4x4(&it->second[std::min(frame, frames - 1)]);
}
std::vector<std::string> missingBones(const Model &m, const Animation &a)
{
    std::vector<std::string> n;
    for (auto &s : m.meshes)
        if (!a.bones.count(s.bone))
            n.push_back(s.bone);
    return n;
}
fs::path companionTexture(const fs::path &mesh, int material)
{
    if (material > 3)
        return {};
    for (auto quality : {L'0', L'2'})
    {
        std::wstring ext = L".b";
        ext += wchar_t(L'a' + material);
        ext += quality;
        auto p = mesh;
        p.replace_extension(ext);
        if (fs::exists(p))
            return p;
    }
    return {};
}
std::string characterClass(const fs::path &p)
{
    auto n = lower(p.stem().string());
    for (auto c : {"az", "gr", "hs"})
        if (n.rfind(std::string(c) + "_", 0) == 0)
            return c;
    return {};
}
fs::path companionAnimation(const fs::path &mesh)
{
    auto p = mesh;
    p.replace_extension(L".bon");
    if (fs::exists(p))
        return p;
    auto c = characterClass(mesh);
    if (!c.empty())
    {
        p = mesh.parent_path() / (c + "_bone.bon");
        if (fs::exists(p))
            return p;
    }
    return {};
}
} // namespace studio
