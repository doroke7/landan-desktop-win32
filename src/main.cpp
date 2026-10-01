// CameraPicker - Win32 + Media Foundation
// 選擇攝影機與解析度並即時預覽
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuids.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

using Microsoft::WRL::ComPtr;

enum { ID_CAM = 101, ID_RES = 102, ID_REFRESH = 103 };
static const int kBarHeight = 44;

struct Res { UINT w, h; };

static HWND g_main, g_cam, g_res, g_status;
static std::vector<ComPtr<IMFActivate>> g_devices;
static std::vector<Res> g_resList;

// ---- 擷取狀態 ----
static ComPtr<IMFMediaSource> g_source;
static ComPtr<IMFSourceReader> g_reader;
static std::thread g_thread;
static std::atomic<bool> g_run{false};
static std::mutex g_frameMutex;
static std::vector<BYTE> g_frame;
static UINT g_fw = 0, g_fh = 0;
static bool g_bottomUp = false;

static void SetStatus(const std::wstring& s) { SetWindowTextW(g_status, s.c_str()); }

static void StopPreview()
{
    g_run = false;
    if (g_thread.joinable()) g_thread.join();
    g_reader.Reset();
    if (g_source) { g_source->Shutdown(); g_source.Reset(); }
    std::lock_guard<std::mutex> lk(g_frameMutex);
    g_frame.clear();
    g_fw = g_fh = 0;
}

static void CaptureLoop()
{
    while (g_run) {
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        HRESULT hr = g_reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                          nullptr, &flags, nullptr, &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) break;
        if (!sample) continue;

        ComPtr<IMFMediaBuffer> buf;
        if (FAILED(sample->ConvertToContiguousBuffer(&buf))) continue;
        BYTE* p = nullptr; DWORD len = 0;
        if (FAILED(buf->Lock(&p, nullptr, &len))) continue;
        {
            std::lock_guard<std::mutex> lk(g_frameMutex);
            size_t need = (size_t)g_fw * g_fh * 4;
            if (g_frame.size() == need) memcpy(g_frame.data(), p, std::min<size_t>(len, need));
        }
        buf->Unlock();

        RECT rc = { 0, kBarHeight, 0, 0 };
        GetClientRect(g_main, &rc);
        rc.top = kBarHeight;
        InvalidateRect(g_main, &rc, FALSE);
    }
}

static bool ActivateDevice(int idx, ComPtr<IMFMediaSource>& src)
{
    if (idx < 0 || idx >= (int)g_devices.size()) return false;
    return SUCCEEDED(g_devices[idx]->ActivateObject(IID_PPV_ARGS(&src)));
}

static ComPtr<IMFSourceReader> CreateReader(IMFMediaSource* src)
{
    ComPtr<IMFAttributes> attr;
    MFCreateAttributes(&attr, 2);
    // 允許 Source Reader 自動插入色彩轉換 / MJPG 解碼
    attr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    attr->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
    ComPtr<IMFSourceReader> reader;
    MFCreateSourceReaderFromMediaSource(src, attr.Get(), &reader);
    return reader;
}

// 列出某台攝影機支援的解析度
static void FillResolutions(int camIdx)
{
    g_resList.clear();
    SendMessageW(g_res, CB_RESETCONTENT, 0, 0);

    ComPtr<IMFMediaSource> src;
    if (!ActivateDevice(camIdx, src)) return;
    auto reader = CreateReader(src.Get());
    if (reader) {
        for (DWORD i = 0;; ++i) {
            ComPtr<IMFMediaType> type;
            if (FAILED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &type))) break;
            UINT32 w = 0, h = 0;
            if (FAILED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h))) continue;
            bool dup = std::any_of(g_resList.begin(), g_resList.end(),
                                   [&](const Res& r) { return r.w == w && r.h == h; });
            if (!dup) g_resList.push_back({ w, h });
        }
    }
    reader.Reset();
    src->Shutdown();
    g_devices[camIdx]->ShutdownObject();

    std::sort(g_resList.begin(), g_resList.end(), [](const Res& a, const Res& b) {
        return (UINT64)a.w * a.h > (UINT64)b.w * b.h;
    });
    for (auto& r : g_resList) {
        std::wstring s = std::to_wstring(r.w) + L" x " + std::to_wstring(r.h);
        SendMessageW(g_res, CB_ADDSTRING, 0, (LPARAM)s.c_str());
    }
    if (!g_resList.empty()) SendMessageW(g_res, CB_SETCURSEL, 0, 0);
}

