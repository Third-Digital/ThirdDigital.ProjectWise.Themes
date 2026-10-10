#include "Fixtures.h"

#include <commctrl.h>
#include <dwmapi.h>

#include <algorithm>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>

namespace Harness
{
    std::vector<CheckDefinition>& Checks()
    {
        static std::vector<CheckDefinition> checks;
        return checks;
    }

    CheckRegistration::CheckRegistration(const CheckDefinition& definition)
    {
        Checks().push_back(definition);
    }

    namespace
    {
        void Write(const wchar_t* prefix, const wchar_t* name, const wchar_t* format, va_list args)
        {
            wchar_t message[2048];
            _vsnwprintf_s(message, _TRUNCATE, format, args);
            std::fwprintf(stdout, L"%s %s: %s\n", prefix, name, message);
            std::fflush(stdout);
        }
    }

    Recorder::Recorder(const wchar_t* checkName) : name_(checkName) {}

    bool Recorder::Expect(bool condition, const wchar_t* format, ...)
    {
        va_list args;
        va_start(args, format);
        Write(condition ? L"ok  " : L"FAIL", name_, format, args);
        va_end(args);
        if (!condition)
        {
            ++failures_;
        }
        return condition;
    }

    void Recorder::Invalidate(const wchar_t* format, ...)
    {
        va_list args;
        va_start(args, format);
        Write(L"INVALID", name_, format, args);
        va_end(args);
        ++invalid_;
    }

    void Recorder::Note(const wchar_t* format, ...)
    {
        va_list args;
        va_start(args, format);
        Write(L"note", name_, format, args);
        va_end(args);
    }

    bool Recorder::Clean() const
    {
        return failures_ == 0 && invalid_ == 0;
    }

    int Recorder::Result() const
    {
        if (invalid_ > 0)
        {
            return Invalid;
        }
        return failures_ > 0 ? Failed : Passed;
    }

    std::wstring SettingsKey()
    {
        wchar_t value[512]{};
        const DWORD length = GetEnvironmentVariableW(L"TDPWTHEMES_SETTINGS_KEY", value, ARRAYSIZE(value));
        if (length == 0 || length >= ARRAYSIZE(value))
        {
            return {};
        }
        return value;
    }

