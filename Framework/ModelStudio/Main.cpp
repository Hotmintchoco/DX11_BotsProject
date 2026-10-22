#include "Renderer.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>
#include <chrono>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <optional>
using namespace studio;
static std::wstring wide(const std::string &s)
{
    return std::wstring(s.begin(), s.end());
}
static fs::path defaultRoot()
{
    PWSTR p = nullptr;
    fs::path root;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &p)))
    {
        root = fs::path(p) / L"바우트 리소스";
        CoTaskMemFree(p);
    }
    return root;
}
static std::optional<fs::path> choose(HWND owner, bool folder, const wchar_t *filter)
{
    if (folder)
    {
        ComPtr<IFileDialog> dialog;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
            return {};
        DWORD flags;
        dialog->GetOptions(&flags);
        dialog->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        if (FAILED(dialog->Show(owner)))
            return {};
        ComPtr<IShellItem> item;
        dialog->GetResult(&item);
        PWSTR p = nullptr;
        if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)))
            return {};
        fs::path path(p);
        CoTaskMemFree(p);
        return path;
    }
    wchar_t path[32768]{};
    OPENFILENAMEW of{};
    of.lStructSize = sizeof(of);
    of.hwndOwner = owner;
    of.lpstrFilter = filter;
    of.lpstrFile = path;
    of.nMaxFile = 32768;
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&of) ? std::optional<fs::path>(path) : std::nullopt;
}
static const wchar_t *modelFilter = L"BOUT Mesh (*.bsc)\0*.bsc\0\0";
static const wchar_t *bonFilter = L"BOUT Animation (*.bon)\0*.bon\0\0";
static const wchar_t *textureFilter = L"Textures\0*.ba0;*.ba2;*.bb0;*.bb2;*.bc0;*.bc2;*.bd0;*.bd2;*.png;*.jpg;*.jpeg;*."
                                      L"bmp;*.tga;*.dds\0All files\0*.*\0\0";