static void StartPreview(int camIdx, int resIdx)
{
    StopPreview();
    if (resIdx < 0 || resIdx >= (int)g_resList.size()) return;
    Res want = g_resList[resIdx];

    g_devices[camIdx]->ShutdownObject();
    if (!ActivateDevice(camIdx, g_source)) { SetStatus(L"無法開啟攝影機（可能被其他程式佔用）"); return; }
    g_reader = CreateReader(g_source.Get());
    if (!g_reader) { SetStatus(L"建立 Source Reader 失敗"); StopPreview(); return; }

    // 找符合解析度且 fps 最高的原生格式
    ComPtr<IMFMediaType> best;
    double bestFps = -1;
    for (DWORD i = 0;; ++i) {
        ComPtr<IMFMediaType> t;
        if (FAILED(g_reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &t))) break;
        UINT32 w = 0, h = 0, n = 0, d = 1;
        MFGetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, &w, &h);
        if (w != want.w || h != want.h) continue;
        MFGetAttributeRatio(t.Get(), MF_MT_FRAME_RATE, &n, &d);
        double fps = d ? (double)n / d : 0;
        if (fps > bestFps) { bestFps = fps; best = t; }
    }
    if (!best || FAILED(g_reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, best.Get()))) {
        SetStatus(L"不支援此解析度"); StopPreview(); return;
    }

    // 輸出轉成 RGB32 供 GDI 顯示
    ComPtr<IMFMediaType> out;
    MFCreateMediaType(&out);
    out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    MFSetAttributeSize(out.Get(), MF_MT_FRAME_SIZE, want.w, want.h);
    if (FAILED(g_reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, out.Get()))) {
        SetStatus(L"無法轉換為 RGB32"); StopPreview(); return;
    }

    ComPtr<IMFMediaType> cur;
    g_reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur);
    UINT32 w = want.w, h = want.h;
    MFGetAttributeSize(cur.Get(), MF_MT_FRAME_SIZE, &w, &h);
    INT32 stride = (INT32)MFGetAttributeUINT32(cur.Get(), MF_MT_DEFAULT_STRIDE, 0);

    {
        std::lock_guard<std::mutex> lk(g_frameMutex);
        g_fw = w; g_fh = h;
        g_bottomUp = stride < 0;
        g_frame.assign((size_t)w * h * 4, 0);
    }
    g_run = true;
    g_thread = std::thread(CaptureLoop);

    wchar_t buf[128];
    swprintf_s(buf, L"預覽中：%u x %u @ %.0f fps", w, h, bestFps);
    SetStatus(buf);
}

static void FillCameras()
{
    StopPreview();
    g_devices.clear();
    SendMessageW(g_cam, CB_RESETCONTENT, 0, 0);
    SendMessageW(g_res, CB_RESETCONTENT, 0, 0);
    g_resList.clear();

    ComPtr<IMFAttributes> attr;
    MFCreateAttributes(&attr, 1);
    attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    IMFActivate** devs = nullptr;
    UINT32 count = 0;
    if (SUCCEEDED(MFEnumDeviceSources(attr.Get(), &devs, &count))) {
        for (UINT32 i = 0; i < count; ++i) {
            WCHAR* name = nullptr; UINT32 len = 0;
            std::wstring s = L"(未命名攝影機)";
            if (SUCCEEDED(devs[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &len))) {
                s = name; CoTaskMemFree(name);
            }
            SendMessageW(g_cam, CB_ADDSTRING, 0, (LPARAM)s.c_str());
            g_devices.emplace_back();
            g_devices.back().Attach(devs[i]);  // 接管參考計數
        }
        CoTaskMemFree(devs);
    }

    if (g_devices.empty()) { SetStatus(L"找不到攝影機"); InvalidateRect(g_main, nullptr, TRUE); return; }
    SendMessageW(g_cam, CB_SETCURSEL, 0, 0);
    FillResolutions(0);
    StartPreview(0, 0);
}

