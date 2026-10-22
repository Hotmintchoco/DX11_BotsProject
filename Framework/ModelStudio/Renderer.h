#pragma once
#include "Assets.h"
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <memory>
namespace studio
{
using Microsoft::WRL::ComPtr;
struct Texture
{
    fs::path path;
    ComPtr<ID3D11ShaderResourceView> view;
};
struct GpuMesh
{
    ComPtr<ID3D11Buffer> vertices, indices;
    UINT count{};
};
struct Slot
{
    std::shared_ptr<Model> model;
    std::vector<GpuMesh> gpu;
    std::array<Texture, 4> textures;
    bool visible = true;
};
class Renderer
{
  public:
    void initialize(HWND window, bool warp = false);
    void resize(UINT w, UINT h);
    Slot loadSlot(const fs::path &path, std::wstring &warnings);
    Texture loadTexture(const fs::path &path);
    void fit(const std::array<Slot, 4> &slots, const Animation *anim, uint32_t frame);
    void render(const std::array<Slot, 4> &slots, const Animation *anim, uint32_t frame, bool present = true);
    void capture(const fs::path &path);
    void orbit(float dx, float dy);
    void pan(float dx, float dy);
    void zoom(float delta);
    bool wire = false, clay = false, grid = true, bones = false;

  private:
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swap;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11DepthStencilView> depth;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> constants, gridBuffer;
    UINT gridCount{};
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RasterizerState> solidRaster, wireRaster;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11DepthStencilState> opaqueDepth, alphaDepth;
    UINT width = 1, height = 1;
    float yaw = .4f, pitch = .18f, distance = 180, radius = 100;
    XMFLOAT3 center{0, 40, 0};
    void drawLines(const std::vector<Vertex> &vertices, FXMMATRIX vp);
};
} // namespace studio
