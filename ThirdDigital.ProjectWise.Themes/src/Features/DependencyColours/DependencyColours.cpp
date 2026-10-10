#include "DependencyColours.h"
#include "Shared/ActivePalette.h"
#include "Shared/Log.h"
#include <objidl.h>
#include <gdiplus.h>
#include <commctrl.h>
#include <MinHook.h>
#include <cwchar>
#include <iterator>
#include <cstdlib>
#include <initializer_list>
#pragma comment(lib, "gdiplus.lib")
using namespace Gdiplus;
using namespace Gdiplus::DllExports;
namespace {
constexpr wchar_t kMarker[] = L"ThirdDigital.ProjectWise.Themes.Dependency";
constexpr UINT_PTR kSubclass = 0x54444450;
thread_local bool t_paint = false;
thread_local bool t_busy = false;
thread_local bool t_chrome = false;
bool g_ready = false;
bool g_attempted = false;
struct DrawingGuard { bool previous=t_busy; DrawingGuard(){t_busy=true;} ~DrawingGuard(){t_busy=previous;} };
ARGB Map(ARGB c, bool text = false) noexcept {
    const auto* p = ActivePalette(); if (!p) return c;
    if (!text) for (int role : {COLOR_WINDOW, COLOR_3DFACE, COLOR_3DSHADOW, COLOR_HIGHLIGHT}) {
        const COLORREF v=*p->palette.At(role); const ARGB rgb=(static_cast<ARGB>(GetRValue(v))<<16)|(static_cast<ARGB>(GetGValue(v))<<8)|GetBValue(v);
        if ((c&0xffffff)==rgb) return c;
    }
    const BYTE r = static_cast<BYTE>(c >> 16), g = static_cast<BYTE>(c >> 8), b = static_cast<BYTE>(c);
    // Preserve semantic colours; neutral backgrounds and outlines follow the palette.
    const bool neutral = abs(int(r)-int(g)) <= 3 && abs(int(g)-int(b)) <= 3 && abs(int(r)-int(b)) <= 3;
    if (!neutral && !(text && t_chrome)) return c;
    const int role = text ? COLOR_WINDOWTEXT : r >= 250 ? COLOR_WINDOW : r >= 160 ? COLOR_3DFACE : COLOR_3DSHADOW;
    COLORREF v = *p->palette.At(role);
    return (c & 0xff000000) | (static_cast<ARGB>(GetRValue(v)) << 16) | (static_cast<ARGB>(GetGValue(v)) << 8) | GetBValue(v);
}
bool CanDraw(GpGraphics* graphics) noexcept {
    if (!t_paint || t_busy || !ActivePalette() || !graphics) return false;
    HDC dc = nullptr; if (GdipGetDC(graphics, &dc) != Ok) return false;
    const DWORD type = GetObjectType(dc);
    const bool display = type != OBJ_METADC && type != OBJ_ENHMETADC && GetDeviceCaps(dc, TECHNOLOGY) == DT_RASDISPLAY;
    GdipReleaseDC(graphics, dc); return display;
}
class Brush {
public:
    GpBrush* value;
    Brush(GpBrush* input, bool text = false) : value(input) {
        GpBrushType type{}; if (!input || GdipGetBrushType(input, &type) != Ok) return;
        ARGB c{};
        if (type == BrushTypeSolidColor && GdipGetSolidFillColor(reinterpret_cast<GpSolidFill*>(input), &c) == Ok && Map(c, text) != c) {
            GpSolidFill* fill = nullptr;
            if (GdipCreateSolidFill(Map(c, text), &fill) == Ok) value = owned_ = fill;
        } else if (type == BrushTypeLinearGradient) {
            ARGB colors[2]{}; auto* line = reinterpret_cast<GpLineGradient*>(input);
            if (GdipGetLineColors(line, colors) != Ok) return;
            // DotNetBar's Office2007 pale blue gradients are chrome, not graph status colours.
            auto chrome = [](ARGB color) { BYTE r=static_cast<BYTE>(color>>16), g=static_cast<BYTE>(color>>8), b=static_cast<BYTE>(color); return r>=100 && b>=g && g>=r && b-r>=15; };
            const bool office = t_chrome && (chrome(colors[0]) || chrome(colors[1]));
            ARGB mapped[2] = {Map(colors[0],text),Map(colors[1],text)};
            if (office) { mapped[0]=Map((colors[0]&0xff000000)|0xe0e0e0); mapped[1]=Map((colors[1]&0xff000000)|0xeeeeee); }
            if (mapped[0]==colors[0] && mapped[1]==colors[1]) return;
            if (GdipCloneBrush(input, &owned_) == Ok) {
                value=owned_; auto* clone=reinterpret_cast<GpLineGradient*>(owned_);
                GdipSetLineColors(clone,mapped[0],mapped[1]);
                INT count=0; ARGB stops[32]{}; REAL positions[32]{};
                if(GdipGetLinePresetBlendCount(line,&count)==Ok && count>0 && count<=32 && GdipGetLinePresetBlend(line,stops,positions,count)==Ok) {
                    for(INT i=0;i<count;++i) stops[i]=office ? Map((stops[i]&0xff000000)|0xe0e0e0) : Map(stops[i],text);
                    GdipSetLinePresetBlend(clone,stops,positions,count);
                }
            }
        }
    }
    ~Brush(){if(owned_) GdipDeleteBrush(owned_);}
private: GpBrush* owned_ = nullptr;
};
class Pen {
public:
    GpPen* value;
    Pen(GpPen* input) : value(input) {
        GpPenType type{}; ARGB c{};
        if(input && GdipGetPenFillType(input,&type)==Ok && type==PenTypeSolidColor && GdipGetPenColor(input,&c)==Ok && Map(c,true)!=c && GdipClonePen(input,&owned_)==Ok) {
            value=owned_; GdipSetPenColor(value,Map(c,true));
        }
    }
    ~Pen(){if(owned_) GdipDeletePen(owned_);}
private: GpPen* owned_=nullptr;
};
// Signatures are derived from the Windows SDK. Only temporary drawing objects are substituted.
template<auto Api> struct FillHook;
template<class... Args, GpStatus (WINGDIPAPI *Api)(GpGraphics*,GpBrush*,Args...)>
struct FillHook<Api> {
    static inline decltype(Api) original = nullptr;
    static GpStatus WINGDIPAPI Draw(GpGraphics* graphics,GpBrush* brush,Args... args) {
        if(!CanDraw(graphics)) return original(graphics,brush,args...);
        DrawingGuard guard; Brush mapped(brush); return original(graphics,mapped.value,args...);
    }
};
template<auto Api> struct PenHook;
template<class... Args, GpStatus (WINGDIPAPI *Api)(GpGraphics*,GpPen*,Args...)>
struct PenHook<Api> {
    static inline decltype(Api) original = nullptr;
    static GpStatus WINGDIPAPI Draw(GpGraphics* graphics,GpPen* pen,Args... args) {
        if(!CanDraw(graphics)) return original(graphics,pen,args...);
        DrawingGuard guard; Pen mapped(pen); return original(graphics,mapped.value,args...);
    }
};
decltype(&GdipGraphicsClear) g_clear=nullptr;
GpStatus WINGDIPAPI Clear(GpGraphics* g,ARGB c) { return g_clear(g,CanDraw(g)?Map(c):c); }
decltype(&GdipDrawString) g_string=nullptr;
GpStatus WINGDIPAPI String(GpGraphics* g,GDIPCONST WCHAR* text,INT count,GDIPCONST GpFont* font,GDIPCONST RectF* bounds,GDIPCONST GpStringFormat* format,GDIPCONST GpBrush* brush) {
    if(!CanDraw(g)) return g_string(g,text,count,font,bounds,format,brush);
    DrawingGuard guard; Brush mapped(const_cast<GpBrush*>(brush),true); return g_string(g,text,count,font,bounds,format,mapped.value);
}
bool Install() noexcept {
    if(g_ready) return true; if(g_attempted) return false; g_attempted=true;
    struct Spec { const char* name; void* draw; void** original; };
#define FILL(name) {#name,reinterpret_cast<void*>(FillHook<&name>::Draw),reinterpret_cast<void**>(&FillHook<&name>::original)}
#define PEN(name) {#name,reinterpret_cast<void*>(PenHook<&name>::Draw),reinterpret_cast<void**>(&PenHook<&name>::original)}
    const Spec specs[]={
        FILL(GdipFillRectangle), FILL(GdipFillRectangleI), FILL(GdipFillRectangles), FILL(GdipFillRectanglesI),
        FILL(GdipFillPath), FILL(GdipFillPolygon), FILL(GdipFillPolygonI), FILL(GdipFillPolygon2), FILL(GdipFillPolygon2I),
        FILL(GdipFillEllipse), FILL(GdipFillEllipseI), FILL(GdipFillRegion),
        PEN(GdipDrawPath), PEN(GdipDrawLine), PEN(GdipDrawLineI), PEN(GdipDrawLines), PEN(GdipDrawLinesI),
        PEN(GdipDrawRectangle), PEN(GdipDrawRectangleI), PEN(GdipDrawEllipse), PEN(GdipDrawEllipseI),
        {"GdipGraphicsClear",reinterpret_cast<void*>(Clear),reinterpret_cast<void**>(&g_clear)},
        {"GdipDrawString",reinterpret_cast<void*>(String),reinterpret_cast<void**>(&g_string)}
    };
#undef FILL
#undef PEN
    void* targets[std::size(specs)]{};
    for(size_t i=0;i<std::size(specs);++i) {
        const auto& s=specs[i];
        if(MH_CreateHookApiEx(L"gdiplus.dll",s.name,s.draw,s.original,&targets[i])!=MH_OK) {
            for(size_t j=0;j<i;++j) MH_RemoveHook(targets[j]);
            Log(L"dependency colours: could not prepare %S",s.name); return false;
        }
    }
    for(void* target:targets) if(MH_EnableHook(target)!=MH_OK) {
        for(void* other:targets) MH_DisableHook(other); return false;
    }
    g_ready=true; Log(L"dependency colours: GDI+ drawing hooks installed");return true;
}
LRESULT CALLBACK Paint(HWND window,UINT msg,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR) {
    if(msg==WM_NCDESTROY) RemoveWindowSubclass(window,Paint,id);
    if(msg!=WM_PAINT && msg!=WM_PRINTCLIENT && msg!=WM_PRINT && msg!=WM_ERASEBKGND) return DefSubclassProc(window,msg,w,l);
    const bool previous=t_paint, previousChrome=t_chrome;
    wchar_t title[64]{}; GetWindowTextW(window,title,ARRAYSIZE(title));
    unsigned managedDepth=0; wchar_t name[96]{};
    for(HWND current=window;current;current=GetParent(current)) {
        GetClassNameW(current,name,ARRAYSIZE(name));
        if(wcsncmp(name,L"WindowsForms",12)!=0) break;
        ++managedDepth;
    }
    t_chrome=wcscmp(title,L"tabStrip1")==0 || managedDepth<=2;
    t_paint=msg==WM_PAINT || (GetDeviceCaps(reinterpret_cast<HDC>(w),TECHNOLOGY)==DT_RASDISPLAY && GetObjectType(reinterpret_cast<HDC>(w))!=OBJ_ENHMETADC);
    __try {return DefSubclassProc(window,msg,w,l);}
    __finally {t_paint=previous;t_chrome=previousChrome;}
}
}
bool IsDependencyColourWindow(HWND window) noexcept {
    wchar_t name[96]{};
    for(HWND current=window;current;current=GetParent(current)) {
        if(GetPropW(current,kMarker) || (GetClassNameW(current,name,ARRAYSIZE(name)) && wcscmp(name,L"DMStudioContainerWndClass")==0)) {
            // The managed viewer is reparented into PWPreviewControlWindowView after creation.
            SetPropW(window,kMarker,reinterpret_cast<HANDLE>(1)); return true;
        }
    }

    // When theming is first enabled after the pane has been opened, the managed root
    // has already left DMStudioContainerWndClass. Identify that exact hosted layout.
    HWND root=window;
    for(HWND parent=GetParent(root);parent;parent=GetParent(root)) {
        GetClassNameW(parent,name,ARRAYSIZE(name));
        if(wcsncmp(name,L"WindowsForms",12)!=0) break;
        root=parent;
    }
    wchar_t caption[96]{};GetClassNameW(root,name,ARRAYSIZE(name));
    GetWindowTextW(GetParent(root),caption,ARRAYSIZE(caption));
    if(wcsncmp(name,L"WindowsForms",12)==0 && wcscmp(caption,L"PWPreviewControlWindowView")==0) {
        unsigned parts=0;
        for(HWND child=GetWindow(root,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
            GetWindowTextW(child,caption,ARRAYSIZE(caption));
            if(wcscmp(caption,L"TabControl1")==0) parts|=1;
            if(wcscmp(caption,L"ToolStrip1")==0) parts|=2;
            if(wcscmp(caption,L"Properties")==0) parts|=4;
            if(wcscmp(caption,L"Tools")==0) parts|=8;
        }
        if(parts==15) {SetPropW(root,kMarker,reinterpret_cast<HANDLE>(1));return true;}
    }
    return false;
}
void ApplyDependencyColours(HWND window) noexcept {
    if(IsDependencyColourWindow(window) && Install()) SetWindowSubclass(window,Paint,kSubclass,0);
}
