#include "HtmlReports.h"
#include "Shared/ActivePalette.h"
#include <atlbase.h>
#include <mshtml.h>
#include <oleacc.h>
#include <commctrl.h>
#include <cwchar>
#include <string>
#pragma comment(lib, "oleacc.lib")
#pragma comment(lib, "oleaut32.lib")

namespace
{
    constexpr UINT_PTR kReportSubclass = 0x54444854;
    constexpr UINT_PTR kReportTimer = 0x54444855;
    constexpr wchar_t kStyleId[] = L"ThirdDigitalProjectWiseTheme";

    bool IsAuditReport(HWND window) noexcept
    {
        wchar_t name[64]{};
        if (!GetClassNameW(window, name, ARRAYSIZE(name)) || wcscmp(name, L"Internet Explorer_Server") != 0) return false;
        for (HWND parent = GetParent(window); parent != nullptr; parent = GetParent(parent))
        {
            GetClassNameW(parent, name, ARRAYSIZE(name));
            if (wcscmp(name, L"#32770") == 0)
            {
                wchar_t caption[80]{};
                GetWindowTextW(parent, caption, ARRAYSIZE(caption));
                return wcscmp(caption, L"@Audit Trail") == 0;
            }
        }
        return false;
    }
    std::wstring Hex(COLORREF colour)
    {
        wchar_t text[8]{};
        swprintf_s(text, L"#%02x%02x%02x", GetRValue(colour), GetGValue(colour), GetBValue(colour));
        return text;
    }

    // No scripts or report data are changed. A screen-only stylesheet leaves printing alone;
    // removing this single node restores all of the report's original CSS on None.
    bool UpdateDocument(HWND window)
    {
        static const UINT getObject = RegisterWindowMessageW(L"WM_HTML_GETOBJECT");
        DWORD_PTR result = 0;
        if (!SendMessageTimeoutW(window, getObject, 0, 0, SMTO_ABORTIFHUNG, 200, &result) || result == 0) return false;
        CComPtr<IHTMLDocument2> document;
        if (FAILED(ObjectFromLresult(static_cast<LRESULT>(result), __uuidof(IHTMLDocument2), 0, reinterpret_cast<void**>(&document))) || !document) return false;
        CComBSTR state;
        if (FAILED(document->get_readyState(&state)) || !state || wcscmp(state, L"complete") != 0) return false;
        CComQIPtr<IHTMLDocument3> document3(document);
        if (!document3) return true;
        CComPtr<IHTMLElement> element;
        document3->getElementById(CComBSTR(kStyleId), &element);
        const auto* palette = ActivePalette();
        if (palette == nullptr)
        {
            if (element)
            {
                CComQIPtr<IHTMLDOMNode> node(element);
                CComPtr<IHTMLDOMNode> parent, removed;
                if (node && SUCCEEDED(node->get_parentNode(&parent)) && parent) parent->removeChild(node, &removed);
            }
            return true;
        }
        if (!element)
        {
            if (FAILED(document->createElement(CComBSTR(L"style"), &element)) || !element) return true;
            element->put_id(CComBSTR(kStyleId));
            CComPtr<IHTMLElement> root;
            if (FAILED(document3->get_documentElement(&root)) || !root) return true;
            CComQIPtr<IHTMLDOMNode> rootNode(root), styleNode(element);
            CComPtr<IHTMLDOMNode> appended;
            if (!rootNode || !styleNode || FAILED(rootNode->appendChild(styleNode, &appended))) return true;
        }
        CComQIPtr<IHTMLStyleElement> style(element);
        CComPtr<IHTMLStyleSheet> sheet;
        if (!style || FAILED(style->get_styleSheet(&sheet)) || !sheet) return true;
        style->put_media(CComBSTR(L"screen"));
        const auto background = Hex(*palette->palette.At(COLOR_WINDOW));
        const auto foreground = Hex(*palette->palette.At(COLOR_WINDOWTEXT));
        const auto panel = Hex(*palette->palette.At(COLOR_3DFACE));
        const auto border = Hex(*palette->palette.At(COLOR_3DSHADOW));
        const auto link = Hex(*palette->palette.At(COLOR_HOTLIGHT));
        const std::wstring css =
            L"html,body,table,tbody,tr,td,div,p,span,font{background-color:" + background +
            L"!important;color:" + foreground + L"!important;}"
            L"th,thead,caption{background-color:" + panel + L"!important;color:" + foreground + L"!important;}"
            L"table,td,th{border-color:" + border + L"!important;}"
            L"a,a:visited{color:" + link + L"!important;}"
            L"html,body{scrollbar-face-color:" + panel + L";scrollbar-track-color:" + background +
            L";scrollbar-arrow-color:" + foreground + L";scrollbar-shadow-color:" + border +
            L";scrollbar-highlight-color:" + border + L";scrollbar-3dlight-color:" + panel + L";scrollbar-darkshadow-color:" + border + L";}";
        CComVariant previous;
        const CComBSTR marker(L"data-tdpw-palette");
        if (SUCCEEDED(element->getAttribute(marker, 0, &previous)) && previous.vt == VT_BSTR && previous.bstrVal != nullptr && css == previous.bstrVal) return true;
        if (SUCCEEDED(sheet->put_cssText(CComBSTR(css.c_str())))) element->setAttribute(marker, CComVariant(css.c_str()), 0);
        return true;
    }
    void CALLBACK Update(HWND window, UINT, UINT_PTR, DWORD) noexcept
    {
        KillTimer(window, kReportTimer);
        if (!IsAuditReport(window)) return;
        try
        {
            if (!UpdateDocument(window)) SetTimer(window, kReportTimer, 250, Update);
        }
        catch (...) {}
    }
    LRESULT CALLBACK ReportProc(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR)
    {
        if (message == WM_NCDESTROY)
        {
            KillTimer(window, kReportTimer);
            RemoveWindowSubclass(window, ReportProc, id);
        }
        const LRESULT result = DefSubclassProc(window, message, w, l);
        if (message == WM_PAINT) SetTimer(window, kReportTimer, 150, Update);
        return result;
    }
}

void ApplyHtmlReportColours(HWND window) noexcept
{
    if (IsAuditReport(window) && SetWindowSubclass(window, ReportProc, kReportSubclass, 0))
        SetTimer(window, kReportTimer, 150, Update);
}