static void Layout(int cw)
{
    int x = 10, y = 8;
    MoveWindow(GetDlgItem(g_main, 201), x, y + 4, 40, 20, TRUE);
    MoveWindow(g_cam, x + 44, y, 260, 300, TRUE);
    MoveWindow(GetDlgItem(g_main, 202), x + 316, y + 4, 56, 20, TRUE);
    MoveWindow(g_res, x + 374, y, 140, 300, TRUE);
    MoveWindow(GetDlgItem(g_main, ID_REFRESH), x + 526, y, 70, 26, TRUE);
    MoveWindow(g_status, x + 606, y + 4, std::max(50, cw - (x + 616)), 20, TRUE);
}

static void OnPaint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc; GetClientRect(hwnd, &rc);

    // 工具列背景
    RECT bar = { 0, 0, rc.right, kBarHeight };
    FillRect(hdc, &bar, GetSysColorBrush(COLOR_BTNFACE));

    RECT view = { 0, kBarHeight, rc.right, rc.bottom };
    FillRect(hdc, &view, (HBRUSH)GetStockObject(BLACK_BRUSH));

    std::lock_guard<std::mutex> lk(g_frameMutex);
    if (!g_frame.empty() && g_fw && g_fh) {
        int vw = view.right - view.left, vh = view.bottom - view.top;
        double scale = std::min((double)vw / g_fw, (double)vh / g_fh);
        int dw = (int)(g_fw * scale), dh = (int)(g_fh * scale);
        int dx = (vw - dw) / 2, dy = kBarHeight + (vh - dh) / 2;

        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = (LONG)g_fw;
        bi.bmiHeader.biHeight = g_bottomUp ? (LONG)g_fh : -(LONG)g_fh;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(hdc, COLORONCOLOR);
        StretchDIBits(hdc, dx, dy, dw, dh, 0, 0, g_fw, g_fh, g_frame.data(), &bi,
                      DIB_RGB_COLORS, SRCCOPY);
    }
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        g_main = hwnd;
        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
            HWND h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10,
                                     hwnd, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);
            return h;
        };
        mk(L"STATIC", L"相機", 0, 201);
        g_cam = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_CAM);
        mk(L"STATIC", L"解析度", 0, 202);
        g_res = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_RES);
        mk(L"BUTTON", L"重新整理", BS_PUSHBUTTON | WS_TABSTOP, ID_REFRESH);
        g_status = mk(L"STATIC", L"", SS_ENDELLIPSIS, 203);
        PostMessageW(hwnd, WM_APP, 0, 0);  // 視窗顯示後再列舉裝置
        return 0;
    }
    case WM_APP:
        FillCameras();
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_CAM && HIWORD(wp) == CBN_SELCHANGE) {
            int c = (int)SendMessageW(g_cam, CB_GETCURSEL, 0, 0);
            StopPreview();
            FillResolutions(c);
            StartPreview(c, 0);
        } else if (LOWORD(wp) == ID_RES && HIWORD(wp) == CBN_SELCHANGE) {
            StartPreview((int)SendMessageW(g_cam, CB_GETCURSEL, 0, 0),
                         (int)SendMessageW(g_res, CB_GETCURSEL, 0, 0));
        } else if (LOWORD(wp) == ID_REFRESH) {
            FillCameras();
        }
        return 0;
    case WM_SIZE:
        Layout(LOWORD(lp));
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        OnPaint(hwnd);
        return 0;
    case WM_DESTROY:
        StopPreview();
        g_devices.clear();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nShow)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"CameraPickerWnd";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Camera Picker", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1000, 700, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    MFShutdown();
    CoUninitialize();
    return 0;
}
