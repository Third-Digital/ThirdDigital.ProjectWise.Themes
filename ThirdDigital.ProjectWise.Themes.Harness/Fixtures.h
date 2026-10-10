#pragma once

#include <windows.h>
#include <uxtheme.h>

#include "Domain/SystemColourMapping.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Harness
{
    enum ExitCode : int
    {
        Passed = 0,
        Failed = 1,
        Invalid = 2,
    };

    struct CheckContext
    {
        std::wstring scheme;
        std::wstring dllPath;
        std::wstring bridgePath;
        std::wstring evidenceDir;
    };

    using CheckFunction = int (*)(const CheckContext&);

    enum class Host
    {
        Self,
        Pwc,
    };

    struct CheckDefinition
    {
        const wchar_t* group;
        const wchar_t* name;
        const wchar_t* mode;
        Host host;
        bool perScheme;
        CheckFunction run;
    };

    std::vector<CheckDefinition>& Checks();

    struct CheckRegistration
    {
        explicit CheckRegistration(const CheckDefinition& definition);
    };

    // Failed means the behaviour is wrong; Invalid means the evidence could not be gathered
    // (for example a capture failed), which must never be reported as either pass or fail.
    class Recorder
    {
    public:
        explicit Recorder(const wchar_t* checkName);
        bool Expect(bool condition, const wchar_t* format, ...);
        void Invalidate(const wchar_t* format, ...);
        void Note(const wchar_t* format, ...);
        int Result() const;
        bool Clean() const;

    private:
        const wchar_t* name_;
        int failures_ = 0;
        int invalid_ = 0;
    };

    std::wstring SettingsKey();
    void DeleteSettingsKey();

    std::optional<COLORREF> BrushColour(HBRUSH brush);

    // A theme handle on a hidden window owned by the calling thread; kept open for the process.
    HTHEME WindowTheme();

    // Every API result that the colour hooks can affect, for the COLOR_* range, values around it and the TMT_* range.
    struct ApiSnapshot
    {
        std::vector<int> indices;
        std::vector<DWORD> sysColour;
        std::vector<std::optional<COLORREF>> sysBrush;
        std::vector<COLORREF> themeColour;
        std::vector<COLORREF> nullThemeColour;
        std::vector<std::optional<COLORREF>> themeBrush;
        std::vector<std::optional<COLORREF>> nullThemeBrush;
    };

    ApiSnapshot TakeSnapshot(HTHEME theme);

    // Every result equals `baseline`, except mapped COLOR_* indices when `palette` is given, which must equal it.
    void ExpectSnapshot(Recorder& r, const ApiSnapshot& baseline, const ApiSnapshot& now, const SystemColourPalette* palette, const wchar_t* label);

    void WriteEvidence(const CheckContext& context, const wchar_t* fileName, const std::wstring& text);
    // A PrintWindow (PW_RENDERFULLCONTENT) capture of `window`, as a .bmp in the evidence folder.
    void WriteCaptureBitmap(const CheckContext& context, const wchar_t* fileName, HWND window);

    void Pump(DWORD milliseconds);

    // A visible top-level test window (class background COLOR_3DFACE + 1, default window procedure).
    HWND CreateTopLevel(const wchar_t* title, int width = 520, int height = 420);
    HWND CreateChild(const wchar_t* className, HWND parent, DWORD style, int x, int y, int width, int height, const wchar_t* text = L"");
    HFONT AliasedFont();
    HWND ListWithScrollbar(HWND parent, int x, int y, int width = 230, int height = 150);
    HWND ShortTree(HWND parent, int x, int y, int width = 230, int height = 150);

    // An in-memory dialog template with one static text control (id 100) and one edit control (id 101).
    std::vector<WORD> DialogTemplate(const wchar_t* title, DWORD style);

    struct Image
    {
        int width = 0;
        int height = 0;
        std::vector<DWORD> pixels;
        COLORREF At(int x, int y) const;
    };

    // PrintWindow of the client area of a top-level window into a buffer pre-filled with a sentinel.
    bool Capture(HWND topLevel, Image& image);

    // Samples a point given in `target` window coordinates (non-client included) through two captures of its
    // top-level window. Returns nullopt, after invalidating the recorder, if either capture fails, the
    // point lies outside the capture, the pixel still holds the sentinel, or the two captures differ.
    std::optional<COLORREF> Sample(Recorder& r, HWND target, POINT windowPoint);

    // Text pixels (closest to `text`) and background (most common) of a client rectangle of `target`.
    struct TextCell
    {
        COLORREF background = 0;
        COLORREF text = 0;
        int textPixels = 0;
    };
    std::optional<TextCell> SampleText(Recorder& r, HWND target, const RECT& clientRect, COLORREF expectedText);

    struct RecordedCall
    {
        enum class Kind
        {
            SetWindowTheme,
            SetWindowSubclass,
            RemoveWindowSubclass,
            DwmSetWindowAttribute,
            SendMessageTimeout,
            SendNotifyMessage,
            RedrawWindow,
        };
        Kind kind;
        HWND window;
        DWORD attribute;
        DWORD value;
        std::wstring text;
        bool duringCreate = false;
    };

    // Redirects `target` in the import address table of `module` (all descriptors) to `replacement`.
    bool PatchImports(HMODULE module, void* target, void* replacement);

    // Records the window-modifying and refresh calls that `module` (default: this executable) makes,
    // by patching its import table, so it is independent of MinHook.
    bool InstallCallRecorder(HMODULE module = nullptr);
    std::vector<RecordedCall> RecordedCalls(HWND window);
    void ClearRecordedCalls();
    void FailDwmCalls(bool fail);

    // Counts WM_NCCREATE/WM_CREATE being processed on this thread (WH_CALLWNDPROC and WH_CALLWNDPROCRET), so
    // recorded calls can show whether they ran with a window still inside its own creation.
    bool TrackCreateMessages();
    int CreateDepth();

    constexpr ULONG kExplorerMask = 0x00000001;      // AAMODULE_EXPLORER
    constexpr ULONG kAdministratorMask = 0x00000008; // AAMODULE_ADMINISTRATOR

    struct ProductionModule
    {
        HMODULE handle = nullptr;
        LONG(WINAPI* customInitialize)(ULONG, LPVOID) = nullptr;
    };

    bool LoadProductionModule(Recorder& r, const CheckContext& context, ProductionModule& module);

    // Lines of %TEMP%\ThirdDigital\projectwise-themes.log containing `text`.
    int LogLines(const wchar_t* text);

    // Windows covering list-view, tree-view, edit, button, static, a modeless dialog, a menu bar and a hidden
    // top-level window that counts WM_SYSCOLORCHANGE.
    struct Fixture
    {
        HWND top = nullptr;
        HWND list = nullptr;
        HWND tree = nullptr;
        HWND edit = nullptr;
        HWND button = nullptr;
        HWND label = nullptr;
        HWND dialog = nullptr;
        HWND menuWindow = nullptr;
        HWND hidden = nullptr;
    };

    Fixture CreateFixture(const wchar_t* title);
    void DestroyFixture(Fixture& fixture);
    int HiddenSysColourChanges();

    struct Pixels
    {
        std::optional<COLORREF> listBackground;
        std::optional<COLORREF> editBackground;
        std::optional<COLORREF> editText;
        std::optional<COLORREF> labelBackground;
        std::optional<COLORREF> labelText;
        std::optional<COLORREF> dialogBackground;
        std::optional<COLORREF> treeBackground;
        std::optional<double> scrollbarLuminance;
    };

    Pixels SamplePixels(Recorder& r, const Fixture& fixture, COLORREF textHint);

    // Opens DialogBoxIndirect, samples its background from a timer inside the modal loop, then closes it.
    std::optional<COLORREF> ModalDialogBackground(Recorder& r);

    void ExpectThemedPixels(Recorder& r, const Pixels& pixels, const SystemColourPalette& palette, const wchar_t* label);
    void ExpectSamePixels(Recorder& r, const Pixels& baseline, const Pixels& now, const wchar_t* label);

    // Whole-window capture (non-client included) of `area`, in window coordinates, from two matching
    // PrintWindow captures; nullopt after invalidating the recorder otherwise.
    std::optional<std::map<COLORREF, int>> CaptureRegion(Recorder& r, HWND window, const RECT& area);
    COLORREF MostCommon(const std::map<COLORREF, int>& histogram);
    int Distance(COLORREF a, COLORREF b);
    RECT ToWindow(HWND window, RECT screen);

    struct MdiFrame
    {
        HWND frame = nullptr;
        HWND client = nullptr;
    };

    // A visible MDI frame with an MDIClient. Its children have a class with a small icon.
    MdiFrame CreateMdiFrame(const wchar_t* title, HMENU menu, int width, int height);
    HWND CreateMdiChild(const MdiFrame& mdi, const wchar_t* title, DWORD style, int x, int y, int width, int height);
}
