#include "Renderer.h"
#include <fstream>
#include <stdexcept>
#include <sstream>
#include <cmath>
using namespace studio;
int runVerification(const fs::path &root, const fs::path &out)
{
    fs::create_directories(out);
    std::ofstream log(out / L"verification.txt");
    HWND window = nullptr;
    try
    {
        window = CreateWindowExW(0, L"STATIC", L"ModelStudio verification", WS_POPUP, 0, 0, 960, 800, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
        if (!window)
            throw std::runtime_error("Cannot create verification window");
        Renderer renderer;
        renderer.initialize(window, true);
        for (const auto &name : {"az", "gr", "hs", "BloodKnight"})
        {
            std::array<Slot, 4> slots;
            std::wstring warning;
            fs::path model;
            bool player = std::string(name) != "BloodKnight";
            if (player)
            {
                std::wstring folder(name, name + 2);
                folder[0] = towupper(folder[0]);
                auto dir = root / L"플레이어" / folder;
                model = dir / (std::string(name) + "_000_body.bsc");
                int i = 1;
                for (auto part : {"body", "arm", "head"})
                    slots[i++] = renderer.loadSlot(dir / (std::string(name) + "_000_" + part + ".bsc"), warning);
            }
            else
            {
                model = root / L"monster" / L"BloodKnight.bsc";
                slots[0] = renderer.loadSlot(model, warning);
            }
            if (!warning.empty())
                throw std::runtime_error("Sample texture loading failed");
            auto animation = loadAnimation(companionAnimation(model));
            size_t meshCount = 0, vertexCount = 0, textureCount = 0;
            for (auto &s : slots)
                if (s.model)
                {
                    if (!missingBones(*s.model, animation).empty())
                        throw std::runtime_error("Missing sample bone");
                    for (auto &m : s.model->meshes)
                    {
                        meshCount++;
                        vertexCount += m.vertices.size();
                    }
                    for (auto &t : s.textures)
                        if (t.view)
                            textureCount++;
                    if (!s.textures[0].view)
                        throw std::runtime_error("Missing main texture");
                }
            if ((!player && meshCount != 41) || (player && textureCount < 3))
                throw std::runtime_error("Unexpected sample counts");
            renderer.fit(slots, &animation, 0);
            renderer.render(slots, &animation, 0, false);
            renderer.capture(out / (std::string(name) + "_frame0.png"));
            renderer.render(slots, &animation, 100, false);
            renderer.capture(out / (std::string(name) + "_frame100.png"));
            log << name << ": meshes=" << meshCount << " vertices=" << vertexCount << " textures=" << textureCount
                << " bones=" << animation.bones.size() << " frames=" << animation.frames
                << " clips=" << animation.clips.size() << "\n";
            // Verify the C++ matrix layout against independently exported JS coordinates.
            std::ifstream ref(out / (std::string(name) + "_reference.txt"));
            if (!ref)
                throw std::runtime_error("Missing independent reference data; run prepare-reference.cjs first");
            int slot, index, frame;
            float x, y, z;
            size_t samples = 0;
            float maxError = 0;
            while (ref >> slot >> index >> frame >> x >> y >> z)
            {
                auto &m = slots.at(slot).model->meshes.at(index);
                auto p =
                    XMVector3TransformCoord(XMLoadFloat3(&m.vertices[0].position), animation.matrix(m.bone, frame));
                XMFLOAT3 actual;
                XMStoreFloat3(&actual, p);
                auto error = std::max({fabsf(actual.x - x), fabsf(actual.y - y), fabsf(actual.z - z)});
                maxError = std::max(maxError, error);
                if (error > .002f)
                    throw std::runtime_error("Matrix reference mismatch");
                ++samples;
            }
            if (!samples)
                throw std::runtime_error("Empty reference data");
            log << "  reference samples=" << samples << " maxError=" << maxError << "\n";
            // Empty animation and replacement resources exercise the same rendering path.
            slots[player ? 1 : 0].textures[0] = {};
            renderer.wire = true;
            renderer.render(slots, nullptr, 0, false);
            renderer.wire = false;
        }
        size_t goodMesh = 0, goodBon = 0, bad = 0;
        for (auto &e : fs::recursive_directory_iterator(root))
            if (e.is_regular_file())
            {
                auto ext = lower(e.path().extension().string());
                if (ext != ".bsc" && ext != ".bon")
                    continue;
                try
                {
                    if (ext == ".bsc")
                    {
                        loadModel(e.path());
                        ++goodMesh;
                    }
                    else
                    {
                        loadAnimation(e.path());
                        ++goodBon;
                    }
                }
                catch (const std::exception &error)
                {
                    ++bad;
                    log << "UNSUPPORTED " << e.path().filename().u8string() << ": " << error.what() << "\n";
                }
            }
        // Truncated input must fail safely instead of returning partial geometry.
        auto malformed = out / L"truncated-test.bsc";
        {
            std::ofstream f(malformed, std::ios::binary);
            f << "bad";
        }
        bool rejected = false;
        try
        {
            loadModel(malformed);
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        fs::remove(malformed);
        if (!rejected)
            throw std::runtime_error("Truncated BSC accepted");
        log << "PASS: DX11 WARP captures, sample textures, assembly, animation matrices, missing-animation rendering, "
               "truncated BSC rejection\n";
        log << "SCAN: BSC=" << goodMesh << " BON=" << goodBon << " unsupported=" << bad << "\n";
        DestroyWindow(window);
        return 0;
    }
    catch (const std::exception &e)
    {
        log << "FAIL: " << e.what() << "\n";
        if (window)
            DestroyWindow(window);
        return 1;
    }
}