enum Id
{
    ROOT = 100,
    LIBRARY,
    LOAD,
    SLOT,
    MESHOPEN,
    MESHCLEAR,
    VISIBLE,
    BONOPEN,
    BONCLEAR,
    CLIP,
    PLAY,
    FIT,
    FPS,
    TIMELINE,
    WIRE,
    CLAY,
    GRID,
    BONES,
    TEXBASE = 200
};
struct Entry
{
    std::wstring label;
    fs::path mesh;
    std::string player;
};
class App
{
  public:
    HWND window{}, view{};
    Renderer renderer;
    std::array<Slot, 4> slots;
    std::shared_ptr<Animation> animation;
    fs::path root;
    std::vector<Entry> entries;
    std::unordered_map<int, HWND> controls;
    HFONT font{};
    HBRUSH background{};
    HWND rootLabel{}, meshLabel{}, bonLabel{}, statusLabel{}, frameLabel{};
    std::array<HWND, 4> textureLabels{};
    std::vector<std::pair<HWND, RECT>> widgets;
    int scrollY = 0;
    bool ready = false, playing = false, drag = false, rightDrag = false;
    POINT previous{};
    uint32_t frame = 0;
    double accumulator = 0;
    std::chrono::steady_clock::time_point last;
    int selected() const
    {
        return static_cast<int>(SendMessageW(controls.at(SLOT), CB_GETCURSEL, 0, 0));
    }
    void status(const std::wstring &text)
    {
        SetWindowTextW(statusLabel, text.c_str());
    }
    HWND control(const wchar_t *cls, const std::wstring &text, int id, int x, int y, int w, int h, DWORD style = 0)
    {
        HWND c = CreateWindowExW(0, cls, text.c_str(), WS_CHILD | WS_VISIBLE | style, x, y, w, h, window,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        if (id)
            controls[id] = c;
        widgets.push_back({c, {x, y, w, h}});
        return c;
    }
    HWND label(const std::wstring &t, int x, int y, int w = 332, int h = 22)
    {
        return control(L"STATIC", t, 0, x, y, w, h, SS_LEFT);
    }
    void comboItem(int id, const std::wstring &text)
    {
        SendMessageW(controls[id], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }
    void create(HWND h)
    {
        window = h;
        root = defaultRoot();
        background = CreateSolidBrush(RGB(24, 32, 45));
        font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"맑은 고딕");
        label(L"MODEL STUDIO  /  DIRECTX 11", 18, 18);
        rootLabel = label(root.wstring(), 18, 49, 246, 40);
        control(L"BUTTON", L"폴더…", ROOT, 270, 49, 76, 30);
        control(WC_COMBOBOXW, L"", LIBRARY, 18, 98, 244, 360, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP);
        control(L"BUTTON", L"세트 열기", LOAD, 270, 97, 76, 30);
        label(L"편집할 부위 / 몬스터", 18, 143);
        control(WC_COMBOBOXW, L"", SLOT, 18, 170, 244, 160, CBS_DROPDOWNLIST | WS_TABSTOP);
        for (auto n : {L"Monster", L"Player / Body", L"Player / Arm", L"Player / Head"})
            comboItem(SLOT, n);
        SendMessageW(controls[SLOT], CB_SETCURSEL, 0, 0);
        control(L"BUTTON", L"표시", VISIBLE, 270, 170, 76, 26, BS_AUTOCHECKBOX);
        meshLabel = label(L"메시 없음", 18, 207, 328, 36);
        control(L"BUTTON", L"BSC 교체…", MESHOPEN, 18, 248, 162, 29);
        control(L"BUTTON", L"메시 해제", MESHCLEAR, 188, 248, 158, 29);
        label(L"선택한 부위의 텍스처", 18, 294);
        for (int i = 0; i < 4; ++i)
        {
            int y = 323 + i * 48;
            textureLabels[i] = label(std::wstring(1, wchar_t(L'A' + i)) + L" · 해제됨", 18, y, 202, 40);
            control(L"BUTTON", L"교체", TEXBASE + i * 2, 225, y, 58, 29);
            control(L"BUTTON", L"해제", TEXBASE + i * 2 + 1, 288, y, 58, 29);
        }
        label(L"공유 애니메이션 / BON", 18, 522);
        bonLabel = label(L"BON 없음", 18, 548, 328, 26);
        control(L"BUTTON", L"BON 교체…", BONOPEN, 18, 580, 162, 29);
        control(L"BUTTON", L"BON 해제", BONCLEAR, 188, 580, 158, 29);
        control(WC_COMBOBOXW, L"", CLIP, 18, 628, 244, 280, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP);
        control(WC_COMBOBOXW, L"", FPS, 270, 628, 76, 150, CBS_DROPDOWNLIST | WS_TABSTOP);
        for (auto t : {L"15 FPS", L"30 FPS", L"60 FPS"})
            comboItem(FPS, t);
        SendMessageW(controls[FPS], CB_SETCURSEL, 1, 0);
        control(L"BUTTON", L"▶ 재생", PLAY, 18, 667, 100, 30);
        control(L"BUTTON", L"화면 맞춤", FIT, 126, 667, 114, 30);
        frameLabel = label(L"0 / 0", 248, 672, 98, 24);
        control(TRACKBAR_CLASSW, L"", TIMELINE, 12, 705, 340, 30, TBS_HORZ | TBS_NOTICKS | WS_TABSTOP);
        control(L"BUTTON", L"와이어", WIRE, 18, 748, 80, 26, BS_AUTOCHECKBOX);
        control(L"BUTTON", L"클레이", CLAY, 100, 748, 80, 26, BS_AUTOCHECKBOX);
        control(L"BUTTON", L"그리드", GRID, 182, 748, 80, 26, BS_AUTOCHECKBOX);
        control(L"BUTTON", L"본 위치", BONES, 264, 748, 90, 26, BS_AUTOCHECKBOX);
        SendMessageW(controls[GRID], BM_SETCHECK, BST_CHECKED, 0);
        statusLabel = label(L"준비 중", 18, 790, 328, 130);
        view = CreateWindowExW(0, L"StudioViewport", L"", WS_CHILD | WS_VISIBLE, 365, 0, 900, 900, h, nullptr,
                               GetModuleHandleW(nullptr), this);
        DragAcceptFiles(view, TRUE);
        renderer.initialize(view);
        ready = true;
        layout();
        scan();
        refresh();
        last = std::chrono::steady_clock::now();
        SetTimer(h, 1, 16, nullptr);
        if (!entries.empty())
        {
            auto it = std::find_if(entries.begin(), entries.end(), [](const Entry &e) { return e.player == "az"; });
            int i = it == entries.end() ? 0 : static_cast<int>(it - entries.begin());
            SendMessageW(controls[LIBRARY], CB_SETCURSEL, i, 0);
            try
            {
                loadEntry(entries[i]);
            }
            catch (const std::exception &e)
            {
                status(L"초기 세트 읽기 실패: " + wide(e.what()));
            }
        }
    }
    void layout()
    {
        if (!view)
            return;
        RECT r{};
        GetClientRect(window, &r);
        scrollY = std::clamp(scrollY, 0, std::max(0, 940 - int(r.bottom)));
        SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS, 0, 939, static_cast<UINT>(r.bottom), scrollY, 0};
        SetScrollInfo(window, SB_VERT, &si, TRUE);
        for (auto &item : widgets)
        {
            auto p = item.second;
            MoveWindow(item.first, p.left, p.top - scrollY, p.right, p.bottom, TRUE);
        }
        MoveWindow(view, 365, 0, std::max(1L, r.right - 365), std::max(1L, r.bottom), TRUE);
        if (ready)
            renderer.resize(std::max(1L, r.right - 365), std::max(1L, r.bottom));
    }
    void scan()
    {
        entries.clear();
        SendMessageW(controls[LIBRARY], CB_RESETCONTENT, 0, 0);
        SetWindowTextW(rootLabel, root.c_str());
        for (auto c : {"az", "gr", "hs"})
        {
            std::wstring folder = wide(c);
            folder[0] = towupper(folder[0]);
            auto p = root / L"플레이어" / folder / (std::string(c) + "_000_body.bsc");
            if (fs::exists(p))
                entries.push_back({L"Player / " + folder, p, c});
        }
        std::vector<fs::path> models;
        std::error_code ec;
        auto monster = root / L"monster";
        if (fs::exists(monster))
            for (auto &e : fs::directory_iterator(monster, ec))
                if (e.is_regular_file() && lower(e.path().extension().string()) == ".bsc")
                    models.push_back(e.path());
        std::sort(models.begin(), models.end());
        for (auto &p : models)
            entries.push_back({L"Monster / " + p.stem().wstring(), p, {}});
        for (auto &e : entries)
            comboItem(LIBRARY, e.label);
        if (!entries.empty())
            SendMessageW(controls[LIBRARY], CB_SETCURSEL, 0, 0);
        else
            status(L"리소스 폴더를 선택하세요.\r\n플레이어 및 monster 하위 폴더를 찾습니다.");
    }
    void validate(const std::array<Slot, 4> &next, const std::shared_ptr<Animation> &a)
    {
        if (!a)
            return;
        for (auto &s : next)
            if (s.model)
            {
                auto source = characterClass(s.model->path), target = characterClass(a->path);
                if (!source.empty() && !target.empty() && source != target)
                    throw std::runtime_error("Player class mismatch. Choose matching Az / Gr / Hs BON.");
                auto missing = missingBones(*s.model, *a);
                if (!missing.empty())
                    throw std::runtime_error("BON is missing mesh bone: " + missing.front());
            }
    }
    void stop()
    {
        playing = false;
        accumulator = 0;
        SetWindowTextW(controls[PLAY], L"▶ 재생");
    }
    void refresh(bool resetClip = true)
    {
        int idx = selected();
        if (idx < 0)
            idx = 0;
        auto &s = slots[idx];
        SetWindowTextW(meshLabel, s.model ? s.model->path.filename().c_str() : L"메시 없음");
        SendMessageW(controls[VISIBLE], BM_SETCHECK, s.visible ? BST_CHECKED : BST_UNCHECKED, 0);
        for (int i = 0; i < 4; ++i)
        {
            auto text = std::wstring(1, wchar_t(L'A' + i)) + L" · " +
                        (s.textures[i].view ? s.textures[i].path.filename().wstring() : L"해제됨");
            SetWindowTextW(textureLabels[i], text.c_str());
        }
        SetWindowTextW(bonLabel, animation ? animation->path.filename().c_str() : L"BON 없음 · 파츠의 로컬 좌표 표시");
        if (resetClip)
        {
            SendMessageW(controls[CLIP], CB_RESETCONTENT, 0, 0);
            comboItem(CLIP, L"전체 모션");
            if (animation)
                for (size_t i = 0; i < animation->clips.size(); ++i)
                {
                    auto c = animation->clips[i];
                    comboItem(CLIP, L"클립 " + std::to_wstring(i + 1) + L" · " + std::to_wstring(c.count) + L" 프레임");
                }
            SendMessageW(controls[CLIP], CB_SETCURSEL, 0, 0);
        }
        auto max = animation ? animation->frames - 1 : 0;
        frame = std::min(frame, max);
        SendMessageW(controls[TIMELINE], TBM_SETRANGEMAX, TRUE, max);
        EnableWindow(controls[PLAY], animation != nullptr);
        updateFrame();
    }
    void updateFrame()
    {
        SendMessageW(controls[TIMELINE], TBM_SETPOS, TRUE, frame);
        auto text = std::to_wstring(frame) + L" / " + std::to_wstring(animation ? animation->frames - 1 : 0);
        SetWindowTextW(frameLabel, text.c_str());
    }
    void loadEntry(const Entry &e)
    {
        std::array<Slot, 4> next;
        std::wstring warnings;
        std::shared_ptr<Animation> a;
        auto bon = companionAnimation(e.mesh);
        if (!bon.empty())
            a = std::make_shared<Animation>(loadAnimation(bon));
        if (e.player.empty())
            next[0] = renderer.loadSlot(e.mesh, warnings);
        else
        {
            int i = 1;
            for (auto part : {"body", "arm", "head"})
                next[i++] = renderer.loadSlot(e.mesh.parent_path() / (e.player + "_000_" + part + ".bsc"), warnings);
        }
        validate(next, a);
        slots = std::move(next);
        animation = std::move(a);
        stop();
        frame = 0;
        SendMessageW(controls[SLOT], CB_SETCURSEL, e.player.empty() ? 0 : 1, 0);
        refresh();
        renderer.fit(slots, animation.get(), frame);
        status(e.label + L"\r\n불러오기 완료" + (animation ? L"" : L" · BON 없음 (로컬 파츠)") + warnings +
               L"\r\n좌드래그 회전 · 우드래그 이동\r\n휠 확대 · F 화면 맞춤");
    }
    void openMesh(const fs::path &p)
    {
        int i = selected();
        auto next = slots;
        std::wstring warnings;
        next[i] = renderer.loadSlot(p, warnings);
        auto a = animation;
        if (i == 0)
        {
            next[1] = {};
            next[2] = {};
            next[3] = {};
            auto bon = companionAnimation(p);
            a = bon.empty() ? nullptr : std::make_shared<Animation>(loadAnimation(bon));
        }
        else
        {
            next[0] = {};
            if (!a)
            {
                auto bon = companionAnimation(p);
                if (!bon.empty())
                    a = std::make_shared<Animation>(loadAnimation(bon));
            }
        }
        validate(next, a);
        slots = std::move(next);
        animation = std::move(a);
        stop();
        frame = 0;
        refresh();
        renderer.fit(slots, animation.get(), frame);
        status(L"메시 교체 완료. 같은 이름의 텍스처를 자동 연결했습니다." + warnings +
               (animation ? L"" : L"\r\nBON 없음: 로컬 파츠 좌표입니다."));
    }
    void openBon(const fs::path &p)
    {
        auto a = std::make_shared<Animation>(loadAnimation(p));
        validate(slots, a);
        animation = std::move(a);
        stop();
        frame = 0;
        refresh();
        renderer.fit(slots, animation.get(), frame);
        status(L"BON 교체 완료");
    }
    void command(int id, int notification)
    {
        try
        {
            if (id == SLOT && notification == CBN_SELCHANGE)
            {
                refresh(false);
                return;
            }
            if (id == LIBRARY)
                return;
            if (id == CLIP && notification == CBN_SELCHANGE)
            {
                stop();
                int n = static_cast<int>(SendMessageW(controls[CLIP], CB_GETCURSEL, 0, 0));
                frame = animation && n > 0 ? animation->clips[n - 1].start : 0;
                updateFrame();
                return;
            }
            if (id == FPS)
            {
                accumulator = 0;
                return;
            }
            if (id == ROOT)
            {
                auto p = choose(window, true, nullptr);
                if (p)
                {
                    root = *p;
                    scan();
                    status(L"폴더 목록을 갱신했습니다. 세트를 선택하고 열어 주세요.");
                }
            }
            else if (id == LOAD)
            {
                int n = static_cast<int>(SendMessageW(controls[LIBRARY], CB_GETCURSEL, 0, 0));
                if (n >= 0 && n < static_cast<int>(entries.size()))
                    loadEntry(entries[n]);
            }
            else if (id == MESHOPEN)
            {
                auto p = choose(window, false, modelFilter);
                if (p)
                    openMesh(*p);
            }
            else if (id == MESHCLEAR)
            {
                slots[selected()] = {};
                refresh(false);
                status(L"선택한 부위의 메시와 텍스처를 해제했습니다.");
            }
            else if (id == VISIBLE)
                slots[selected()].visible = SendMessageW(controls[VISIBLE], BM_GETCHECK, 0, 0) == BST_CHECKED;
            else if (id == BONOPEN)
            {
                auto p = choose(window, false, bonFilter);
                if (p)
                    openBon(*p);
            }
            else if (id == BONCLEAR)
            {
                stop();
                animation.reset();
                frame = 0;
                refresh();
                renderer.fit(slots, nullptr, 0);
                status(L"BON 해제: 파츠 로컬 좌표를 표시합니다. 조립된 기본 자세가 아닙니다.");
            }
            else if (id == PLAY)
            {
                playing = !playing;
                accumulator = 0;
                SetWindowTextW(controls[PLAY], playing ? L"❚❚ 정지" : L"▶ 재생");
            }
            else if (id == FIT)
                renderer.fit(slots, animation.get(), frame);
            else if (id >= WIRE && id <= BONES)
            {
                bool on = SendMessageW(controls[id], BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (id == WIRE)
                    renderer.wire = on;
                if (id == CLAY)
                    renderer.clay = on;
                if (id == GRID)
                    renderer.grid = on;
                if (id == BONES)
                    renderer.bones = on;
            }
            else if (id >= TEXBASE && id < TEXBASE + 8)
            {
                int i = (id - TEXBASE) / 2;
                if ((id - TEXBASE) % 2)
                {
                    slots[selected()].textures[i] = {};
                    status(L"텍스처를 해제했습니다.");
                }
                else
                {
                    auto p = choose(window, false, textureFilter);
                    if (p)
                    {
                        auto texture = renderer.loadTexture(*p);
                        slots[selected()].textures[i] = std::move(texture);
                        status(L"텍스처 교체 완료");
                    }
                }
                refresh(false);
            }
        }
        catch (const std::exception &e)
        {
            status(L"불러오기 실패 · 기존 모델 유지\r\n" + wide(e.what()));
        }
        last = std::chrono::steady_clock::now();
    }
    void tick()
    {
        if (!ready)
            return;
        auto now = std::chrono::steady_clock::now();
        double dt = std::min(.1, std::chrono::duration<double>(now - last).count());
        last = now;
        if (playing && animation)
        {
            int speeds[] = {15, 30, 60};
            int rate = speeds[std::clamp(static_cast<int>(SendMessageW(controls[FPS], CB_GETCURSEL, 0, 0)), 0, 2)];
            accumulator += dt * rate;
            uint32_t step = static_cast<uint32_t>(accumulator);
            if (step)
            {
                accumulator -= step;
                int n = static_cast<int>(SendMessageW(controls[CLIP], CB_GETCURSEL, 0, 0));
                Clip c = n > 0 ? animation->clips[n - 1] : Clip{0, animation->frames};
                frame =
                    c.start + ((frame < c.start || frame >= c.start + c.count ? 0 : frame - c.start) + step) % c.count;
                updateFrame();
            }
        }
        try
        {
            renderer.render(slots, animation.get(), frame);
        }
        catch (const std::exception &e)
        {
            ready = false;
            KillTimer(window, 1);
            status(L"렌더링 오류: " + wide(e.what()));
        }
    }
    ~App()
    {
        if (font)
            DeleteObject(font);
        if (background)
            DeleteObject(background);
    }
};
static LRESULT CALLBACK ViewProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    auto a = reinterpret_cast<App *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (msg == WM_NCCREATE)
    {
        a = static_cast<App *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(a));
    }
    if (!a)
        return DefWindowProcW(h, msg, w, l);
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        a->drag = true;
        a->rightDrag = msg == WM_RBUTTONDOWN;
        a->previous = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        SetCapture(h);
        SetFocus(h);
        return 0;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
        ReleaseCapture();
        a->drag = false;
        return 0;
    case WM_CAPTURECHANGED:
        a->drag = false;
        return 0;
    case WM_MOUSEMOVE:
        if (a->drag)
        {
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            float dx = float(p.x - a->previous.x), dy = float(p.y - a->previous.y);
            if (a->rightDrag)
                a->renderer.pan(dx, dy);
            else
                a->renderer.orbit(dx, dy);
            a->previous = p;
        }
        return 0;
    case WM_MOUSEWHEEL:
        a->renderer.zoom(float(GET_WHEEL_DELTA_WPARAM(w)));
        return 0;
    case WM_KEYDOWN:
        if (w == 'F')
            a->command(FIT, 0);
        if (w == VK_SPACE && a->animation)
            a->command(PLAY, 0);
        return 0;
    case WM_DROPFILES: {
        auto drop = reinterpret_cast<HDROP>(w);
        wchar_t p[32768]{};
        auto count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        DragQueryFileW(drop, 0, p, 32768);
        DragFinish(drop);
        if (count != 1)
        {
            a->status(L"한 번에 한 파일을 놓아 주세요. BSC는 선택한 부위에 적용됩니다.");
            return 0;
        }
        try
        {
            auto ext = lower(fs::path(p).extension().string());
            if (ext == ".bsc")
                a->openMesh(p);
            else if (ext == ".bon")
                a->openBon(p);
            else
                a->status(L"텍스처는 원하는 슬롯의 교체 버튼으로 선택하세요.");
        }
        catch (const std::exception &e)
        {
            a->status(L"기존 모델 유지: " + wide(e.what()));
        }
        return 0;
    }
    }
    return DefWindowProcW(h, msg, w, l);
}
static LRESULT CALLBACK MainProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    auto a = reinterpret_cast<App *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (msg == WM_NCCREATE)
    {
        a = static_cast<App *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(a));
    }
    if (!a)
        return DefWindowProcW(h, msg, w, l);
    switch (msg)
    {
    case WM_CREATE:
        try
        {
            a->create(h);
        }
        catch (const std::exception &e)
        {
            MessageBoxW(h, wide(e.what()).c_str(), L"ModelStudio", MB_ICONERROR);
            return -1;
        }
        return 0;
    case WM_SIZE:
        if (a->ready)
            try
            {
                a->layout();
            }
            catch (const std::exception &e)
            {
                a->status(wide(e.what()));
            }
        return 0;
    case WM_GETMINMAXINFO: {
        auto info = reinterpret_cast<MINMAXINFO *>(l);
        info->ptMinTrackSize = {900, 640};
        return 0;
    }
    case WM_VSCROLL: {
        int code = LOWORD(w);
        if (code == SB_LINEUP)
            a->scrollY -= 32;
        else if (code == SB_LINEDOWN)
            a->scrollY += 32;
        else if (code == SB_PAGEUP)
            a->scrollY -= 300;
        else if (code == SB_PAGEDOWN)
            a->scrollY += 300;
        else if (code == SB_THUMBTRACK)
        {
            SCROLLINFO si{sizeof(si), SIF_TRACKPOS};
            GetScrollInfo(h, SB_VERT, &si);
            a->scrollY = si.nTrackPos;
        }
        a->layout();
        return 0;
    }
    case WM_MOUSEWHEEL:
        a->scrollY -= GET_WHEEL_DELTA_WPARAM(w) / 3;
        a->layout();
        return 0;
    case WM_COMMAND:
        a->command(LOWORD(w), HIWORD(w));
        return 0;
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(l) == a->controls[TIMELINE])
        {
            a->stop();
            a->frame = static_cast<uint32_t>(SendMessageW(a->controls[TIMELINE], TBM_GETPOS, 0, 0));
            a->updateFrame();
        }
        return 0;
    case WM_TIMER:
        a->tick();
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        auto dc = reinterpret_cast<HDC>(w);
        SetTextColor(dc, RGB(222, 231, 244));
        SetBkColor(dc, RGB(24, 32, 45));
        return reinterpret_cast<LRESULT>(a->background);
    }
    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(h, &r);
        FillRect(reinterpret_cast<HDC>(w), &r, a->background);
        return 1;
    }
    case WM_DESTROY:
        KillTimer(h, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}