    void DeleteSettingsKey()
    {
        const std::wstring key = SettingsKey();
        if (key.rfind(L"Software\\TDPWThemesHarness-", 0) != 0)
        {
            return;
        }
        RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str());
        RegDeleteKeyW(HKEY_CURRENT_USER, key.c_str());
    }

    std::optional<COLORREF> BrushColour(HBRUSH brush)
    {
        LOGBRUSH info{};
        if (brush == nullptr || GetObjectW(brush, sizeof(info), &info) != sizeof(info) || info.lbStyle != BS_SOLID)
        {
            return std::nullopt;
        }
        return info.lbColor;
    }

    HTHEME WindowTheme()
    {
        static HWND window = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 16, 16, nullptr, nullptr, nullptr, nullptr);
        static HTHEME theme = window != nullptr ? OpenThemeData(window, L"WINDOW") : nullptr;
        return theme;
    }

    ApiSnapshot TakeSnapshot(HTHEME theme)
    {
        ApiSnapshot s;
        for (int i = -2; i <= COLOR_MENUBAR + 2; ++i)
        {
            s.indices.push_back(i);
        }
        for (int i = 1599; i <= 1633; ++i)
        {
            s.indices.push_back(i);
        }
        s.indices.push_back(9999);

        for (int i : s.indices)
        {
            s.sysColour.push_back(GetSysColor(i));
            s.sysBrush.push_back(BrushColour(GetSysColorBrush(i)));
            s.themeColour.push_back(GetThemeSysColor(theme, i));
            s.nullThemeColour.push_back(GetThemeSysColor(nullptr, i));
            HBRUSH owned = GetThemeSysColorBrush(theme, i);
            s.themeBrush.push_back(BrushColour(owned));
            if (owned != nullptr)
            {
                DeleteObject(owned);
            }
            HBRUSH stock = GetThemeSysColorBrush(nullptr, i);
            s.nullThemeBrush.push_back(BrushColour(stock));
        }
        return s;
    }

    void WriteEvidence(const CheckContext& context, const wchar_t* fileName, const std::wstring& text)
    {
        if (context.evidenceDir.empty())
        {
            return;
        }
        const std::wstring path = context.evidenceDir + L"\\" + fileName;
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return;
        }
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string utf8(static_cast<std::size_t>(bytes), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), utf8.data(), bytes, nullptr, nullptr);
        DWORD written = 0;
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        CloseHandle(file);
    }

    void Pump(DWORD milliseconds)
    {
        const ULONGLONG end = GetTickCount64() + milliseconds;
        do
        {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
        } while (GetTickCount64() < end);
    }

    HWND CreateTopLevel(const wchar_t* title, int width, int height)
    {
        static const ATOM atom = [] {
            INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES | ICC_TAB_CLASSES};
            InitCommonControlsEx(&icc);
            WNDCLASSEXW wc{sizeof(wc)};
            wc.lpfnWndProc = DefWindowProcW;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_3DFACE + 1);
            wc.lpszClassName = L"TDPWHarnessWindow";
            return RegisterClassExW(&wc);
        }();
        if (atom == 0)
        {
            return nullptr;
        }
        HWND window = CreateWindowExW(0, L"TDPWHarnessWindow", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN, 60, 60, width, height,
                                      nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (window != nullptr)
        {
            SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            UpdateWindow(window);
        }
        return window;
    }

    HWND CreateChild(const wchar_t* className, HWND parent, DWORD style, int x, int y, int width, int height, const wchar_t* text)
    {
        HWND child = CreateWindowExW(0, className, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, parent, nullptr,
                                     GetModuleHandleW(nullptr), nullptr);
        if (child != nullptr)
        {
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(AliasedFont()), TRUE);
        }
        return child;
    }

    HFONT AliasedFont()
    {
        static HFONT font = CreateFontW(-20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        NONANTIALIASED_QUALITY, DEFAULT_PITCH, L"Arial");
        return font;
    }

    namespace
    {
        void Align(std::vector<WORD>& t)
        {
            if (t.size() % 2 != 0)
            {
                t.push_back(0);
            }
        }

        void Text(std::vector<WORD>& t, const wchar_t* s)
        {
            for (; *s; ++s)
            {
                t.push_back(static_cast<WORD>(*s));
            }
            t.push_back(0);
        }

        void Push(std::vector<WORD>& t, const void* data, std::size_t bytes)
        {
            const WORD* words = static_cast<const WORD*>(data);
            t.insert(t.end(), words, words + bytes / sizeof(WORD));
        }

        void Item(std::vector<WORD>& t, DWORD style, short x, short y, short cx, short cy, WORD id, WORD atom, const wchar_t* text)
        {
            Align(t);
            DLGITEMTEMPLATE item{WS_CHILD | WS_VISIBLE | style, 0, x, y, cx, cy, id};
            Push(t, &item, sizeof(item));
            t.push_back(0xFFFF);
            t.push_back(atom);
            Text(t, text);
            t.push_back(0);
        }
    }

    std::vector<WORD> DialogTemplate(const wchar_t* title, DWORD style)
    {
        std::vector<WORD> t;
        DLGTEMPLATE dlg{style | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 0, 2, 40, 40, 240, 140};
        Push(t, &dlg, sizeof(dlg));
        t.push_back(0);
        t.push_back(0);
        Text(t, title);
        t.push_back(9);
        Text(t, L"Segoe UI");
        Item(t, SS_LEFT, 10, 10, 200, 20, 100, 0x0082, L"WWWWWW");
        Item(t, ES_LEFT | WS_BORDER, 10, 50, 200, 24, 101, 0x0081, L"WWWWWW");
        return t;
    }

    COLORREF Image::At(int x, int y) const
    {
        const DWORD p = pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
        return RGB((p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF);
    }

    namespace
    {
        constexpr DWORD kSentinel = 0x00FE01FE;
        constexpr int kCaptureAttempts = 3;
    }

    bool Capture(HWND topLevel, Image& image)
    {
        RECT client{};
        if (!GetClientRect(topLevel, &client) || client.right <= 0 || client.bottom <= 0)
        {
            return false;
        }
        DwmFlush();
        image.width = client.right;
        image.height = client.bottom;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = image.width;
        info.bmiHeader.biHeight = -image.height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HDC screen = GetDC(nullptr);
        HDC memory = CreateCompatibleDC(screen);
        HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, screen);
        if (memory == nullptr || bitmap == nullptr || bits == nullptr)
        {
            if (bitmap != nullptr) DeleteObject(bitmap);
            if (memory != nullptr) DeleteDC(memory);
            return false;
        }
        const std::size_t count = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
        DWORD* raw = static_cast<DWORD*>(bits);
        std::fill(raw, raw + count, kSentinel);
        HGDIOBJ previous = SelectObject(memory, bitmap);
        const BOOL printed = PrintWindow(topLevel, memory, PW_CLIENTONLY | PW_RENDERFULLCONTENT);
        GdiFlush();
        image.pixels.assign(raw, raw + count);
        SelectObject(memory, previous);
        DeleteObject(bitmap);
        DeleteDC(memory);
        return printed != FALSE;
    }

    namespace
    {
        bool ToTopClient(HWND target, POINT windowPoint, HWND& top, POINT& client)
        {
            top = GetAncestor(target, GA_ROOT);
            RECT windowRect{};
            if (top == nullptr || !GetWindowRect(target, &windowRect))
            {
                return false;
            }
            POINT screen{windowRect.left + windowPoint.x, windowRect.top + windowPoint.y};
            client = screen;
            return ScreenToClient(top, &client) != FALSE;
        }
    }

    std::optional<COLORREF> Sample(Recorder& r, HWND target, POINT windowPoint)
    {
        HWND top = nullptr;
        POINT at{};
        if (!ToTopClient(target, windowPoint, top, at))
        {
            r.Invalidate(L"cannot map sample point");
            return std::nullopt;
        }
        // A pair of captures can straddle a repaint; only a stable, drawn pair is accepted.
        for (int attempt = 1;; ++attempt)
        {
            Image first;
            Image second;
            if (!Capture(top, first) || !Capture(top, second))
            {
                r.Invalidate(L"PrintWindow failed");
                return std::nullopt;
            }
            if (at.x < 0 || at.y < 0 || at.x >= first.width || at.y >= first.height)
            {
                r.Invalidate(L"sample point (%ld,%ld) outside the %dx%d capture", at.x, at.y, first.width, first.height);
                return std::nullopt;
            }
            const std::size_t index = static_cast<std::size_t>(at.y) * static_cast<std::size_t>(first.width) + static_cast<std::size_t>(at.x);
            if (first.pixels[index] != kSentinel && first.pixels[index] == second.pixels[index])
            {
                return first.At(at.x, at.y);
            }
            if (attempt == kCaptureAttempts)
            {
                r.Invalidate(L"sample (%ld,%ld) undrawn or unstable in %d capture pairs", at.x, at.y, kCaptureAttempts);
                return std::nullopt;
            }
            Pump(100);
        }
    }

    std::optional<TextCell> SampleText(Recorder& r, HWND target, const RECT& clientRect, COLORREF expectedText)
    {
        // A focused edit's blinking caret would make the text cell differ between captures.
        SetFocus(nullptr);
        Pump(50);
        POINT origin{clientRect.left, clientRect.top};
        MapWindowPoints(target, nullptr, &origin, 1);
        RECT windowRect{};
        GetWindowRect(target, &windowRect);
        HWND top = nullptr;
        POINT at{};
        if (!ToTopClient(target, {origin.x - windowRect.left, origin.y - windowRect.top}, top, at))
        {
            r.Invalidate(L"cannot map text cell");
            return std::nullopt;
        }
        Image first;
        Image second;
        const int w = clientRect.right - clientRect.left;
        const int h = clientRect.bottom - clientRect.top;
        for (int attempt = 1;; ++attempt)
        {
            if (!Capture(top, first) || !Capture(top, second))
            {
                r.Invalidate(L"PrintWindow failed");
                return std::nullopt;
            }
            if (at.x < 0 || at.y < 0 || at.x + w > first.width || at.y + h > first.height || w <= 0 || h <= 0)
            {
                r.Invalidate(L"text cell outside the capture");
                return std::nullopt;
            }
            bool stable = true;
            for (int y = at.y; stable && y < at.y + h; ++y)
            {
                for (int x = at.x; x < at.x + w; ++x)
                {
                    const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(first.width) + static_cast<std::size_t>(x);
                    if (first.pixels[index] == kSentinel || first.pixels[index] != second.pixels[index])
                    {
                        stable = false;
                        break;
                    }
                }
            }
            if (stable)
            {
                break;
            }
            if (attempt == kCaptureAttempts)
            {
                r.Invalidate(L"text cell undrawn or unstable in %d capture pairs", kCaptureAttempts);
                return std::nullopt;
            }
            Pump(100);
        }
        std::map<COLORREF, int> histogram;
        for (int y = at.y; y < at.y + h; ++y)
        {
            for (int x = at.x; x < at.x + w; ++x)
            {
                ++histogram[first.At(x, y)];
            }
        }
        TextCell cell;
        int most = 0;
        for (const auto& [colour, count] : histogram)
        {
            if (count > most)
            {
                most = count;
                cell.background = colour;
            }
        }
        auto distance = [](COLORREF a, COLORREF b) {
            return std::abs(GetRValue(a) - GetRValue(b)) + std::abs(GetGValue(a) - GetGValue(b)) + std::abs(GetBValue(a) - GetBValue(b));
        };
        int best = INT_MAX;
        for (const auto& [colour, count] : histogram)
        {
            if (colour != cell.background && distance(colour, expectedText) < best)
            {
                best = distance(colour, expectedText);
                cell.text = colour;
                cell.textPixels = count;
            }
        }
        return cell;
    }

    bool PatchImports(HMODULE module, void* target, void* replacement)
    {
        auto* base = reinterpret_cast<BYTE*>(module);
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (directory.VirtualAddress == 0)
        {
            return false;
        }
        bool patched = false;
        for (auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress); descriptor->Name != 0; ++descriptor)
        {
            for (auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk); thunk->u1.Function != 0; ++thunk)
            {
                if (reinterpret_cast<void*>(thunk->u1.Function) != target)
                {
                    continue;
                }
                DWORD protection = 0;
                if (VirtualProtect(&thunk->u1.Function, sizeof(thunk->u1.Function), PAGE_READWRITE, &protection))
                {
                    thunk->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);
                    VirtualProtect(&thunk->u1.Function, sizeof(thunk->u1.Function), protection, &protection);
                    patched = true;
                }
            }
        }
        return patched;
    }

    namespace
    {
        // The real function bound in this executable's import table. comctl32's subclass functions are
        // imported by ordinal and are not declared dllimport, so their C++ address is a local thunk.
        void* Imported(const char* dll, const char* name, WORD ordinal)
        {
            auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
            auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
            const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
            for (auto* d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress); d->Name != 0; ++d)
            {
                if (_stricmp(reinterpret_cast<const char*>(base + d->Name), dll) != 0)
                {
                    continue;
                }
                auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->OriginalFirstThunk);
                auto* bound = reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->FirstThunk);
                for (; names->u1.AddressOfData != 0; ++names, ++bound)
                {
                    const bool match = IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)
                                           ? IMAGE_ORDINAL(names->u1.Ordinal) == ordinal
                                           : std::strcmp(reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name, name) == 0;
                    if (match)
                    {
                        return reinterpret_cast<void*>(bound->u1.Function);
                    }
                }
            }
            return nullptr;
        }

        std::mutex g_callsLock;
        std::vector<RecordedCall> g_calls;
        bool g_failDwm = false;

        // Captured before this executable's own import table is patched, so the recorders call the real functions.
        struct Originals
        {
            decltype(&::SetWindowTheme) setWindowTheme = &::SetWindowTheme;
            decltype(&::SetWindowSubclass) setWindowSubclass =
                reinterpret_cast<decltype(&::SetWindowSubclass)>(Imported("COMCTL32.dll", "SetWindowSubclass", 410));
            decltype(&::RemoveWindowSubclass) removeWindowSubclass =
                reinterpret_cast<decltype(&::RemoveWindowSubclass)>(Imported("COMCTL32.dll", "RemoveWindowSubclass", 412));
            decltype(&::DwmSetWindowAttribute) dwmSetWindowAttribute = &::DwmSetWindowAttribute;
            decltype(&::SendMessageTimeoutW) sendMessageTimeout = &::SendMessageTimeoutW;
            decltype(&::SendNotifyMessageW) sendNotifyMessage = &::SendNotifyMessageW;
            decltype(&::RedrawWindow) redrawWindow = &::RedrawWindow;
        };

        const Originals& Real()
        {
            static const Originals originals;
            return originals;
        }

        thread_local int t_createDepth = 0;

        LRESULT CALLBACK BeforeMessage(int code, WPARAM wParam, LPARAM lParam)
        {
            const auto* message = reinterpret_cast<const CWPSTRUCT*>(lParam);
            if (code == HC_ACTION && (message->message == WM_NCCREATE || message->message == WM_CREATE))
            {
                ++t_createDepth;
            }
            return CallNextHookEx(nullptr, code, wParam, lParam);
        }

        LRESULT CALLBACK AfterMessage(int code, WPARAM wParam, LPARAM lParam)
        {
            const auto* message = reinterpret_cast<const CWPRETSTRUCT*>(lParam);
            if (code == HC_ACTION && (message->message == WM_NCCREATE || message->message == WM_CREATE) && t_createDepth > 0)
            {
                --t_createDepth;
            }
            return CallNextHookEx(nullptr, code, wParam, lParam);
        }

        void Record(RecordedCall call)
        {
            call.duringCreate = t_createDepth > 0;
            std::lock_guard<std::mutex> lock(g_callsLock);
            g_calls.push_back(std::move(call));
        }

        HRESULT WINAPI RecordSetWindowTheme(HWND window, LPCWSTR app, LPCWSTR idList)
        {
            Record({RecordedCall::Kind::SetWindowTheme, window, 0, 0, app != nullptr ? app : L"(null)"});
            return Real().setWindowTheme(window, app, idList);
        }

        BOOL WINAPI RecordSetWindowSubclass(HWND window, SUBCLASSPROC proc, UINT_PTR id, DWORD_PTR data)
        {
            Record({RecordedCall::Kind::SetWindowSubclass, window, 0, 0, {}});
            return Real().setWindowSubclass(window, proc, id, data);
        }

        BOOL WINAPI RecordRemoveWindowSubclass(HWND window, SUBCLASSPROC proc, UINT_PTR id)
        {
            Record({RecordedCall::Kind::RemoveWindowSubclass, window, 0, 0, {}});
            return Real().removeWindowSubclass(window, proc, id);
        }

        HRESULT WINAPI RecordDwmSetWindowAttribute(HWND window, DWORD attribute, LPCVOID data, DWORD size)
        {
            const DWORD value = data != nullptr && size >= sizeof(DWORD) ? *static_cast<const DWORD*>(data) : 0;
            Record({RecordedCall::Kind::DwmSetWindowAttribute, window, attribute, value, {}});
            if (g_failDwm)
            {
                return E_INVALIDARG;
            }
            return Real().dwmSetWindowAttribute(window, attribute, data, size);
        }

        LRESULT WINAPI RecordSendMessageTimeoutW(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT flags, UINT timeout, PDWORD_PTR result)
        {
            Record({RecordedCall::Kind::SendMessageTimeout, window, message, flags, {}});
            return Real().sendMessageTimeout(window, message, wParam, lParam, flags, timeout, result);
        }

        BOOL WINAPI RecordSendNotifyMessageW(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            Record({RecordedCall::Kind::SendNotifyMessage, window, message, 0, {}});
            return Real().sendNotifyMessage(window, message, wParam, lParam);
        }

        BOOL WINAPI RecordRedrawWindow(HWND window, const RECT* rect, HRGN region, UINT flags)
        {
            Record({RecordedCall::Kind::RedrawWindow, window, 0, flags, {}});
            return Real().redrawWindow(window, rect, region, flags);
        }
    }

    bool InstallCallRecorder(HMODULE module)
    {
        if (module == nullptr)
        {
            module = GetModuleHandleW(nullptr);
        }
        const Originals& real = Real();
        const std::pair<void*, void*> targets[] = {
            {reinterpret_cast<void*>(real.setWindowTheme), reinterpret_cast<void*>(RecordSetWindowTheme)},
            {reinterpret_cast<void*>(real.setWindowSubclass), reinterpret_cast<void*>(RecordSetWindowSubclass)},
            {reinterpret_cast<void*>(real.removeWindowSubclass), reinterpret_cast<void*>(RecordRemoveWindowSubclass)},
            {reinterpret_cast<void*>(real.dwmSetWindowAttribute), reinterpret_cast<void*>(RecordDwmSetWindowAttribute)},
            {reinterpret_cast<void*>(real.sendMessageTimeout), reinterpret_cast<void*>(RecordSendMessageTimeoutW)},
            {reinterpret_cast<void*>(real.sendNotifyMessage), reinterpret_cast<void*>(RecordSendNotifyMessageW)},
            {reinterpret_cast<void*>(real.redrawWindow), reinterpret_cast<void*>(RecordRedrawWindow)},
        };
        bool any = false;
        for (const auto& [target, replacement] : targets)
        {
            any |= PatchImports(module, target, replacement);
        }
        return any;
    }

    std::vector<RecordedCall> RecordedCalls(HWND window)
    {
        std::lock_guard<std::mutex> lock(g_callsLock);
        std::vector<RecordedCall> result;
        for (const RecordedCall& call : g_calls)
        {
            if (window == nullptr || call.window == window)
            {
                result.push_back(call);
            }
        }
        return result;
    }

    void ClearRecordedCalls()
    {
        std::lock_guard<std::mutex> lock(g_callsLock);
        g_calls.clear();
    }

    void FailDwmCalls(bool fail)
    {
        g_failDwm = fail;
    }

    namespace
    {
        template <typename T>
        bool Same(const std::optional<T>& a, const std::optional<T>& b)
        {
            return a.has_value() == b.has_value() && (!a || *a == *b);
        }
    }

    void ExpectSnapshot(Recorder& r, const ApiSnapshot& baseline, const ApiSnapshot& now, const SystemColourPalette* palette, const wchar_t* label)
    {
        int mismatches = 0;
        for (std::size_t k = 0; k < baseline.indices.size(); ++k)
        {
            const int i = baseline.indices[k];
            const auto mapped = palette != nullptr ? palette->At(i) : std::nullopt;
            bool ok = false;
            if (mapped)
            {
                const std::optional<COLORREF> want = *mapped;
                ok = now.sysColour[k] == *mapped && Same(now.sysBrush[k], want) && now.themeColour[k] == *mapped &&
                     now.nullThemeColour[k] == *mapped && Same(now.themeBrush[k], want) && Same(now.nullThemeBrush[k], want);
                if (!ok)
                {
                    r.Expect(false, L"%s index %d: GetSysColor=%06lX theme=%06lX want %06lX", label, i, now.sysColour[k], now.themeColour[k], *mapped);
                }
            }
            else
            {
                ok = now.sysColour[k] == baseline.sysColour[k] && Same(now.sysBrush[k], baseline.sysBrush[k]) &&
                     now.themeColour[k] == baseline.themeColour[k] && now.nullThemeColour[k] == baseline.nullThemeColour[k] &&
                     Same(now.themeBrush[k], baseline.themeBrush[k]) && Same(now.nullThemeBrush[k], baseline.nullThemeBrush[k]);
                if (!ok)
                {
                    r.Expect(false, L"%s index %d: GetSysColor=%06lX baseline %06lX", label, i, now.sysColour[k], baseline.sysColour[k]);
                }
            }
            mismatches += ok ? 0 : 1;
        }
        r.Expect(mismatches == 0, L"%s: all %u API results as expected (%d mismatches)", label, static_cast<unsigned>(baseline.indices.size()), mismatches);
    }

    HWND ListWithScrollbar(HWND parent, int x, int y, int width, int height)
    {
        HWND list = CreateChild(WC_LISTVIEWW, parent, LVS_REPORT | WS_BORDER, x, y, width, height);
        LVCOLUMNW column{LVCF_TEXT | LVCF_WIDTH, 0, 180, const_cast<wchar_t*>(L"Name")};
        ListView_InsertColumn(list, 0, &column);
        for (int i = 0; i < 100; ++i)
        {
            LVITEMW item{LVIF_TEXT};
            item.iItem = i;
            item.pszText = const_cast<wchar_t*>(L"Item");
            ListView_InsertItem(list, &item);
        }
        return list;
    }

    HWND ShortTree(HWND parent, int x, int y, int width, int height)
    {
        HWND tree = CreateChild(WC_TREEVIEWW, parent, TVS_HASLINES | WS_BORDER, x, y, width, height);
        TVINSERTSTRUCTW insert{};
        insert.hParent = TVI_ROOT;
        insert.hInsertAfter = TVI_LAST;
        insert.item.mask = TVIF_TEXT;
        insert.item.pszText = const_cast<wchar_t*>(L"Folder");
        TreeView_InsertItem(tree, &insert);
        return tree;
    }

    bool LoadProductionModule(Recorder& r, const CheckContext& context, ProductionModule& module)
    {
        if (context.dllPath.empty())
        {
            r.Invalidate(L"no --dll path");
            return false;
        }
        module.handle = LoadLibraryW(context.dllPath.c_str());
        if (module.handle == nullptr)
        {
            r.Invalidate(L"LoadLibrary(%s) failed (%lu)", context.dllPath.c_str(), GetLastError());
            return false;
        }
        module.customInitialize = reinterpret_cast<LONG(WINAPI*)(ULONG, LPVOID)>(GetProcAddress(module.handle, "CustomInitialize"));
        if (module.customInitialize == nullptr)
        {
            r.Invalidate(L"CustomInitialize not exported");
            return false;
        }
        return true;
    }

    int LogLines(const wchar_t* text)
    {
        wchar_t path[MAX_PATH + 64]{};
        GetTempPathW(MAX_PATH, path);
        wcscat_s(path, L"ThirdDigital\\projectwise-themes.log");
        HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return 0;
        }
        std::string bytes;
        char buffer[4096];
        DWORD read = 0;
        while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read > 0)
        {
            bytes.append(buffer, read);
        }
        CloseHandle(file);
        const int chars = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        std::wstring content(static_cast<std::size_t>(chars), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), content.data(), chars);
        int count = 0;
        std::size_t start = 0;
        while (start < content.size())
        {
            std::size_t end = content.find(L'\n', start);
            if (end == std::wstring::npos)
            {
                end = content.size();
            }
            if (content.substr(start, end - start).find(text) != std::wstring::npos)
            {
                ++count;
            }
            start = end + 1;
        }
        return count;
    }

    namespace
    {
        int g_hiddenSysColourChanges = 0;

        LRESULT CALLBACK HiddenProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            if (message == WM_SYSCOLORCHANGE)
            {
                ++g_hiddenSysColourChanges;
            }
            return DefWindowProcW(window, message, wParam, lParam);
        }

        HWND OpenModeless(const wchar_t* title)
        {
            std::vector<WORD> tmpl = DialogTemplate(title, DS_MODALFRAME);
            HWND dialog = CreateDialogIndirectParamW(GetModuleHandleW(nullptr), reinterpret_cast<LPCDLGTEMPLATEW>(tmpl.data()), nullptr,
                                                     [](HWND, UINT message, WPARAM, LPARAM) -> INT_PTR { return message == WM_INITDIALOG; }, 0);
            if (dialog != nullptr)
            {
                SendDlgItemMessageW(dialog, 100, WM_SETFONT, reinterpret_cast<WPARAM>(AliasedFont()), TRUE);
                SendDlgItemMessageW(dialog, 101, WM_SETFONT, reinterpret_cast<WPARAM>(AliasedFont()), TRUE);
                SetWindowPos(dialog, HWND_TOPMOST, 740, 80, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
            }
            return dialog;
        }

        POINT ClientToWindowPoint(HWND window, POINT client)
        {
            RECT rect{};
            GetWindowRect(window, &rect);
            MapWindowPoints(window, nullptr, &client, 1);
            return {client.x - rect.left, client.y - rect.top};
        }

        struct ModalState
        {
            Recorder* recorder;
            std::optional<COLORREF> background;
        };

        INT_PTR CALLBACK ModalProc(HWND dialog, UINT message, WPARAM, LPARAM lParam)
        {
            if (message == WM_INITDIALOG)
            {
                SetWindowLongPtrW(dialog, DWLP_USER, lParam);
                SetWindowPos(dialog, HWND_TOPMOST, 740, 320, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
                SetTimer(dialog, 1, 400, nullptr);
                return TRUE;
            }
            if (message == WM_TIMER)
            {
                KillTimer(dialog, 1);
                auto* state = reinterpret_cast<ModalState*>(GetWindowLongPtrW(dialog, DWLP_USER));
                RECT client{};
                GetClientRect(dialog, &client);
                state->background = Sample(*state->recorder, dialog, ClientToWindowPoint(dialog, {client.right - 10, client.bottom - 10}));
                EndDialog(dialog, 0);
                return TRUE;
            }
            return FALSE;
        }
    }

    int HiddenSysColourChanges()
    {
        return g_hiddenSysColourChanges;
    }

    Fixture CreateFixture(const wchar_t* title)
    {
        static const ATOM hiddenClass = [] {
            WNDCLASSW wc{};
            wc.lpfnWndProc = HiddenProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"TDPWHarnessHidden";
            return RegisterClassW(&wc);
        }();
        Fixture f;
        f.top = CreateTopLevel(title, 660, 360);
        f.list = ListWithScrollbar(f.top, 10, 10, 300, 160);
        f.tree = ShortTree(f.top, 330, 10, 300, 160);
        f.edit = CreateChild(L"EDIT", f.top, WS_BORDER | ES_AUTOHSCROLL, 10, 190, 300, 40, L"WWWWWW");
        f.button = CreateChild(L"BUTTON", f.top, BS_PUSHBUTTON, 330, 190, 140, 40, L"Button");
        f.label = CreateChild(L"STATIC", f.top, SS_LEFT, 10, 250, 300, 30, L"WWWWWW");
        f.dialog = OpenModeless(title);
        f.menuWindow = CreateTopLevel(L"menu", 320, 160);
        if (f.menuWindow != nullptr)
        {
            HMENU bar = CreateMenu();
            AppendMenuW(bar, MF_STRING, 1, L"File");
            AppendMenuW(bar, MF_STRING, 2, L"Edit");
            SetMenu(f.menuWindow, bar);
            SetWindowPos(f.menuWindow, HWND_TOPMOST, 60, 440, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
        }
        if (hiddenClass != 0)
        {
            f.hidden = CreateWindowExW(0, L"TDPWHarnessHidden", L"", WS_OVERLAPPED, 0, 0, 10, 10, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        }
        return f;
    }

    void DestroyFixture(Fixture& f)
    {
        for (HWND window : {f.dialog, f.menuWindow, f.hidden, f.top})
        {
            if (window != nullptr && IsWindow(window))
            {
                DestroyWindow(window);
            }
        }
        f = {};
    }

    Pixels SamplePixels(Recorder& r, const Fixture& f, COLORREF textHint)
    {
        Pixels p;
        RECT rect{};
        GetWindowRect(f.list, &rect);
        p.listBackground = Sample(r, f.list, {200, 60});
        if (const auto scroll = Sample(r, f.list, {rect.right - rect.left - 9, (rect.bottom - rect.top) * 2 / 3}))
        {
            p.scrollbarLuminance = RelativeLuminance(*scroll);
        }
        GetWindowRect(f.tree, &rect);
        p.treeBackground = Sample(r, f.tree, {(rect.right - rect.left) / 2, rect.bottom - rect.top - 20});
        RECT client{};
        GetClientRect(f.edit, &client);
        if (const auto cell = SampleText(r, f.edit, client, textHint))
        {
            p.editBackground = cell->background;
            p.editText = cell->text;
        }
        GetClientRect(f.label, &client);
        if (const auto cell = SampleText(r, f.label, client, textHint))
        {
            p.labelBackground = cell->background;
            p.labelText = cell->text;
        }
        GetClientRect(f.dialog, &client);
        p.dialogBackground = Sample(r, f.dialog, ClientToWindowPoint(f.dialog, {client.right - 10, client.bottom - 10}));
        return p;
    }

    std::optional<COLORREF> ModalDialogBackground(Recorder& r)
    {
        std::vector<WORD> tmpl = DialogTemplate(L"Modal", DS_MODALFRAME);
        ModalState state{&r, std::nullopt};
        const INT_PTR result = DialogBoxIndirectParamW(GetModuleHandleW(nullptr), reinterpret_cast<LPCDLGTEMPLATEW>(tmpl.data()), nullptr,
                                                       ModalProc, reinterpret_cast<LPARAM>(&state));
        if (result == -1)
        {
            r.Invalidate(L"DialogBoxIndirect failed (%lu)", GetLastError());
        }
        else if (!state.background)
        {
            r.Invalidate(L"modal dialog was not sampled");
        }
        return state.background;
    }

    void ExpectThemedPixels(Recorder& r, const Pixels& p, const SystemColourPalette& palette, const wchar_t* label)
    {
        auto expect = [&](const std::optional<COLORREF>& actual, int index, const wchar_t* what) {
            if (actual)
            {
                r.Expect(*actual == *palette.At(index), L"%s %s %06lX want %06lX", label, what, *actual, *palette.At(index));
            }
        };
        expect(p.listBackground, COLOR_WINDOW, L"list-view background");
        expect(p.editBackground, COLOR_WINDOW, L"edit background");
        expect(p.editText, COLOR_WINDOWTEXT, L"edit text");
        expect(p.labelBackground, COLOR_3DFACE, L"static background");
        expect(p.labelText, COLOR_WINDOWTEXT, L"static text");
        expect(p.dialogBackground, COLOR_3DFACE, L"dialog background");
        if (p.scrollbarLuminance && palette.isDark)
        {
            r.Expect(*p.scrollbarLuminance < 0.2, L"%s list-view scrollbar dark (%.3f)", label, *p.scrollbarLuminance);
        }
        expect(p.treeBackground, COLOR_WINDOW, L"tree-view background");
    }

    void ExpectSamePixels(Recorder& r, const Pixels& baseline, const Pixels& now, const wchar_t* label)
    {
        auto same = [&](const std::optional<COLORREF>& before, const std::optional<COLORREF>& after, const wchar_t* what) {
            if (before && after)
            {
                r.Expect(*before == *after, L"%s %s %06lX baseline %06lX", label, what, *after, *before);
            }
        };
        same(baseline.listBackground, now.listBackground, L"list-view background");
        same(baseline.editBackground, now.editBackground, L"edit background");
        same(baseline.editText, now.editText, L"edit text");
        same(baseline.labelBackground, now.labelBackground, L"static background");
        same(baseline.labelText, now.labelText, L"static text");
        same(baseline.dialogBackground, now.dialogBackground, L"dialog background");
        same(baseline.treeBackground, now.treeBackground, L"tree-view background");
        if (baseline.scrollbarLuminance && now.scrollbarLuminance)
        {
            r.Expect(std::abs(*baseline.scrollbarLuminance - *now.scrollbarLuminance) < 0.02, L"%s scrollbar luminance %.3f baseline %.3f", label,
                     *now.scrollbarLuminance, *baseline.scrollbarLuminance);
        }
    }

    bool TrackCreateMessages()
    {
        return SetWindowsHookExW(WH_CALLWNDPROC, BeforeMessage, nullptr, GetCurrentThreadId()) != nullptr &&
               SetWindowsHookExW(WH_CALLWNDPROCRET, AfterMessage, nullptr, GetCurrentThreadId()) != nullptr;
    }

    int CreateDepth()
    {
        return t_createDepth;
    }

    namespace
    {
        constexpr DWORD kRegionSentinel = 0x00FE01FE;

        LRESULT CALLBACK MdiFrameProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            if (message == WM_CREATE)
            {
                CLIENTCREATESTRUCT client{nullptr, 50000};
                HWND mdiClient = CreateWindowExW(0, L"MDICLIENT", nullptr, WS_CHILD | WS_CLIPCHILDREN | WS_VISIBLE, 0, 0, 0, 0, window, nullptr,
                                                 GetModuleHandleW(nullptr), &client);
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(mdiClient));
                return mdiClient != nullptr ? 0 : -1;
            }
            return DefFrameProcW(window, reinterpret_cast<HWND>(GetWindowLongPtrW(window, GWLP_USERDATA)), message, wParam, lParam);
        }

        constexpr wchar_t kMdiFrameClass[] = L"TDPWThemesMdiFrame";
        constexpr wchar_t kMdiChildClass[] = L"TDPWThemesMdiChild";

        bool RegisterMdiClasses()
        {
            static const bool registered = [] {
                HINSTANCE instance = GetModuleHandleW(nullptr);
                WNDCLASSEXW frameClass{sizeof(frameClass)};
                frameClass.lpfnWndProc = MdiFrameProc;
                frameClass.hInstance = instance;
                frameClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
                frameClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_APPWORKSPACE + 1);
                frameClass.lpszClassName = kMdiFrameClass;
                WNDCLASSEXW childClass{sizeof(childClass)};
                childClass.lpfnWndProc = DefMDIChildProcW;
                childClass.hInstance = instance;
                childClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
                childClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
                childClass.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
                childClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
                childClass.lpszClassName = kMdiChildClass;
                return RegisterClassExW(&frameClass) != 0 && RegisterClassExW(&childClass) != 0;
            }();
            return registered;
        }
    }

    std::optional<std::map<COLORREF, int>> CaptureRegion(Recorder& r, HWND window, const RECT& area)
    {
        RECT rect{};
        GetWindowRect(window, &rect);
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        if (area.left < 0 || area.top < 0 || area.right > width || area.bottom > height || area.right <= area.left || area.bottom <= area.top)
        {
            r.Invalidate(L"capture region outside the window");
            return std::nullopt;
        }
        for (int attempt = 1; attempt <= 3; ++attempt)
        {
            std::vector<DWORD> captures[2];
            bool printed = true;
            for (auto& pixels : captures)
            {
                DwmFlush();
                BITMAPINFO info{};
                info.bmiHeader.biSize = sizeof(info.bmiHeader);
                info.bmiHeader.biWidth = width;
                info.bmiHeader.biHeight = -height;
                info.bmiHeader.biPlanes = 1;
                info.bmiHeader.biBitCount = 32;
                void* bits = nullptr;
                HDC screen = GetDC(nullptr);
                HDC memory = CreateCompatibleDC(screen);
                HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
                ReleaseDC(nullptr, screen);
                if (memory == nullptr || bitmap == nullptr)
                {
                    r.Invalidate(L"capture buffer could not be created");
                    return std::nullopt;
                }
                DWORD* raw = static_cast<DWORD*>(bits);
                std::fill(raw, raw + static_cast<std::size_t>(width) * static_cast<std::size_t>(height), kRegionSentinel);
                HGDIOBJ previous = SelectObject(memory, bitmap);
                printed &= PrintWindow(window, memory, PW_RENDERFULLCONTENT) != FALSE;
                GdiFlush();
                pixels.assign(raw, raw + static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
                SelectObject(memory, previous);
                DeleteObject(bitmap);
                DeleteDC(memory);
            }
            if (!printed)
            {
                r.Invalidate(L"PrintWindow failed");
                return std::nullopt;
            }
            bool stable = true;
            std::map<COLORREF, int> histogram;
            for (int y = area.top; stable && y < area.bottom; ++y)
            {
                for (int x = area.left; x < area.right; ++x)
                {
                    const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
                    if (captures[0][i] == kRegionSentinel || captures[0][i] != captures[1][i])
                    {
                        stable = false;
                        break;
                    }
                    const DWORD p = captures[0][i];
                    ++histogram[RGB((p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF)];
                }
            }
            if (stable)
            {
                return histogram;
            }
            Pump(100);
        }
        r.Invalidate(L"region capture undrawn or unstable in 3 capture pairs");
        return std::nullopt;
    }

    COLORREF MostCommon(const std::map<COLORREF, int>& histogram)
    {
        COLORREF colour = 0;
        int most = 0;
        for (const auto& [c, n] : histogram)
        {
            if (n > most)
            {
                most = n;
                colour = c;
            }
        }
        return colour;
    }

    int Distance(COLORREF a, COLORREF b)
    {
        return std::abs(GetRValue(a) - GetRValue(b)) + std::abs(GetGValue(a) - GetGValue(b)) + std::abs(GetBValue(a) - GetBValue(b));
    }

    RECT ToWindow(HWND window, RECT screen)
    {
        RECT rect{};
        GetWindowRect(window, &rect);
        OffsetRect(&screen, -rect.left, -rect.top);
        return screen;
    }

    MdiFrame CreateMdiFrame(const wchar_t* title, HMENU menu, int width, int height)
    {
        MdiFrame mdi;
        if (!RegisterMdiClasses())
        {
            return mdi;
        }
        mdi.frame = CreateWindowExW(0, kMdiFrameClass, title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE, 40, 40, width, height, nullptr, menu,
                                    GetModuleHandleW(nullptr), nullptr);
        mdi.client = mdi.frame != nullptr ? reinterpret_cast<HWND>(GetWindowLongPtrW(mdi.frame, GWLP_USERDATA)) : nullptr;
        return mdi;
    }

    HWND CreateMdiChild(const MdiFrame& mdi, const wchar_t* title, DWORD style, int x, int y, int width, int height)
    {
        MDICREATESTRUCTW create{};
        create.szClass = kMdiChildClass;
        create.szTitle = title;
        create.hOwner = GetModuleHandleW(nullptr);
        create.x = x;
        create.y = y;
        create.cx = width;
        create.cy = height;
        create.style = style;
        return reinterpret_cast<HWND>(SendMessageW(mdi.client, WM_MDICREATE, 0, reinterpret_cast<LPARAM>(&create)));
    }

    void WriteCaptureBitmap(const CheckContext& context, const wchar_t* fileName, HWND window)
    {
        RECT rect{};
        if (context.evidenceDir.empty() || !GetWindowRect(window, &rect))
        {
            return;
        }
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HDC screen = GetDC(nullptr);
        HDC memory = CreateCompatibleDC(screen);
        HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, screen);
        if (memory != nullptr && bitmap != nullptr)
        {
            HGDIOBJ previous = SelectObject(memory, bitmap);
            DwmFlush();
            PrintWindow(window, memory, PW_RENDERFULLCONTENT);
            GdiFlush();
            const DWORD imageBytes = static_cast<DWORD>(width) * static_cast<DWORD>(height) * 4;
            BITMAPFILEHEADER header{};
            header.bfType = 0x4D42;
            header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
            header.bfSize = header.bfOffBits + imageBytes;
            const std::wstring path = context.evidenceDir + L"\\" + fileName;
            HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE)
            {
                DWORD written = 0;
                WriteFile(file, &header, sizeof(header), &written, nullptr);
                WriteFile(file, &info.bmiHeader, sizeof(info.bmiHeader), &written, nullptr);
                WriteFile(file, bits, imageBytes, &written, nullptr);
                CloseHandle(file);
            }
            SelectObject(memory, previous);
        }
        if (bitmap != nullptr)
        {
            DeleteObject(bitmap);
        }
        if (memory != nullptr)
        {
            DeleteDC(memory);
        }
    }
}
