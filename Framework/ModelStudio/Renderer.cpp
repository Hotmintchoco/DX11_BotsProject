#include "Renderer.h"
#include <d3dcompiler.h>
#include <wincodec.h>
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <cmath>
namespace studio
{
static void check(HRESULT hr)
{
    if (FAILED(hr))
        throw std::runtime_error("Direct3D/WIC failure: " + std::to_string(static_cast<unsigned long>(hr)));
}
struct alignas(16) Constants
{
    XMFLOAT4X4 world, vp, normal;
    XMFLOAT4 color, options;
};
static const char *shader = R"(
cbuffer Scene : register(b0) { row_major float4x4 World; row_major float4x4 VP; row_major float4x4 Normal; float4 Color; float4 Options; };
struct Input { float3 p:POSITION;float3 n:NORMAL;float2 uv:TEXCOORD; };
struct Output { float4 p:SV_POSITION;float3 n:NORMAL;float2 uv:TEXCOORD; };
Output VS(Input a){Output o;o.p=mul(mul(float4(a.p,1),World),VP);o.n=mul(float4(a.n,0),Normal).xyz;o.uv=a.uv;return o;}
Texture2D Tex:register(t0);SamplerState Sam:register(s0);
float4 PS(Output i):SV_TARGET {float4 c=Color;if(Options.x>0.5)c*=Tex.Sample(Sam,i.uv);if(Options.y>0.5)clip(c.a-0.03);
 float diffuse=Options.z>0.5?1:0.45+0.55*abs(dot(normalize(i.n+0.000001),normalize(float3(-.4,.8,-.6))));return float4(c.rgb*diffuse,Options.y>0.5?c.a:1);}
)";
void Renderer::initialize(HWND wnd, bool warp)
{
    DXGI_SWAP_CHAIN_DESC d{};
    d.BufferCount = 1;
    d.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.OutputWindow = wnd;
    d.SampleDesc.Count = 1;
    d.Windowed = TRUE;
    d.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                               0, levels, 1, D3D11_SDK_VERSION, &d, &swap, &device, &got, &context);
    if (FAILED(hr) && !warp)
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 1, D3D11_SDK_VERSION, &d,
                                           &swap, &device, &got, &context);
    check(hr);
    ComPtr<ID3DBlob> v, p, error;
    check(D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr, "VS", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
                     &v, &error));
    check(D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr, "PS", "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
                     &p, &error));
    check(device->CreateVertexShader(v->GetBufferPointer(), v->GetBufferSize(), nullptr, &vs));
    check(device->CreatePixelShader(p->GetBufferPointer(), p->GetBufferSize(), nullptr, &ps));
    D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    check(device->CreateInputLayout(elements, 3, v->GetBufferPointer(), v->GetBufferSize(), &layout));
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(Constants);
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    check(device->CreateBuffer(&cb, nullptr, &constants));
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    check(device->CreateSamplerState(&sd, &sampler));
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    check(device->CreateRasterizerState(&rd, &solidRaster));
    rd.FillMode = D3D11_FILL_WIREFRAME;
    check(device->CreateRasterizerState(&rd, &wireRaster));
    D3D11_BLEND_DESC bd{};
    auto &b = bd.RenderTarget[0];
    b.BlendEnable = TRUE;
    b.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    b.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    b.BlendOp = D3D11_BLEND_OP_ADD;
    b.SrcBlendAlpha = D3D11_BLEND_ONE;
    b.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    b.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    b.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    check(device->CreateBlendState(&bd, &blend));
    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = TRUE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    check(device->CreateDepthStencilState(&dd, &opaqueDepth));
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    check(device->CreateDepthStencilState(&dd, &alphaDepth));
    RECT rc{};
    GetClientRect(wnd, &rc);
    resize(std::max(1L, rc.right), std::max(1L, rc.bottom));
}
void Renderer::resize(UINT w, UINT h)
{
    if (!swap || !w || !h)
        return;
    width = w;
    height = h;
    context->OMSetRenderTargets(0, nullptr, nullptr);
    target.Reset();
    depth.Reset();
    check(swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0));
    ComPtr<ID3D11Texture2D> back;
    check(swap->GetBuffer(0, IID_PPV_ARGS(&back)));
    D3D11_RENDER_TARGET_VIEW_DESC rv{};
    rv.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    check(device->CreateRenderTargetView(back.Get(), &rv, &target));
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w;
    td.Height = h;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> dep;
    check(device->CreateTexture2D(&td, nullptr, &dep));
    check(device->CreateDepthStencilView(dep.Get(), nullptr, &depth));
}
Texture Renderer::loadTexture(const fs::path &path)
{
    Texture out;
    out.path = path;
    auto bytes = readBytes(path);
    UINT w = 0, h = 0;
    std::vector<uint8_t> pixels;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    UINT pitchBytes = 0;
    const void *data = nullptr;
    auto u16 = [&](size_t p) -> UINT {
        if (p + 2 > bytes.size())
            throw std::runtime_error("Truncated image");
        return bytes[p] | (UINT(bytes[p + 1]) << 8);
    };
    auto u32 = [&](size_t p) -> UINT {
        if (p + 4 > bytes.size())
            throw std::runtime_error("Truncated DDS");
        UINT x;
        memcpy(&x, bytes.data() + p, 4);
        return x;
    };
    if (bytes.size() >= 128 && memcmp(bytes.data(), "DDS ", 4) == 0)
    {
        if (u32(4) != 124 || u32(76) != 32 || u32(112) != 0)
            throw std::runtime_error("Unsupported DDS header/cubemap/volume");
        w = u32(16);
        h = u32(12);
        auto cc = u32(84);
        UINT block = 16;
        if (cc == 0x31545844)
        {
            format = DXGI_FORMAT_BC1_UNORM_SRGB;
            block = 8;
        }
        else if (cc == 0x33545844)
            format = DXGI_FORMAT_BC2_UNORM_SRGB;
        else if (cc == 0x35545844)
            format = DXGI_FORMAT_BC3_UNORM_SRGB;
        else
            throw std::runtime_error("DDS requires DXT1/DXT3/DXT5");
        if (!w || !h || w > 16384 || h > 16384)
            throw std::runtime_error("Invalid DDS dimensions");
        pitchBytes = ((w + 3) / 4) * block;
        if (128 + size_t(pitchBytes) * ((h + 3) / 4) > bytes.size())
            throw std::runtime_error("Truncated DDS pixels");
        data = bytes.data() + 128;
    }
    else if (bytes.size() >= 18 && bytes[1] == 0 &&
             (bytes[2] == 2 || bytes[2] == 3 || bytes[2] == 10 || bytes[2] == 11) &&
             (bytes[16] == 8 || bytes[16] == 24 || bytes[16] == 32))
    {
        w = u16(12);
        h = u16(14);
        if (!w || !h || w > 8192 || h > 8192)
            throw std::runtime_error("Invalid TGA dimensions");
        size_t o = 18 + bytes[0], n = size_t(w) * h, done = 0;
        UINT channels = bytes[16] / 8;
        pixels.resize(n * 4);
        auto pixel = [&]() {
            if (o + channels > bytes.size())
                throw std::runtime_error("Truncated TGA pixels");
            std::array<uint8_t, 4> c = {bytes[o], bytes[o], bytes[o], 255};
            if (channels >= 3)
            {
                c[0] = bytes[o + 2];
                c[1] = bytes[o + 1];
                c[2] = bytes[o];
                if (channels == 4)
                    c[3] = bytes[o + 3];
            }
            o += channels;
            return c;
        };
        auto put = [&](const std::array<uint8_t, 4> &c) {
            if (done >= n)
                throw std::runtime_error("TGA RLE overflow");
            size_t x = done % w, y = done / w;
            if (!(bytes[17] & 32))
                y = h - 1 - y;
            if (bytes[17] & 16)
                x = w - 1 - x;
            memcpy(pixels.data() + (y * w + x) * 4, c.data(), 4);
            ++done;
        };
        while (done < n)
        {
            if (bytes[2] < 8)
                put(pixel());
            else
            {
                if (o >= bytes.size())
                    throw std::runtime_error("Truncated TGA RLE");
                auto code = bytes[o++];
                UINT count = (code & 127) + 1;
                if (code & 128)
                {
                    auto c = pixel();
                    while (count--)
                        put(c);
                }
                else
                    while (count--)
                        put(pixel());
            }
        }
        pitchBytes = w * 4;
        data = pixels.data();
    }
    else
    {
        ComPtr<IWICImagingFactory> factory;
        check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
        ComPtr<IWICStream> stream;
        check(factory->CreateStream(&stream));
        check(stream->InitializeFromMemory(bytes.data(), static_cast<DWORD>(bytes.size())));
        ComPtr<IWICBitmapDecoder> dec;
        check(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &dec));
        ComPtr<IWICBitmapFrameDecode> frame;
        check(dec->GetFrame(0, &frame));
        check(frame->GetSize(&w, &h));
        if (!w || !h || w > 8192 || h > 8192)
            throw std::runtime_error("Invalid image dimensions");
        ComPtr<IWICFormatConverter> conv;
        check(factory->CreateFormatConverter(&conv));
        check(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                               WICBitmapPaletteTypeCustom));
        pixels.resize(size_t(w) * h * 4);
        pitchBytes = w * 4;
        check(conv->CopyPixels(nullptr, pitchBytes, static_cast<UINT>(pixels.size()), pixels.data()));
        data = pixels.data();
    }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w;
    td.Height = h;
    td.MipLevels = td.ArraySize = 1;
    td.Format = format;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = data;
    init.SysMemPitch = pitchBytes;
    ComPtr<ID3D11Texture2D> tex;
    check(device->CreateTexture2D(&td, &init, &tex));
    check(device->CreateShaderResourceView(tex.Get(), nullptr, &out.view));
    return out;
}
Slot Renderer::loadSlot(const fs::path &path, std::wstring &warnings)
{
    Slot slot;
    slot.model = std::make_shared<Model>(loadModel(path));
    for (auto &mesh : slot.model->meshes)
    {
        GpuMesh g;
        g.count = static_cast<UINT>(mesh.indices.size());
        if (!mesh.vertices.empty() && !mesh.indices.empty())
        {
            D3D11_BUFFER_DESC b{};
            b.Usage = D3D11_USAGE_IMMUTABLE;
            b.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            b.ByteWidth = static_cast<UINT>(mesh.vertices.size() * sizeof(Vertex));
            D3D11_SUBRESOURCE_DATA d{};
            d.pSysMem = mesh.vertices.data();
            check(device->CreateBuffer(&b, &d, &g.vertices));
            b.BindFlags = D3D11_BIND_INDEX_BUFFER;
            b.ByteWidth = static_cast<UINT>(mesh.indices.size() * 2);
            d.pSysMem = mesh.indices.data();
            check(device->CreateBuffer(&b, &d, &g.indices));
        }
        slot.gpu.push_back(std::move(g));
    }
    for (int i = 0; i < 4; ++i)
    {
        auto p = companionTexture(path, i);
        if (!p.empty())
            try
            {
                slot.textures[i] = loadTexture(p);
            }
            catch (const std::exception &)
            {
                warnings += L"\r\n텍스처 읽기 실패: " + p.filename().wstring();
            }
    }
    return slot;
}
void Renderer::fit(const std::array<Slot, 4> &slots, const Animation *anim, uint32_t frame)
{
    XMVECTOR lo = XMVectorReplicate(FLT_MAX), hi = XMVectorReplicate(-FLT_MAX);
    bool any = false;
    for (auto &s : slots)
        if (s.model && s.visible)
            for (auto &m : s.model->meshes)
            {
                auto world = anim ? anim->matrix(m.bone, frame) : XMMatrixIdentity();
                for (auto &v : m.vertices)
                {
                    auto p = XMVector3TransformCoord(XMLoadFloat3(&v.position), world);
                    lo = XMVectorMin(lo, p);
                    hi = XMVectorMax(hi, p);
                    any = true;
                }
            }
    if (!any)
        return;
    XMStoreFloat3(&center, (lo + hi) * .5f);
    XMFLOAT3 size;
    XMStoreFloat3(&size, hi - lo);
    radius = std::max({size.x, size.y, size.z, 10.f});
    distance = radius * 1.8f;
}
void Renderer::orbit(float dx, float dy)
{
    yaw += dx * .008f;
    pitch = std::clamp(pitch + dy * .008f, -1.5f, 1.5f);
}
void Renderer::pan(float dx, float dy)
{
    float s = distance * .0015f;
    center.x -= cosf(yaw) * dx * s;
    center.z -= sinf(yaw) * dx * s;
    center.y += dy * s;
}
void Renderer::zoom(float delta)
{
    distance = std::clamp(distance * expf(-delta * .001f), radius * .05f, radius * 30);
}
void Renderer::drawLines(const std::vector<Vertex> &vertices, FXMMATRIX vp)
{
    if (vertices.empty())
        return;
    D3D11_BUFFER_DESC d{};
    d.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(Vertex));
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = vertices.data();
    ComPtr<ID3D11Buffer> b;
    check(device->CreateBuffer(&d, &init, &b));
    Constants c{};
    XMStoreFloat4x4(&c.world, XMMatrixIdentity());
    XMStoreFloat4x4(&c.vp, vp);
    XMStoreFloat4x4(&c.normal, XMMatrixIdentity());
    c.color = {.18f, .36f, .4f, 1};
    c.options = {0, 0, 1, 0};
    context->UpdateSubresource(constants.Get(), 0, nullptr, &c, 0, 0);
    UINT stride = sizeof(Vertex), offset = 0;
    context->IASetVertexBuffers(0, 1, b.GetAddressOf(), &stride, &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    context->Draw(static_cast<UINT>(vertices.size()), 0);
}
void Renderer::render(const std::array<Slot, 4> &slots, const Animation *anim, uint32_t frame, bool present)
{
    if (!target)
        return;
    float bg[] = {.025f, .035f, .052f, 1};
    context->ClearRenderTargetView(target.Get(), bg);
    context->ClearDepthStencilView(depth.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1, 0);
    context->OMSetRenderTargets(1, target.GetAddressOf(), depth.Get());
    D3D11_VIEWPORT viewport{0, 0, float(width), float(height), 0, 1};
    context->RSSetViewports(1, &viewport);
    context->RSSetState(wire ? wireRaster.Get() : solidRaster.Get());
    context->IASetInputLayout(layout.Get());
    context->VSSetShader(vs.Get(), nullptr, 0);
    context->PSSetShader(ps.Get(), nullptr, 0);
    context->VSSetConstantBuffers(0, 1, constants.GetAddressOf());
    context->PSSetConstantBuffers(0, 1, constants.GetAddressOf());
    context->PSSetSamplers(0, 1, sampler.GetAddressOf());
    auto eye = XMLoadFloat3(&center) + XMVectorSet(sinf(yaw) * cosf(pitch) * distance, sinf(pitch) * distance,
                                                   -cosf(yaw) * cosf(pitch) * distance, 0);
    auto vp = XMMatrixLookAtLH(eye, XMLoadFloat3(&center), XMVectorSet(0, 1, 0, 0)) *
              XMMatrixPerspectiveFovLH(XM_PIDIV4, float(width) / height, std::max(.01f, radius * .0001f), radius * 100);
    context->OMSetDepthStencilState(opaqueDepth.Get(), 0);
    context->OMSetBlendState(nullptr, nullptr, ~0u);
    std::vector<Vertex> lines;
    if (grid)
    {
        float step = radius / 10;
        for (int i = -10; i <= 10; ++i)
        {
            lines.push_back({{i * step, 0, -10 * step}, {0, 1, 0}, {}});
            lines.push_back({{i * step, 0, 10 * step}, {0, 1, 0}, {}});
            lines.push_back({{-10 * step, 0, i * step}, {0, 1, 0}, {}});
            lines.push_back({{10 * step, 0, i * step}, {0, 1, 0}, {}});
        }
        drawLines(lines, vp);
    }
    struct Draw
    {
        const Slot *slot;
        size_t index;
        bool alpha;
        float distance;
    };
    std::vector<Draw> draws;
    for (auto &s : slots)
        if (s.model && s.visible)
            for (size_t i = 0; i < s.gpu.size(); ++i)
            {
                auto &m = s.model->meshes[i];
                auto w = anim ? anim->matrix(m.bone, frame) : XMMatrixIdentity();
                auto delta = w.r[3] - eye;
                bool alpha = m.material == 1 && s.textures[1].view && !clay;
                draws.push_back({&s, i, alpha, XMVectorGetX(XMVector3LengthSq(delta))});
            }
    std::stable_sort(draws.begin(), draws.end(), [](const Draw &a, const Draw &b) {
        if (a.alpha != b.alpha)
            return !a.alpha;
        return a.alpha ? a.distance > b.distance : false;
    });
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for (auto &d : draws)
    {
        auto &s = *d.slot;
        auto &m = s.model->meshes[d.index];
        auto &gpu = s.gpu[d.index];
        if (!gpu.count)
            continue;
        auto world = anim ? anim->matrix(m.bone, frame) : XMMatrixIdentity();
        Constants c{};
        XMStoreFloat4x4(&c.world, world);
        XMStoreFloat4x4(&c.vp, vp);
        auto det = XMMatrixDeterminant(world);
        XMStoreFloat4x4(&c.normal, fabsf(XMVectorGetX(det)) > 1e-8f ? XMMatrixTranspose(XMMatrixInverse(nullptr, world))
                                                                    : XMMatrixIdentity());
        ID3D11ShaderResourceView *texture = (!clay && m.material < 4) ? s.textures[m.material].view.Get() : nullptr;
        c.color = texture ? XMFLOAT4(1, 1, 1, 1) : XMFLOAT4(.5f, .65f, .8f, 1);
        c.options = {texture ? 1.f : 0.f, d.alpha ? 1.f : 0.f, 0, 0};
        context->UpdateSubresource(constants.Get(), 0, nullptr, &c, 0, 0);
        context->PSSetShaderResources(0, 1, &texture);
        context->OMSetBlendState(d.alpha ? blend.Get() : nullptr, nullptr, ~0u);
        context->OMSetDepthStencilState(d.alpha ? alphaDepth.Get() : opaqueDepth.Get(), 0);
        UINT stride = sizeof(Vertex), offset = 0;
        context->IASetVertexBuffers(0, 1, gpu.vertices.GetAddressOf(), &stride, &offset);
        context->IASetIndexBuffer(gpu.indices.Get(), DXGI_FORMAT_R16_UINT, 0);
        context->DrawIndexed(gpu.count, 0, 0);
    }
    if (bones && anim)
    {
        context->OMSetBlendState(nullptr, nullptr, ~0u);
        context->OMSetDepthStencilState(opaqueDepth.Get(), 0);
        lines.clear();
        float r = radius * .008f;
        for (auto &b : anim->bones)
        {
            auto m = b.second[std::min(frame, anim->frames - 1)];
            XMFLOAT3 p{m._41, m._42, m._43};
            for (int i = 0; i < 3; ++i)
            {
                auto a = p, z = p;
                (&a.x)[i] -= r;
                (&z.x)[i] += r;
                lines.push_back({a, {0, 1, 0}, {}});
                lines.push_back({z, {0, 1, 0}, {}});
            }
        }
        drawLines(lines, vp);
    }
    if (present)
        check(swap->Present(1, 0));
}
void Renderer::capture(const fs::path &path)
{
    ComPtr<ID3D11Texture2D> back, staging;
    check(swap->GetBuffer(0, IID_PPV_ARGS(&back)));
    D3D11_TEXTURE2D_DESC td{};
    back->GetDesc(&td);
    td.BindFlags = 0;
    td.Usage = D3D11_USAGE_STAGING;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    check(device->CreateTexture2D(&td, nullptr, &staging));
    context->CopyResource(staging.Get(), back.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
    std::vector<uint8_t> pixels(size_t(width) * height * 4);
    for (UINT y = 0; y < height; ++y)
        memcpy(pixels.data() + size_t(y) * width * 4,
               static_cast<uint8_t *>(mapped.pData) + size_t(y) * mapped.RowPitch, size_t(width) * 4);
    context->Unmap(staging.Get(), 0);
    ComPtr<IWICImagingFactory> f;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)));
    ComPtr<IWICStream> s;
    check(f->CreateStream(&s));
    check(s->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> e;
    check(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &e));
    check(e->Initialize(s.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;
    check(e->CreateNewFrame(&frame, nullptr));
    check(frame->Initialize(nullptr));
    check(frame->SetSize(width, height));
    for (size_t i = 0; i < pixels.size(); i += 4)
        std::swap(pixels[i], pixels[i + 2]);
    auto format = GUID_WICPixelFormat32bppBGRA;
    check(frame->SetPixelFormat(&format));
    if (format != GUID_WICPixelFormat32bppBGRA)
        throw std::runtime_error("PNG pixel format mismatch");
    check(frame->WritePixels(height, width * 4, static_cast<UINT>(pixels.size()), pixels.data()));
    check(frame->Commit());
    check(e->Commit());
}
} // namespace studio