int runVerification(const fs::path &root, const fs::path &out);
int runInterfaceVerification(App &a, const fs::path &root, const fs::path &out)
{
    fs::create_directories(out);
    std::ofstream log(out / L"interface-verification.txt");
    try
    {
        auto require = [](bool ok, const char *msg) {
            if (!ok)
                throw std::runtime_error(msg);
        };
        a.root = root;
        a.scan();
        require(a.entries.size() >= 4, "Library discovery failed");
        auto entry = std::find_if(a.entries.begin(), a.entries.end(), [](const Entry &e) { return e.player == "az"; });
        require(entry != a.entries.end(), "Missing Az preset");
        a.loadEntry(*entry);
        require(a.slots[1].model && a.slots[2].model && a.slots[3].model, "Player assembly missing slot");
        SendMessageW(a.controls[SLOT], CB_SETCURSEL, 2, 0);
        a.command(SLOT, CBN_SELCHANGE);
        auto body = a.slots[1].model;
        a.openMesh(root / L"플레이어" / L"Az" / L"az_000_arm.bsc");
        require(a.slots[1].model == body, "Arm replacement modified body");
        auto old = a.animation;
        bool rejected = false;
        try
        {
            a.openBon(root / L"플레이어" / L"Hs" / L"hs_bone.bon");
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        require(rejected && a.animation == old, "Incompatible BON replaced valid state");
        a.command(TEXBASE + 1, BN_CLICKED);
        require(!a.slots[2].textures[0].view && a.slots[1].textures[0].view, "Texture removal leaked across slots");
        a.openMesh(root / L"플레이어" / L"Az" / L"az_000_arm.bsc");
        require(bool(a.slots[2].textures[0].view), "Automatic texture replacement failed");
        SendMessageW(a.controls[CLIP], CB_SETCURSEL, 2, 0);
        a.command(CLIP, CBN_SELCHANGE);
        auto c = a.animation->clips[1];
        require(a.frame == c.start, "Clip start mismatch");
        a.command(PLAY, BN_CLICKED);
        a.last -= std::chrono::milliseconds(100);
        a.tick();
        require(a.frame > c.start && a.frame < c.start + c.count, "Clip playback failed");
        a.command(BONCLEAR, BN_CLICKED);
        require(!a.animation && !a.playing && a.frame == 0, "BON detach failed");
        a.command(MESHCLEAR, BN_CLICKED);
        require(!a.slots[2].model && a.slots[1].model == body, "Mesh detach leaked across slots");
        a.loadEntry(*entry);
        a.renderer.render(a.slots, a.animation.get(), 0, false);
        a.renderer.capture(out / L"interface-player.png");
        auto monster = std::find_if(a.entries.begin(), a.entries.end(),
                                    [](const Entry &e) { return e.mesh.stem() == L"BloodKnight"; });
        require(monster != a.entries.end(), "Missing BloodKnight preset");
        a.loadEntry(*monster);
        require(a.slots[0].model && !a.slots[1].model && !a.slots[2].model && !a.slots[3].model,
                "Monster mode did not clear player slots");
        log << "PASS: library, player assembly, independent arm replacement, incompatible BON rollback, per-slot "
               "texture removal/reload, clip selection/playback, BON detach, mesh detach, monster mode switch\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        log << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show)
{
    SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX init{sizeof(init), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&init);
    int count = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &count);
    bool uiVerify = count >= 4 && wcscmp(argv[1], L"--ui-verify") == 0;
    fs::path testRoot = uiVerify ? argv[2] : L"", testOut = uiVerify ? argv[3] : L"";
    if (count >= 4 && wcscmp(argv[1], L"--verify") == 0)
    {
        fs::path root = argv[2], out = argv[3];
        LocalFree(argv);
        int code = runVerification(root, out);
        CoUninitialize();
        return code;
    }
    LocalFree(argv);
    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.lpfnWndProc = MainProc;
    wc.lpszClassName = L"ModelStudioWindow";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    wc.lpfnWndProc = ViewProc;
    wc.lpszClassName = L"StudioViewport";
    RegisterClassW(&wc);
    int result = 1;
    {
        App app;
        HWND h = CreateWindowExW(0, L"ModelStudioWindow", L"ModelStudio · BOUT DX11 Resource Viewer",
                                 WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VSCROLL, CW_USEDEFAULT, CW_USEDEFAULT, 1460,
                                 960, nullptr, nullptr, instance, &app);
        if (h && uiVerify)
        {
            result = runInterfaceVerification(app, testRoot, testOut);
            DestroyWindow(h);
        }
        else if (h)
        {
            ShowWindow(h, show);
            UpdateWindow(h);
            MSG msg{};
            while (GetMessageW(&msg, nullptr, 0, 0) > 0)
            {
                if (!IsDialogMessageW(h, &msg))
                {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
            result = static_cast<int>(msg.wParam);
        }
    }
    CoUninitialize();
    return result;
}
