#ifndef __INC_GL_COMMDLG_EXTENT_
#define __INC_GL_COMMDLG_EXTENT_

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <Shlobj.h>
#include <cstdint>
#include <thread>
#include <memory>
#include <functional>
#include <atomic>
#include <condition_variable>
#include <queue>
#include <mutex>
#include <cmath>
#include <uxtheme.h>
#include <wingdi.h>
#include <dwmapi.h>
#include "UTF8toWide.hpp"

namespace GLDLG
{

#pragma region 非Win32原生对话框

    struct ColorRGBA
    {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;

        constexpr ColorRGBA() : r(255), g(255), b(255), a(255) {}
        constexpr ColorRGBA(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255)
            : r(r_), g(g_), b(b_), a(a_) {}

        COLORREF ToCOLORREF() const
        {
            return RGB(r, g, b);
        }
    };

    struct Theme
    {
        ColorRGBA Text;
        ColorRGBA ControlFrame;
        ColorRGBA PrimaryBackground;
        ColorRGBA SecondaryBackground;
        ColorRGBA PrimaryForeground;
        ColorRGBA SecondaryForeground;
    };

    inline Theme theme = {
        // Text: #E6E6EF
        ColorRGBA(230, 230, 239),
        // ControlFrame: #383842
        ColorRGBA(56, 56, 66),
        // PrimaryBackground: #1A1A1E
        ColorRGBA(26, 26, 30),
        // SecondaryBackground: #27272E
        ColorRGBA(39, 39, 46),
        // PrimaryForeground: #424949
        ColorRGBA(66, 73, 73),
        // SecondaryForeground: #485466
        ColorRGBA(72, 84, 102)};

    inline const Theme &GetTheme() { return theme; }
    inline void SetTheme(const Theme &t) { theme = t; }

    namespace Controls
    {
        void InitWindowColor(HWND hWnd)
        {
            COLORREF captionBgr = theme.PrimaryBackground.ToCOLORREF();
            DwmSetWindowAttribute(
                hWnd,
                DWMWA_CAPTION_COLOR,
                &captionBgr,
                sizeof(COLORREF));

            COLORREF captionText = theme.Text.ToCOLORREF();
            DwmSetWindowAttribute(
                hWnd,
                DWMWA_TEXT_COLOR,
                &captionText,
                sizeof(COLORREF));
            BOOL darkMode = TRUE;
            DwmSetWindowAttribute(
                hWnd,
                DWMWA_USE_IMMERSIVE_DARK_MODE,
                &darkMode,
                sizeof(BOOL));

            SetWindowPos(hWnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }

        enum class CtrlState
        {
            Normal,
            Hover,
            Pressed,
            Disabled
        };
        namespace CtrlDraw
        {

            void DrawRoundFrame(HDC hdc, const RECT &rc, COLORREF penColor, COLORREF bgColor, int radius = 4)
            {
                HBRUSH hBrush = CreateSolidBrush(bgColor);
                HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
                HPEN hPen = CreatePen(PS_SOLID, 1, penColor);
                HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
                RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
                SelectObject(hdc, hOldPen);
                DeleteObject(hPen);
                SelectObject(hdc, hOldBrush);
                DeleteObject(hBrush);
            }

            void DrawFrame(HDC hdc, const RECT &rc, COLORREF penColor, COLORREF bgColor)
            {
                HBRUSH hBrush = CreateSolidBrush(bgColor);
                HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
                HPEN hPen = CreatePen(PS_SOLID, 1, penColor);
                HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
                Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
                SelectObject(hdc, hOldPen);
                DeleteObject(hPen);
                SelectObject(hdc, hOldBrush);
                DeleteObject(hBrush);
            }

        }
        namespace Button
        {
            struct WinData
            {
                WNDPROC origProc = nullptr;
                CtrlState state = CtrlState::Normal;
                HWND hWnd = nullptr;
                HFONT font = nullptr;
            };

            namespace
            {
                COLORREF GetBgColor(CtrlState state)
                {
                    switch (state)
                    {
                    case CtrlState::Hover:
                        return theme.PrimaryForeground.ToCOLORREF();
                    case CtrlState::Pressed:
                    {
                        auto c = theme.PrimaryForeground;
                        return RGB(c.r / 2, c.g / 2, c.b / 2);
                    }
                    case CtrlState::Disabled:
                        return theme.SecondaryForeground.ToCOLORREF();
                    default:
                        return theme.SecondaryBackground.ToCOLORREF();
                    }
                }

                COLORREF GetTextColor(CtrlState state)
                {
                    if (state == CtrlState::Disabled)
                    {
                        auto c = theme.Text;
                        return RGB(c.r / 2, c.g / 2, c.b / 2);
                    }
                    return theme.Text.ToCOLORREF();
                }
            }

            void Unsubclass(HWND hBtn);

            LRESULT CALLBACK WinProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
            {
                WinData *pData = (WinData *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
                if (!pData || !pData->origProc)
                    return DefWindowProc(hWnd, msg, wParam, lParam);

                switch (msg)
                {
                case WM_SETFONT:
                {
                    pData->font = (HFONT)wParam;
                    break;
                }
                case WM_MOUSEMOVE:
                {
                    if (pData->state != CtrlState::Disabled)
                    {
                        if (pData->state != CtrlState::Pressed)
                        {
                            pData->state = CtrlState::Hover;
                            InvalidateRect(hWnd, nullptr, TRUE);
                        }
                        TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hWnd, 0};
                        TrackMouseEvent(&tme);
                    }
                    break;
                }
                case WM_MOUSELEAVE:
                    pData->state = CtrlState::Normal;
                    InvalidateRect(hWnd, nullptr, TRUE);
                    break;
                case WM_LBUTTONDOWN:
                    if (pData->state != CtrlState::Disabled)
                    {
                        pData->state = CtrlState::Pressed;
                        InvalidateRect(hWnd, nullptr, TRUE);
                    }
                    break;
                case WM_LBUTTONUP:
                    if (pData->state == CtrlState::Pressed)
                    {
                        pData->state = CtrlState::Hover;
                        InvalidateRect(hWnd, nullptr, TRUE);
                    }
                    break;
                case WM_ENABLE:
                    pData->state = (wParam) ? CtrlState::Normal : CtrlState::Disabled;
                    InvalidateRect(hWnd, nullptr, TRUE);
                    break;
                case WM_ERASEBKGND:
                    return 1;
                case WM_PAINT:
                {
                    PAINTSTRUCT ps;
                    HDC hdc = BeginPaint(hWnd, &ps);
                    RECT rcClient;
                    GetClientRect(hWnd, &rcClient);

                    HDC hMemDC = CreateCompatibleDC(hdc);
                    HBITMAP hBmp = CreateCompatibleBitmap(hdc, rcClient.right, rcClient.bottom);
                    HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hBmp);

                    BitBlt(hMemDC, 0, 0, rcClient.right, rcClient.bottom, hdc, 0, 0, SRCCOPY);

                    CtrlDraw::DrawRoundFrame(hMemDC, rcClient, theme.ControlFrame.ToCOLORREF(), GetBgColor(pData->state), 5);

                    wchar_t szText[256] = {0};
                    GetWindowTextW(hWnd, szText, 256);
                    SetTextColor(hMemDC, GetTextColor(pData->state));
                    SetBkMode(hMemDC, TRANSPARENT);
                    HFONT hOldFont = (HFONT)SelectObject(hMemDC, pData->font);

                    RECT rcText = rcClient;
                    DrawTextW(hMemDC, szText, lstrlenW(szText), &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(hMemDC, hOldFont);

                    BitBlt(hdc, 0, 0, rcClient.right, rcClient.bottom, hMemDC, 0, 0, SRCCOPY);

                    SelectObject(hMemDC, hOldBmp);
                    DeleteObject(hBmp);
                    DeleteDC(hMemDC);

                    EndPaint(hWnd, &ps);
                    return 0;
                }
                case WM_DESTROY:
                {
                    LRESULT res = CallWindowProc(pData->origProc, hWnd, msg, wParam, lParam);
                    Unsubclass(hWnd);
                    return res;
                }
                case WM_DRAWITEM:
                    return 0;
                }

                return CallWindowProc(pData->origProc, hWnd, msg, wParam, lParam);
            }

            void Subclass(HWND hBtn)
            {
                WinData *pData = new WinData();
                pData->hWnd = hBtn;
                pData->origProc = (WNDPROC)GetWindowLongPtr(hBtn, GWLP_WNDPROC);
                SetWindowLongPtr(hBtn, GWLP_USERDATA, (LONG_PTR)pData);
                SetWindowLongPtr(hBtn, GWLP_WNDPROC, (LONG_PTR)WinProc);

                LONG_PTR style = GetWindowLongPtr(hBtn, GWL_STYLE);
                style &= ~BS_PUSHBUTTON;
                style &= ~BS_DEFPUSHBUTTON;
                style |= BS_OWNERDRAW;
                SetWindowLongPtr(hBtn, GWL_STYLE, style);
            }

            void Unsubclass(HWND hBtn)
            {
                WinData *pData = (WinData *)GetWindowLongPtr(hBtn, GWLP_USERDATA);
                if (pData)
                {
                    SetWindowLongPtr(hBtn, GWLP_WNDPROC, (LONG_PTR)pData->origProc);
                    SetWindowLongPtr(hBtn, GWLP_USERDATA, 0);
                    delete pData;
                }
            }
        }
        namespace Edit
        {
            struct WinData
            {
                WNDPROC origProc = nullptr;
                CtrlState state = CtrlState::Normal;
                HWND hWnd = nullptr;
                HFONT font = nullptr;
            };

            namespace
            {
                COLORREF GetBgColor(CtrlState state)
                {
                    switch (state)
                    {
                    case CtrlState::Disabled:
                        return theme.SecondaryForeground.ToCOLORREF();
                    default:
                        return theme.SecondaryBackground.ToCOLORREF();
                    }
                }
            }

            void Unsubclass(HWND hEdit);
            void AutoHideScrollbar(HWND hEdit);

            LRESULT CALLBACK WinProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
            {
                WinData *pData = (WinData *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
                if (!pData || !pData->origProc)
                    return DefWindowProc(hWnd, msg, wParam, lParam);

                switch (msg)
                {
                case WM_SETFONT:
                {
                    pData->font = (HFONT)wParam;
                    LRESULT res = CallWindowProc(pData->origProc, hWnd, msg, wParam, lParam);
                    AutoHideScrollbar(hWnd);
                    return res;
                }
                case WM_SETFOCUS:
                {
                    pData->state = CtrlState::Normal;
                    InvalidateRect(hWnd, nullptr, TRUE);
                    break;
                }
                case WM_KILLFOCUS:
                {
                    InvalidateRect(hWnd, nullptr, TRUE);
                    break;
                }
                case WM_ENABLE:
                    pData->state = (wParam) ? CtrlState::Normal : CtrlState::Disabled;
                    InvalidateRect(hWnd, nullptr, TRUE);
                    break;
                case WM_ERASEBKGND:
                {
                    HDC hdc = (HDC)wParam;
                    RECT rc;
                    GetClientRect(hWnd, &rc);
                    HBRUSH hBrush = CreateSolidBrush(GetBgColor(pData->state));
                    FillRect(hdc, &rc, hBrush);
                    DeleteObject(hBrush);
                    return 1;
                }
                case WM_NCPAINT:
                {
                    // 边框改由 WM_PAINT 中绘制，避免被默认 EDIT 绘制覆盖
                    return CallWindowProc(pData->origProc, hWnd, msg, wParam, lParam);
                }
                case WM_PAINT:
                {
                    // 先让默认 EDIT 控件绘制文本/光标/选区
                    CallWindowProc(pData->origProc, hWnd, msg, wParam, lParam);

                    // 再在其上方绘制无圆角边框（不填充内部，避免覆盖文本）
                    HDC hdc = GetDC(hWnd);
                    RECT rc;
                    GetClientRect(hWnd, &rc);
                    HPEN hPen = CreatePen(PS_INSIDEFRAME, 1, theme.ControlFrame.ToCOLORREF());
                    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
                    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
                    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
                    SelectObject(hdc, hOldPen);
                    SelectObject(hdc, hOldBrush);
                    DeleteObject(hPen);
                    ReleaseDC(hWnd, hdc);
                    return 0;
                }
                case WM_SIZE:
                {
                    LRESULT res = CallWindowProc(pData->origProc, hWnd, msg, wParam, lParam);
                    AutoHideScrollbar(hWnd);
                    return res;
                }
                case WM_DESTROY:
                {
                    LRESULT res = CallWindowProc(pData->origProc, hWnd, msg, wParam, lParam);
                    Unsubclass(hWnd);
                    return res;
                }
                }

                return CallWindowProc(pData->origProc, hWnd, msg, wParam, lParam);
            }

            void Subclass(HWND hEdit)
            {
                WinData *pData = new WinData();
                pData->hWnd = hEdit;
                pData->origProc = (WNDPROC)GetWindowLongPtr(hEdit, GWLP_WNDPROC);
                SetWindowLongPtr(hEdit, GWLP_USERDATA, (LONG_PTR)pData);
                SetWindowLongPtr(hEdit, GWLP_WNDPROC, (LONG_PTR)WinProc);

                LONG_PTR exStyle = GetWindowLongPtr(hEdit, GWL_EXSTYLE);
                exStyle &= ~WS_EX_CLIENTEDGE;
                SetWindowLongPtr(hEdit, GWL_EXSTYLE, exStyle);
                SetWindowPos(hEdit, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_DRAWFRAME);
            }

            void Unsubclass(HWND hEdit)
            {
                WinData *pData = (WinData *)GetWindowLongPtr(hEdit, GWLP_USERDATA);
                if (pData)
                {
                    SetWindowLongPtr(hEdit, GWLP_WNDPROC, (LONG_PTR)pData->origProc);
                    SetWindowLongPtr(hEdit, GWLP_USERDATA, 0);
                    delete pData;
                }
            }

            void AutoHideScrollbar(HWND hEdit)
            {
                LONG_PTR style = GetWindowLongPtr(hEdit, GWL_STYLE);
                if (!(style & WS_VSCROLL))
                    return;

                // 临时移除滚动条，检测文本是否装得下
                style &= ~WS_VSCROLL;
                SetWindowLongPtr(hEdit, GWL_STYLE, style);
                SetWindowPos(hEdit, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

                HDC hdc = GetDC(hEdit);
                if (hdc)
                {
                    HFONT hFont = (HFONT)SendMessage(hEdit, WM_GETFONT, 0, 0);
                    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont ? hFont : GetStockObject(SYSTEM_FONT));

                    RECT rc;
                    GetClientRect(hEdit, &rc);

                    int textLen = GetWindowTextLengthW(hEdit);
                    if (textLen > 0)
                    {
                        wchar_t *buf = new wchar_t[textLen + 1];
                        GetWindowTextW(hEdit, buf, textLen + 1);

                        RECT rcText = {0, 0, rc.right, 0};
                        DrawTextW(hdc, buf, -1, &rcText, DT_CALCRECT | DT_WORDBREAK | DT_LEFT | DT_TOP);

                        delete[] buf;

                        if (rcText.bottom > rc.bottom)
                        {
                            // 文本高度超过可见区域，恢复滚动条
                            style |= WS_VSCROLL;
                            SetWindowLongPtr(hEdit, GWL_STYLE, style);
                            SetWindowPos(hEdit, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
                        }
                    }

                    SelectObject(hdc, hOldFont);
                    ReleaseDC(hEdit, hdc);
                }
            }
        }
        namespace Tooltip
        {

            LRESULT CALLBACK WinProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

            namespace
            {
                const wchar_t CLASS_NAME[] = L"GL_Commdlg.TooltipClass";

                bool EnsureRegistered()
                {
                    static std::once_flag flag;
                    static bool registered = false;
                    std::call_once(flag, []()
                                   {
                    WNDCLASSEXW wc = {sizeof(wc)};
                    wc.style = CS_HREDRAW | CS_VREDRAW;
                    wc.lpfnWndProc = WinProc;
                    wc.hInstance = GetModuleHandleW(nullptr);
                    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
                    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
                    wc.lpszClassName = CLASS_NAME;
                    registered = RegisterClassExW(&wc) != 0; });
                    return registered;
                }
            }

            void MeasureText(HDC hdc, const std::wstring &text, HFONT hFont, int &outW, int &outH)
            {
                HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
                SIZE sz = {};
                GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.length()), &sz);
                SelectObject(hdc, hOldFont);
                outW = sz.cx + 18;
                outH = sz.cy + 10;
            }

            HWND Create(HWND hParent, const std::wstring &text, HFONT hFont)
            {
                if (!EnsureRegistered())
                    return nullptr;

                HDC hdc = GetDC(hParent);
                int tw = 40, th = 20;
                if (!text.empty())
                    MeasureText(hdc, text, hFont, tw, th);
                ReleaseDC(hParent, hdc);

                HWND hwnd = CreateWindowExW(
                    WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                    CLASS_NAME, text.c_str(), WS_POPUP,
                    0, 0, tw, th,
                    hParent, nullptr,
                    GetModuleHandleW(nullptr), nullptr);

                if (hwnd)
                    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)hFont);
                return hwnd;
            }

            void SetText(HWND hTooltip, const std::wstring &text, HFONT hFont)
            {
                if (!hTooltip || !IsWindow(hTooltip))
                    return;
                SetWindowTextW(hTooltip, text.c_str());
                SetWindowLongPtrW(hTooltip, GWLP_USERDATA, (LONG_PTR)hFont);

                HWND hParent = GetWindow(hTooltip, GW_OWNER);
                if (!hParent)
                    hParent = GetDesktopWindow();

                HDC hdc = GetDC(hParent);
                int tw, th;
                MeasureText(hdc, text, hFont, tw, th);
                ReleaseDC(hParent, hdc);

                SetWindowPos(hTooltip, nullptr, 0, 0, tw, th, SWP_NOMOVE | SWP_NOZORDER);
                InvalidateRect(hTooltip, nullptr, TRUE);
            }

            void SetPosition(HWND hTooltip, int screenX, int screenY, int width = 0, int height = 0)
            {
                if (!hTooltip || !IsWindow(hTooltip))
                    return;
                if (width == 0 || height == 0)
                {
                    RECT rc;
                    GetWindowRect(hTooltip, &rc);
                    width = rc.right - rc.left;
                    height = rc.bottom - rc.top;
                }
                SetWindowPos(hTooltip, nullptr, screenX, screenY, width, height, SWP_NOZORDER);
            }

            void Destroy(HWND hTooltip)
            {
                if (hTooltip && IsWindow(hTooltip))
                    DestroyWindow(hTooltip);
            }

            LRESULT CALLBACK WinProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
            {
                switch (msg)
                {
                case WM_NCPAINT:
                case WM_NCACTIVATE:
                    return 0;
                case WM_PAINT:
                {
                    PAINTSTRUCT ps;
                    HDC hdc = BeginPaint(hwnd, &ps);
                    RECT rc;
                    GetClientRect(hwnd, &rc);

                    HBRUSH hBrush = CreateSolidBrush(theme.SecondaryBackground.ToCOLORREF());
                    FillRect(hdc, &rc, hBrush);
                    DeleteObject(hBrush);

                    HRGN hClipRgn = CreateRoundRectRgn(0, 0, rc.right, rc.bottom, 8, 8);
                    SelectClipRgn(hdc, hClipRgn);
                    DeleteObject(hClipRgn);

                    HFONT hFont = (HFONT)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
                    if (hFont)
                    {
                        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
                        SetTextColor(hdc, theme.Text.ToCOLORREF());
                        SetBkMode(hdc, TRANSPARENT);
                        wchar_t szText[256] = {0};
                        GetWindowTextW(hwnd, szText, 256);
                        DrawTextW(hdc, szText, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                        SelectObject(hdc, hOldFont);
                    }

                    SelectClipRgn(hdc, NULL);
                    HPEN hPen = CreatePen(PS_SOLID, 1, theme.ControlFrame.ToCOLORREF());
                    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
                    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
                    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
                    SelectObject(hdc, hOldPen);
                    SelectObject(hdc, hOldBrush);
                    DeleteObject(hPen);

                    EndPaint(hwnd, &ps);
                    return 0;
                }
                case WM_ERASEBKGND:
                    return 1;
                }
                return DefWindowProcW(hwnd, msg, wParam, lParam);
            }
        }

    };

#define __GCOMMMDLG_IDC_PROMPT 1001 // 提示文本
#define __GCOMMMDLG_IDC_INPUT 1002  // 输入框
#define __GCOMMMDLG_IDOK 1003       // 确定按钮
#define __GCOMMMDLG_IDCANCEL 1004   // 取消按钮

    namespace
    {

        WCHAR *g_inputText = nullptr;
        std::wstring g_defalutContent;
        std::wstring g_message;
        bool g_did_confirm;

        LRESULT CALLBACK PromptDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
        {

            static HWND hStaticPrompt = NULL;
            static HWND hEditInput = NULL;
            static HWND hButtonOK = NULL;
            static HWND hButtonCancel = NULL;
            static HBRUSH hDefaultBrush = CreateSolidBrush(theme.PrimaryBackground.ToCOLORREF());

            switch (msg)
            {
            case WM_CREATE:
            {

                Controls::InitWindowColor(hDlg);

                HFONT hFont = CreateFontW(
                    24, 0, 0, 0, FW_NORMAL,
                    FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                    L"Segoe UI");

                hStaticPrompt = CreateWindowExW(
                    0,
                    L"STATIC",
                    g_message.c_str(),
                    WS_CHILD | WS_VISIBLE | SS_LEFT,
                    20, 20, 260, 25,
                    hDlg,
                    (HMENU)__GCOMMMDLG_IDC_PROMPT,
                    ((LPCREATESTRUCTW)lParam)->hInstance,
                    NULL);

                hEditInput = CreateWindowExW(
                    0,
                    L"EDIT",
                    g_defalutContent.c_str(),
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                    20, 50, 360, 30,
                    hDlg,
                    (HMENU)__GCOMMMDLG_IDC_INPUT,
                    ((LPCREATESTRUCTW)lParam)->hInstance,
                    NULL);

                Controls::Edit::Subclass(hEditInput);

                hButtonOK = CreateWindowExW(
                    0,
                    L"BUTTON",
                    L"确定",
                    WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                    120, 95, 80, 30,
                    hDlg,
                    (HMENU)__GCOMMMDLG_IDOK,
                    ((LPCREATESTRUCTW)lParam)->hInstance,
                    NULL);

                Controls::Button::Subclass(hButtonOK);

                hButtonCancel = CreateWindowExW(
                    0,
                    L"BUTTON",
                    L"取消",
                    WS_CHILD | WS_VISIBLE,
                    220, 95, 80, 30,
                    hDlg,
                    (HMENU)__GCOMMMDLG_IDCANCEL,
                    ((LPCREATESTRUCTW)lParam)->hInstance,
                    NULL);

                Controls::Button::Subclass(hButtonCancel);

                if (hFont)
                {
                    SendMessage(hStaticPrompt, WM_SETFONT, (WPARAM)hFont, TRUE);
                    SendMessage(hEditInput, WM_SETFONT, (WPARAM)hFont, TRUE);
                    SendMessage(hButtonOK, WM_SETFONT, (WPARAM)hFont, TRUE);
                    SendMessage(hButtonCancel, WM_SETFONT, (WPARAM)hFont, TRUE);
                }

                return 0;
            }

            case WM_SIZE:
            {
                int clientWidth = LOWORD(lParam);
                int clientHeight = HIWORD(lParam);

                if (hStaticPrompt)
                {
                    SetWindowPos(hStaticPrompt, NULL,
                                 20, 20,
                                 clientWidth - 40, 25,
                                 SWP_NOZORDER);
                }

                if (hEditInput)
                {
                    SetWindowPos(hEditInput, NULL,
                                 20, 55,
                                 clientWidth - 40, 30,
                                 SWP_NOZORDER);
                }

                if (hButtonOK && hButtonCancel)
                {
                    int buttonWidth = 80;
                    int buttonHeight = 30;
                    int buttonY = clientHeight - buttonHeight - 15;
                    int totalButtonWidth = buttonWidth * 2 + 20;
                    int startX = (clientWidth - totalButtonWidth) / 2;

                    SetWindowPos(hButtonOK, NULL,
                                 startX, buttonY,
                                 buttonWidth, buttonHeight,
                                 SWP_NOZORDER);

                    SetWindowPos(hButtonCancel, NULL,
                                 startX + buttonWidth + 20, buttonY,
                                 buttonWidth, buttonHeight,
                                 SWP_NOZORDER);
                }
                return 0;
            }

            case WM_COMMAND:
            {
                if (LOWORD(wParam) == __GCOMMMDLG_IDOK)
                {
                    WCHAR buffer[256] = {0};
                    GetDlgItemTextW(hDlg, __GCOMMMDLG_IDC_INPUT, buffer, 256);
                    wcscpy(g_inputText, buffer);
                    g_did_confirm = true;
                    DestroyWindow(hDlg);
                }
                else if (LOWORD(wParam) == __GCOMMMDLG_IDCANCEL)
                {
                    wcscpy(g_inputText, L"");
                    g_did_confirm = false;
                    DestroyWindow(hDlg);
                }
                return 0;
            }

            case WM_CLOSE:
                wcscpy(g_inputText, L"");
                g_did_confirm = false;
                DestroyWindow(hDlg);
                return 0;

            case WM_CTLCOLOREDIT:
            {
                HDC hdc = (HDC)wParam;
                SetBkColor(hdc, theme.SecondaryBackground.ToCOLORREF());
                SetTextColor(hdc, theme.Text.ToCOLORREF());
                static HBRUSH hEditBrush = CreateSolidBrush(theme.SecondaryBackground.ToCOLORREF());
                return (LRESULT)hEditBrush;
            }

            case WM_CTLCOLORSTATIC:
            {
                HDC hdc = (HDC)wParam;
                SetBkColor(hdc, theme.PrimaryBackground.ToCOLORREF());
                SetTextColor(hdc, theme.Text.ToCOLORREF());
                return (LRESULT)hDefaultBrush;
            }

            case WM_CTLCOLORBTN:
            {
                HDC hdc = (HDC)wParam;
                SetBkColor(hdc, theme.PrimaryBackground.ToCOLORREF());
                SetTextColor(hdc, theme.Text.ToCOLORREF());
                return (LRESULT)hDefaultBrush;
            }

            case WM_DESTROY:
            {
                PostQuitMessage(0);
                return 0;
            }

            case WM_ERASEBKGND:
            {
                HDC hdc = (HDC)wParam;
                RECT rect;
                GetClientRect(hDlg, &rect);
                FillRect(hdc, &rect, hDefaultBrush);
                return TRUE;
            }

            default:
                return DefWindowProcW(hDlg, msg, wParam, lParam);
            }
        }
    }

    /**
     * @brief Show an input dialog for the user to enter a string
     * @brief 显示输入对话框，用于让用户输入一段字符串
     *
     * @param title Title of the input dialog
     * @param title 输入对话框的标题
     * @param message Prompt text displayed inside the input dialog
     * @param message 输入对话框内显示的提示文本
     * @param output Output string receiving the user's input
     * @param output 输出用户输入的内容
     * @param defaultContent Default content pre-filled in the input field
     * @param defaultContent 输入栏内的默认内容
     * @param hParent Parent window handle of the input dialog
     * @param hParent 输入对话框的父窗口句柄
     * @return Whether the user confirmed the input
     * @return 用户是否确认了输入
     */
    bool promptDialog(std::string title, std::string message, std::string &output, std::string defaultContent = "", HWND hParent = NULL)
    {

        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = PromptDialogProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"GL_Commdlg.PromptDialogClass";
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.style = CS_HREDRAW | CS_VREDRAW;

        RegisterClassExW(&wc);

        int targetWidth = GetSystemMetrics(SM_CXSCREEN);
        int targetHeight = GetSystemMetrics(SM_CYSCREEN);

        int x = targetWidth / 2 - 400 / 2, y = targetHeight / 2 - 180 / 2;

        g_inputText = new wchar_t[256];
        g_message = utf8ToWide(message);
        g_defalutContent = utf8ToWide(defaultContent);

        HWND hDlg = CreateWindowExW(
            0,
            L"GL_Commdlg.PromptDialogClass",
            utf8ToWide(title).c_str(),
            WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME /* | WS_SIZEBOX*/,
            x, y, 400, 180,
            hParent,
            NULL,
            GetModuleHandleW(NULL),
            NULL);

        if (hDlg)
        {
            ShowWindow(hDlg, SW_SHOW);
            UpdateWindow(hDlg);

            MSG msg;

            while (GetMessageW(&msg, NULL, 0, 0))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        UnregisterClassW(L"GL_Commdlg.PromptDialogClass", GetModuleHandleW(NULL));

        if (!g_did_confirm)
        {
            output = "";
            return false;
        }

        output = wideToUtf8(g_inputText);
        delete[] g_inputText;
        return true;
    }

#define __GCOMMMDLG_BTN_START 2000 // 选项按钮起始ID

    namespace
    {

        std::vector<std::pair<int, std::wstring>> g_options;
        std::wstring g_msgContent;
        std::wstring g_boxTitle;
        int g_selectedId = 0;

        LRESULT CALLBACK MessageBoxDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
        {

            static std::vector<HWND> hButtons;
            static HBRUSH hDefaultBrush = CreateSolidBrush(theme.PrimaryBackground.ToCOLORREF());
            static HFONT hFont = NULL;
            static int calculatedTextHeight = 0;

            switch (msg)
            {
            case WM_CREATE:
            {

                Controls::InitWindowColor(hDlg);

                const int TEXT_MARGIN_TOP = 20;
                const int BTN_GAP_ABOVE = 20;
                const int BTN_HEIGHT = 30;
                const int BTN_SPACING = 20;
                const int BTN_PADDING = 20; // horizontal text padding inside button
                const int BTN_BOTTOM_MARGIN = 24;
                const int MIN_BTN_WIDTH = 80;

                hFont = CreateFontW(
                    24, 0, 0, 0, FW_NORMAL,
                    FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                    L"Segoe UI");

                // Measure button label widths & message text height
                int maxBtnWidth = MIN_BTN_WIDTH;
                HDC hdc = GetDC(hDlg);
                if (hdc && hFont)
                {
                    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

                    // Measure each button label
                    for (const auto &opt : g_options)
                    {
                        SIZE sz = {};
                        GetTextExtentPoint32W(hdc, opt.second.c_str(),
                                              static_cast<int>(opt.second.length()), &sz);
                        int w = sz.cx + BTN_PADDING;
                        if (w > maxBtnWidth)
                            maxBtnWidth = w;
                    }

                    SelectObject(hdc, hOldFont);
                    ReleaseDC(hDlg, hdc);
                }

                // Calculate dialog width based on button grid (max 3 per row)
                size_t numBtns = g_options.size();
                int btnCols = (std::min)(static_cast<int>(numBtns), 3);
                int btnRows = static_cast<int>((numBtns + 2) / 3);
                int clientWidth = 20 + btnCols * maxBtnWidth + (btnCols - 1) * BTN_SPACING + 20;
                clientWidth = (std::max)(clientWidth, 400);
                int textWidth = clientWidth - 40;

                // Calculate text height with the actual text width
                hdc = GetDC(hDlg);
                if (hdc && hFont)
                {
                    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
                    RECT rcText = {0, 0, textWidth, 0};
                    DrawTextW(hdc, g_msgContent.c_str(), -1, &rcText,
                              DT_CALCRECT | DT_WORDBREAK | DT_LEFT | DT_TOP);
                    calculatedTextHeight = rcText.bottom;
                    SelectObject(hdc, hOldFont);
                    ReleaseDC(hDlg, hdc);
                }
                else
                {
                    calculatedTextHeight = 26;
                }

                // Calculate desired client area size
                int btnAreaH = btnRows * BTN_HEIGHT + (btnRows - 1) * 10;
                int clientHeight = TEXT_MARGIN_TOP + calculatedTextHeight +
                                   BTN_GAP_ABOVE + btnAreaH + BTN_BOTTOM_MARGIN;
                clientHeight = (std::max)(clientHeight, 120);

                // Convert client area → window size (account for title bar & border)
                RECT rcWin = {0, 0, clientWidth, clientHeight};
                AdjustWindowRectEx(&rcWin,
                                   WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME,
                                   FALSE, 0);
                int winWidth = rcWin.right - rcWin.left;
                int winHeight = rcWin.bottom - rcWin.top;

                // Center and resize the dialog
                int scrW = GetSystemMetrics(SM_CXSCREEN);
                int scrH = GetSystemMetrics(SM_CYSCREEN);
                int cx = (scrW - winWidth) / 2;
                int cy = (scrH - winHeight) / 2;
                SetWindowPos(hDlg, NULL, cx, cy, winWidth, winHeight,
                             SWP_NOZORDER | SWP_NOACTIVATE);

                // Create buttons at correct positions with dynamic width
                int startY = TEXT_MARGIN_TOP + calculatedTextHeight + BTN_GAP_ABOVE;
                hButtons.reserve(numBtns);
                for (size_t i = 0; i < numBtns; ++i)
                {
                    int col = static_cast<int>(i % 3);
                    int row = static_cast<int>(i / 3);
                    int btnX = 20 + col * (maxBtnWidth + BTN_SPACING);
                    int btnY = startY + row * (BTN_HEIGHT + 10);

                    HWND hBtn = CreateWindowExW(
                        0, L"BUTTON", g_options[i].second.c_str(),
                        WS_CHILD | WS_VISIBLE,
                        btnX, btnY, maxBtnWidth, BTN_HEIGHT,
                        hDlg, (HMENU)(INT_PTR)(__GCOMMMDLG_BTN_START + i),
                        ((LPCREATESTRUCTW)lParam)->hInstance, NULL);
                    Controls::Button::Subclass(hBtn);
                    hButtons.push_back(hBtn);
                    if (hFont)
                    {
                        SendMessage(hBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
                    }
                }
                return 0;
            }

            // Draw message text using GDI (supports word wrapping)
            case WM_PAINT:
            {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hDlg, &ps);
                if (hFont && calculatedTextHeight > 0)
                {
                    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
                    RECT textRect = {20, 20, 380, 20 + calculatedTextHeight};
                    SetTextColor(hdc, theme.Text.ToCOLORREF());
                    SetBkMode(hdc, TRANSPARENT);
                    DrawTextW(hdc, g_msgContent.c_str(), -1, &textRect,
                              DT_WORDBREAK | DT_LEFT | DT_TOP);
                    SelectObject(hdc, hOldFont);
                }
                EndPaint(hDlg, &ps);
                return 0;
            }

            case WM_COMMAND:
            {
                int btnId = LOWORD(wParam);
                if (btnId >= __GCOMMMDLG_BTN_START && btnId < __GCOMMMDLG_BTN_START + (int)g_options.size())
                {
                    size_t index = btnId - __GCOMMMDLG_BTN_START;
                    g_selectedId = g_options[index].first;
                    DestroyWindow(hDlg);
                }
                return 0;
            }

            case WM_CLOSE:
            {
                g_selectedId = 0;
                DestroyWindow(hDlg);
                return 0;
            }

            case WM_DESTROY:
            {
                if (hFont)
                {
                    DeleteObject(hFont);
                    hFont = NULL;
                }
                calculatedTextHeight = 0;
                // for(auto i : hButtons){

                // }
                hButtons.clear();
                PostQuitMessage(0);
                return 0;
            }

                // case WM_CTLCOLORBTN:
                // {
                //     HDC hdc = (HDC)wParam;
                //     SetBkColor(hdc, RGB(240, 240, 240));
                //     SetTextColor(hdc, RGB(0, 0, 0));
                //     return (LRESULT)hDefaultBrush;
                // }

            case WM_ERASEBKGND:
            {
                HDC hdc = (HDC)wParam;
                RECT rect;
                GetClientRect(hDlg, &rect);
                FillRect(hdc, &rect, hDefaultBrush);
                return TRUE;
            }

            default:
                return DefWindowProcW(hDlg, msg, wParam, lParam);
            }
        }

        LRESULT CALLBACK MessageBoxDialogProc2(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            static HWND hEditMsg = NULL;
            static std::vector<HWND> hButtons;
            static HBRUSH hDefaultBrush = CreateSolidBrush(theme.PrimaryBackground.ToCOLORREF());
            static HFONT hFont = NULL;
            static int calculatedEditHeight = 0;

            switch (msg)
            {
            case WM_CREATE:
            {

                Controls::InitWindowColor(hDlg);

                hFont = CreateFontW(
                    24, 0, 0, 0, FW_NORMAL,
                    FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                    L"Segoe UI");

                hEditMsg = CreateWindowExW(
                    0,
                    L"EDIT",
                    g_msgContent.c_str(),
                    WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL |
                        ES_WANTRETURN | WS_VSCROLL | ES_LEFT,
                    20, 20, 360, 100,
                    hDlg,
                    (HMENU)1001,
                    ((LPCREATESTRUCTW)lParam)->hInstance,
                    NULL);

                Controls::Edit::Subclass(hEditMsg);

                if (hFont)
                {
                    SendMessage(hEditMsg, WM_SETFONT, (WPARAM)hFont, TRUE);
                }

                HDC hdc = GetDC(hEditMsg);
                if (hdc && hFont)
                {
                    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

                    RECT rcText = {0, 0, 360 - 10, 0};
                    const wchar_t *text = g_msgContent.c_str();

                    DrawTextW(hdc, text, -1, &rcText, DT_CALCRECT | DT_WORDBREAK | DT_LEFT | DT_TOP | DT_EXPANDTABS);

                    calculatedEditHeight = std::min<int>(std::max<int>(rcText.bottom + 20, 60), 400);

                    SelectObject(hdc, hOldFont);
                    ReleaseDC(hEditMsg, hdc);

                    SetWindowPos(hEditMsg, NULL, 20, 20, 360, calculatedEditHeight, SWP_NOZORDER);
                }

                // Measure each button label to determine dynamic width
                int maxBtnWidth = 80; // minimum
                if (hFont)
                {
                    HDC hdc2 = GetDC(hDlg);
                    if (hdc2)
                    {
                        HFONT hOldFont = (HFONT)SelectObject(hdc2, hFont);
                        const int btnPadding = 20;
                        for (const auto &opt : g_options)
                        {
                            SIZE sz = {};
                            GetTextExtentPoint32W(hdc2, opt.second.c_str(),
                                                  static_cast<int>(opt.second.length()), &sz);
                            int w = sz.cx + btnPadding;
                            if (w > maxBtnWidth)
                                maxBtnWidth = w;
                        }
                        SelectObject(hdc2, hOldFont);
                        ReleaseDC(hDlg, hdc2);
                    }
                }

                hButtons.reserve(g_options.size());
                const int btnHeight = 32;
                const int btnSpacing = 15;

                // Calculate dialog width to fit all buttons in a single row
                int btnCols = static_cast<int>(g_options.size());
                int clientWidth = 20 + btnCols * maxBtnWidth + (btnCols - 1) * btnSpacing + 20;
                clientWidth = (std::max)(clientWidth, 400);
                int totalBtnWidth = btnCols * maxBtnWidth + (btnCols - 1) * btnSpacing;
                int startX = (clientWidth - totalBtnWidth) / 2;

                for (size_t i = 0; i < g_options.size(); ++i)
                {
                    HWND hBtn = CreateWindowExW(
                        0,
                        L"BUTTON",
                        g_options[i].second.c_str(),
                        WS_CHILD | WS_VISIBLE,
                        startX + (int)i * (maxBtnWidth + btnSpacing),
                        20 + calculatedEditHeight + 20,
                        maxBtnWidth, btnHeight,
                        hDlg,
                        (HMENU)(__GCOMMMDLG_BTN_START + i),
                        ((LPCREATESTRUCTW)lParam)->hInstance,
                        NULL);

                    Controls::Button::Subclass(hBtn);

                    hButtons.push_back(hBtn);

                    if (hFont)
                    {
                        SendMessage(hBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
                    }
                }

                int totalHeight = 20 + calculatedEditHeight + 20 + btnHeight + 20;
                // Also resize width to fit buttons
                RECT rcWin2 = {0, 0, clientWidth, totalHeight};
                AdjustWindowRectEx(&rcWin2,
                                   WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME,
                                   FALSE, 0);
                int winWidth = rcWin2.right - rcWin2.left;
                int winHeight = rcWin2.bottom - rcWin2.top;
                int scrW = GetSystemMetrics(SM_CXSCREEN);
                int scrH = GetSystemMetrics(SM_CYSCREEN);
                int cx = (scrW - winWidth) / 2;
                int cy = (scrH - winHeight) / 2;
                SetWindowPos(hDlg, NULL, cx, cy, winWidth, winHeight,
                             SWP_NOZORDER | SWP_NOACTIVATE);

                return 0;
            }

            case WM_SIZE:
            {
                int clientWidth = LOWORD(lParam);

                if (hEditMsg)
                {
                    SetWindowPos(hEditMsg, NULL,
                                 20, 20,
                                 clientWidth - 40, calculatedEditHeight,
                                 SWP_NOZORDER);
                }

                if (!hButtons.empty())
                {
                    // Re-measure button widths from actual button text
                    // Since hFont might have been deleted, use saved hFont
                    int btnWidth = 80;
                    // Read back actual button widths by getting text
                    // For simplicity, measure the first button's width
                    // Since all buttons share the same max width
                    // We stored the button handles, we can just get the window text
                    WCHAR buf[256];
                    int maxW = 0;
                    for (HWND hBtn : hButtons)
                    {
                        GetWindowTextW(hBtn, buf, 256);
                        // Use a rough estimate: count characters * some factor
                        int len = (int)wcslen(buf);
                        // Estimate 14px per CJK char at 20pt, 8px per Latin at 18pt
                        int estW = len * 8 + 20;
                        if (estW > maxW)
                            maxW = estW;
                    }
                    btnWidth = (std::max)(maxW, 80);

                    const int btnHeight = 32;
                    const int btnSpacing = 15;
                    int totalBtnWidth = (btnWidth + btnSpacing) * (int)hButtons.size() - btnSpacing;
                    int startX = (clientWidth - totalBtnWidth) / 2;
                    int startY = 20 + calculatedEditHeight + 20;

                    for (size_t i = 0; i < hButtons.size(); ++i)
                    {
                        SetWindowPos(hButtons[i], NULL,
                                     startX + (int)i * (btnWidth + btnSpacing),
                                     startY,
                                     btnWidth, btnHeight,
                                     SWP_NOZORDER);
                    }
                }
                return 0;
            }

            case WM_COMMAND:
            {
                int btnId = LOWORD(wParam);
                if (btnId >= __GCOMMMDLG_BTN_START && btnId < __GCOMMMDLG_BTN_START + (int)g_options.size())
                {
                    size_t index = btnId - __GCOMMMDLG_BTN_START;
                    g_selectedId = g_options[index].first;
                    DestroyWindow(hDlg);
                }
                return 0;
            }

            case WM_CLOSE:
            {
                g_selectedId = 0;
                DestroyWindow(hDlg);
                return 0;
            }

            case WM_DESTROY:
            {

                if (hFont)
                {
                    DeleteObject(hFont);
                    hFont = NULL;
                }
                hButtons.clear();
                PostQuitMessage(0);
                return 0;
            }

            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORSTATIC:
            {
                HDC hdc = (HDC)wParam;
                SetBkColor(hdc, theme.SecondaryBackground.ToCOLORREF());
                SetTextColor(hdc, theme.Text.ToCOLORREF());
                static HBRUSH hEditBrush = CreateSolidBrush(theme.SecondaryBackground.ToCOLORREF());
                return (LRESULT)hEditBrush;
            }

            case WM_CTLCOLORBTN:
            {
                HDC hdc = (HDC)wParam;
                SetBkColor(hdc, theme.PrimaryBackground.ToCOLORREF());
                SetTextColor(hdc, theme.Text.ToCOLORREF());
                return (LRESULT)hDefaultBrush;
            }

            case WM_ERASEBKGND:
            {
                HDC hdc = (HDC)wParam;
                RECT rect;
                GetClientRect(hDlg, &rect);
                FillRect(hdc, &rect, hDefaultBrush);
                return TRUE;
            }

            default:
                return DefWindowProcW(hDlg, msg, wParam, lParam);
            }
        }
    }

    /**
     * @brief Display a custom message box with multiple option buttons
     * @brief 显示自定义消息对话框，支持多个选项按钮
     *
     * @param title Dialog title
     * @param title 对话框标题
     * @param message Prompt text inside the dialog
     * @param message 对话框内的提示文本
     * @param options Option set (key = return value, value = button text)
     * @param options 选项集合（键为返回值，值为按钮文本）
     * @param hParent Parent window handle
     * @param hParent 父窗口句柄
     * @param style Dialog style: 0 = GDI rendered text (supports word wrapping), 1 = EDIT control
     * @param style 对话框的样式，若为0，则使用GDI绘制文本。若为1，则使用EDIT控件来显示提示文本。
     * @return Selected option ID (returns 0 if closed, returns -1 if options is empty)
     * @return 选中的选项ID（关闭窗口返回0，要是你传入的options没有元素则返回-1以告知失败）
     */
    int messageBox(std::string title, std::string message, const std::vector<std::pair<int, std::string>> &options, HWND hParent = NULL, int style = 0)
    {

        if (options.empty())
            return -1;

        g_options.clear();
        for (const auto &opt : options)
        {
            g_options.emplace_back(opt.first, utf8ToWide(opt.second));
        }
        g_msgContent = utf8ToWide(message);
        g_boxTitle = utf8ToWide(title);
        g_selectedId = 0;

        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(WNDCLASSEXW);

        if (style == 1)
        {
            wc.lpfnWndProc = MessageBoxDialogProc2;
        }
        else
        {
            wc.lpfnWndProc = MessageBoxDialogProc;
        }

        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"GL_Commdlg.MessageBoxClass";
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.style = CS_HREDRAW | CS_VREDRAW;

        if (!RegisterClassExW(&wc))
        {
            return 0;
        }

        // Initial dimensions; style 0 will be resized in WM_CREATE
        int windowWidth = 400;
        int windowHeight = 200;
        int x = (GetSystemMetrics(SM_CXSCREEN) - windowWidth) / 2;
        int y = (GetSystemMetrics(SM_CYSCREEN) - windowHeight) / 2;

        HWND hDlg = CreateWindowExW(
            0,
            L"GL_Commdlg.MessageBoxClass",
            g_boxTitle.c_str(),
            WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME,
            x, y, windowWidth, windowHeight,
            hParent,
            NULL,
            GetModuleHandleW(NULL),
            NULL);

        if (hDlg)
        {
            ShowWindow(hDlg, SW_SHOW);
            UpdateWindow(hDlg);

            MSG msg;
            while (GetMessageW(&msg, NULL, 0, 0))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        UnregisterClassW(L"GL_Commdlg.MessageBoxClass", GetModuleHandleW(NULL));
        g_options.clear();

        return g_selectedId;
    }

    /*
        我接下来会引入一个新的概念：动态对话框
        动态对话框是在传统静态阻塞型对话框的基础上进行创新的新型动态非阻塞型对话框
        调用动态对话框函数会返回一个Interface用与控制创建的动态对话框
        动态对话框的生命周期与返回的Interface一致
        动态对话框由一个单独的线程来维护，线程的维护则由Interface来完成
        也就是说Interface的析构函数必须完成动态对话框的所有资源释放
        Interface类是不可复制的，仅能进行移动操作
    */

    class DynamicDialogInterface
    {
    public:
        virtual ~DynamicDialogInterface() = default;

        // 禁止复制，允许移动
        DynamicDialogInterface(const DynamicDialogInterface &) = delete;
        DynamicDialogInterface &operator=(const DynamicDialogInterface &) = delete;
        DynamicDialogInterface(DynamicDialogInterface &&) = default;
        DynamicDialogInterface &operator=(DynamicDialogInterface &&) = default;

        // 显示对话框
        virtual void Show() = 0;

        // 关闭对话框
        virtual void Close() = 0;

    protected:
        DynamicDialogInterface() = default;
    };

    namespace
    {
        // 进度条消息类型
        enum class ProgressBarMessageType
        {
            SetValue,
            Close
        };

        // 进度条消息结构
        struct ProgressBarMessage
        {
            ProgressBarMessageType type;
            uint64_t currentValue;
            uint64_t maxValue;
            std::string message;

            ProgressBarMessage(ProgressBarMessageType t, uint64_t c = 0, uint64_t m = 0, const std::string &msg = "")
                : type(t), currentValue(c), maxValue(m), message(msg) {}
        };
    }

    // 动态进度条接口类
    class DynamicProgressBar : public DynamicDialogInterface
    {
    private:
        // 对话框数据
        struct DialogData
        {
            HWND hwnd = nullptr;
            HWND hwndParent = nullptr;
            std::wstring title;
            std::atomic<uint64_t> currentValue{0};
            std::atomic<uint64_t> maxValue{100};
            std::atomic<bool> isFinished{false};
            std::string currentMessage;
            std::mutex dataMutex;

            // UI资源
            HFONT hFont = nullptr;
            HBRUSH hBackgroundBrush = nullptr;
            HPEN hBorderPen = nullptr;
            HPEN hProgressPen = nullptr;

            void UpdateMessage(const std::string &msg)
            {
                std::lock_guard<std::mutex> lock(dataMutex);
                currentMessage = msg;
            }

            std::string GetMessage()
            {
                std::lock_guard<std::mutex> lock(dataMutex);
                return currentMessage;
            }
        };

        // 线程控制
        std::unique_ptr<std::thread> dialogThread;
        std::atomic<bool> threadRunning{false};
        std::condition_variable messageCV;
        std::mutex messageMutex;
        std::queue<ProgressBarMessage> messageQueue;

        // 对话框数据
        std::shared_ptr<DialogData> dialogData;

        // 窗口类注册状态
        static bool IsWindowClassRegistered()
        {
            static std::once_flag registerFlag;
            static bool registered = false;

            std::call_once(registerFlag, []()
                           {
            WNDCLASSEXW wc = {0};
            wc.cbSize = sizeof(WNDCLASSEXW);
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = WindowProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            wc.lpszClassName = L"GL_Commdlg.DynamicProgressBarClass";
            
            registered = RegisterClassExW(&wc) != 0; });

            return registered;
        }

        // 窗口过程（静态，通过用户数据获取实例）
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            if (msg == WM_NCCREATE)
            {
                CREATESTRUCT *pCreate = reinterpret_cast<CREATESTRUCT *>(lParam);
                DialogData *pData = reinterpret_cast<DialogData *>(pCreate->lpCreateParams);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pData));
                return DefWindowProcW(hwnd, msg, wParam, lParam);
            }

            DialogData *pData = reinterpret_cast<DialogData *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

            switch (msg)
            {
            case WM_CREATE:
            {

                Controls::InitWindowColor(hwnd);

                if (!pData)
                    return -1;

                pData->hwnd = hwnd;

                // 创建字体和画刷
                pData->hFont = CreateFontW(
                    24, 0, 0, 0, FW_NORMAL,
                    FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                    L"Segoe UI");

                pData->hBackgroundBrush = CreateSolidBrush(theme.SecondaryBackground.ToCOLORREF());
                pData->hBorderPen = CreatePen(PS_SOLID, 1, theme.ControlFrame.ToCOLORREF());
                pData->hProgressPen = CreatePen(PS_SOLID, 1, RGB(0, 160, 200));

                return 0;
            }

            case WM_PAINT:
            {
                if (!pData)
                    break;

                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);

                uint64_t current = pData->currentValue.load();
                uint64_t max = pData->maxValue.load();
                std::string message = pData->GetMessage();
                double progress = (max > 0) ? static_cast<double>(current) / static_cast<double>(max) * 100.0 : 0.0;

                RECT clientRect;
                GetClientRect(hwnd, &clientRect);
                int width = clientRect.right - clientRect.left;
                int height = clientRect.bottom - clientRect.top;

                HDC memDC = CreateCompatibleDC(hdc);
                HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
                HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

                FillRect(memDC, &clientRect, pData->hBackgroundBrush);

                HFONT oldFont = NULL;
                if (pData->hFont)
                {
                    oldFont = (HFONT)SelectObject(memDC, pData->hFont);
                }

                SetTextColor(memDC, theme.Text.ToCOLORREF());
                SetBkMode(memDC, TRANSPARENT);

                if (!message.empty())
                {
                    std::wstring wmessage = utf8ToWide(message);
                    RECT textRect = {20, 20, clientRect.right - 20, 50};
                    DrawTextW(memDC, wmessage.c_str(), -1, &textRect, DT_LEFT | DT_TOP | DT_WORDBREAK);
                }

                std::wstring progressText = L"Progress: " +
                                            std::to_wstring(static_cast<int>(progress)) + L"% (" +
                                            std::to_wstring(current) + L" / " + std::to_wstring(max) + L")";

                RECT percentRect = {20, 60, clientRect.right - 20, 90};
                DrawTextW(memDC, progressText.c_str(), -1, &percentRect, DT_LEFT | DT_TOP);

                RECT progressRect = {20, 100, clientRect.right - 20, 130};

                if (progress > 0)
                {
                    int fillWidth = static_cast<int>(
                        (progressRect.right - progressRect.left) * progress / 100.0);

                    RECT fillRect = progressRect;
                    fillRect.right = progressRect.left + fillWidth;

                    HBRUSH hProgressBrush = CreateSolidBrush(RGB(0, 180, 120));
                    FillRect(memDC, &fillRect, hProgressBrush);
                    DeleteObject(hProgressBrush);
                }

                HPEN oldPen = (HPEN)SelectObject(memDC, pData->hBorderPen);
                HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(HOLLOW_BRUSH));
                Rectangle(memDC, progressRect.left, progressRect.top,
                          progressRect.right, progressRect.bottom);

                if (pData->hFont)
                {
                    SelectObject(memDC, oldFont);
                }
                SelectObject(memDC, oldPen);
                SelectObject(memDC, oldBrush);

                BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

                SelectObject(memDC, oldBitmap);
                DeleteObject(memBitmap);
                DeleteDC(memDC);

                EndPaint(hwnd, &ps);
                return 0;
            }

            case WM_CLOSE:
                // 防止用户关闭对话框
                return 0;

            case WM_DESTROY:
                if (pData)
                {
                    pData->isFinished.store(true);

                    // 清理资源
                    if (pData->hFont)
                        DeleteObject(pData->hFont);
                    if (pData->hBackgroundBrush)
                        DeleteObject(pData->hBackgroundBrush);
                    if (pData->hBorderPen)
                        DeleteObject(pData->hBorderPen);
                    if (pData->hProgressPen)
                        DeleteObject(pData->hProgressPen);

                    pData->hwnd = nullptr;
                }

                PostQuitMessage(0);
                return 0;

            case WM_ERASEBKGND:
                return 1;

            default:
                return DefWindowProcW(hwnd, msg, wParam, lParam);
            }

            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        void DialogThreadProc()
        {
            if (!IsWindowClassRegistered())
            {
                threadRunning.store(false);
                return;
            }

            int screenWidth = GetSystemMetrics(SM_CXSCREEN);
            int screenHeight = GetSystemMetrics(SM_CYSCREEN);
            int windowWidth = 600;
            int windowHeight = 190;
            int x = (screenWidth - windowWidth) / 2;
            int y = (screenHeight - windowHeight) / 2;

            HWND hwnd = CreateWindowExW(
                WS_EX_DLGMODALFRAME,
                L"GL_Commdlg.DynamicProgressBarClass",
                dialogData->title.c_str(),
                WS_POPUP | WS_CAPTION | WS_SYSMENU,
                x, y, windowWidth, windowHeight,
                dialogData->hwndParent,
                nullptr,
                GetModuleHandleW(nullptr),
                dialogData.get());

            if (!hwnd)
            {
                threadRunning.store(false);
                return;
            }

            dialogData->hwnd = hwnd;

            ShowWindow(hwnd, SW_SHOW);
            UpdateWindow(hwnd);

            MSG msg;
            while (threadRunning.load())
            {

                {
                    std::unique_lock<std::mutex> lock(messageMutex);
                    messageCV.wait_for(lock, std::chrono::milliseconds(10),
                                       [this]()
                                       { return !messageQueue.empty(); });

                    while (!messageQueue.empty())
                    {
                        ProgressBarMessage message = messageQueue.front();
                        messageQueue.pop();
                        lock.unlock();

                        ProcessMessage(message);
                        lock.lock();
                    }
                }

                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
                {
                    if (msg.message == WM_QUIT)
                    {
                        threadRunning.store(false);
                        break;
                    }

                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }

                if (!IsWindow(hwnd) || dialogData->isFinished.load())
                {
                    threadRunning.store(false);
                }
            }

            if (IsWindow(hwnd))
            {
                DestroyWindow(hwnd);
            }
        }

        void ProcessMessage(const ProgressBarMessage &msg)
        {
            switch (msg.type)
            {
            case ProgressBarMessageType::SetValue:
                dialogData->currentValue.store(msg.currentValue);
                dialogData->maxValue.store(msg.maxValue);
                dialogData->UpdateMessage(msg.message);

                if (dialogData->hwnd && IsWindow(dialogData->hwnd))
                {
                    InvalidateRect(dialogData->hwnd, nullptr, TRUE);
                    UpdateWindow(dialogData->hwnd);
                }
                break;

            case ProgressBarMessageType::Close:
                dialogData->isFinished.store(true);
                threadRunning.store(false);
                if (dialogData->hwnd && IsWindow(dialogData->hwnd))
                {
                    DestroyWindow(dialogData->hwnd);
                }
                break;
            }
        }

        void PostMessageToThread(ProgressBarMessageType type, uint64_t current = 0,
                                 uint64_t max = 0, const std::string &message = "")
        {
            std::lock_guard<std::mutex> lock(messageMutex);
            messageQueue.emplace(type, current, max, message);
            messageCV.notify_one();
        }

    public:
        DynamicProgressBar(const std::string &title, const std::string &initialMessage, HWND hParent = nullptr)
            : dialogData(std::make_shared<DialogData>())
        {

            dialogData->hwndParent = hParent;
            dialogData->title = utf8ToWide(title);
            dialogData->UpdateMessage(initialMessage);

            threadRunning.store(true);
            dialogThread = std::make_unique<std::thread>([this]()
                                                         { DialogThreadProc(); });
            // dialogThread->detach();
        }

        ~DynamicProgressBar() override
        {
            Close();

            if (dialogThread && dialogThread->joinable())
            {
                dialogThread->join();
            }
        }

        DynamicProgressBar(DynamicProgressBar &&other) noexcept
            : dialogThread(std::move(other.dialogThread)), threadRunning(other.threadRunning.load()), dialogData(std::move(other.dialogData))
        {
        }

        DynamicProgressBar &operator=(DynamicProgressBar &&other) noexcept
        {
            if (this != &other)
            {
                Close();

                if (dialogThread && dialogThread->joinable())
                {
                    dialogThread->join();
                }

                dialogThread = std::move(other.dialogThread);
                threadRunning = other.threadRunning.load();
                dialogData = std::move(other.dialogData);
            }
            return *this;
        }

        void SetValue(uint64_t currentValue, uint64_t maxValue, const std::string &message)
        {
            if (!threadRunning.load() || dialogData->isFinished.load())
            {
                return;
            }

            PostMessageToThread(ProgressBarMessageType::SetValue, currentValue, maxValue, message);
        }

        void GetProgressInfo(uint64_t &current, uint64_t &max, std::string &message, double &progress)
        {
            if (!dialogData)
                return;

            current = dialogData->currentValue.load();
            max = dialogData->maxValue.load();
            message = dialogData->GetMessage();
            progress = (max > 0) ? static_cast<double>(current) / static_cast<double>(max) * 100.0 : 0.0;
        }

        void Show() override
        {
            if (dialogData)
            {
                ShowWindow(dialogData->hwnd, SW_SHOW);
            }
        }

        void Close() override
        {
            if (threadRunning.load() && !dialogData->isFinished.load())
            {
                PostMessageToThread(ProgressBarMessageType::Close);
            }
        }

        bool IsFinished() const
        {
            return dialogData ? dialogData->isFinished.load() : true;
        }

        HWND GetWindowHandle() const
        {
            return dialogData ? dialogData->hwnd : nullptr;
        }
    };

    /**
     * @brief Create a dynamic progress bar dialog instance
     * @brief 创建进度条动态对话框实例
     *
     * @param title Dialog title
     * @param title 对话框标题
     * @param initialMessage Initial prompt text inside the dialog
     * @param initialMessage 初始时对话框内的提示文本
     * @param hParent Parent window handle
     * @param hParent 父窗口句柄
     * @return The created dynamic progress bar instance
     * @return 创建的进度条动态对话框实例
     */
    DynamicProgressBar CreateDynamicProgressBar(const std::string &title,
                                                const std::string &initialMessage,
                                                HWND hParent = nullptr)
    {
        return DynamicProgressBar(title, initialMessage, hParent);
    }

    namespace
    {
        // 滑动条消息类型
        enum class SliderMessageType
        {
            SetValue,
            Close,
            SetRange,
            SetCallback
        };

        // 滑动条回调消息类型
        enum class DynamicSliderCallbackMessageType
        {
            Dragging,
            Released
        };

        // 滑动条消息结构
        struct SliderMessage
        {
            SliderMessageType type;
            int value;
            int minValue;
            int maxValue;
            std::function<int(DynamicSliderCallbackMessageType, int)> callback;

            SliderMessage(SliderMessageType t, int v = 0, int min = 0, int max = 100,
                          std::function<int(DynamicSliderCallbackMessageType, int)> cb = nullptr)
                : type(t), value(v), minValue(min), maxValue(max), callback(cb) {}
        };
    }

    // 动态滑动条接口类
    class DynamicSlider : public DynamicDialogInterface
    {
    private:
        // 对话框数据
        struct DialogData
        {
            DynamicSlider *parentObject;

            HWND hwnd = nullptr;
            HWND hwndParent = nullptr;
            std::wstring title;
            std::atomic<int> currentValue{0};
            std::atomic<int> minValue{0};
            std::atomic<int> maxValue{100};
            std::atomic<bool> isFinished{false};
            std::atomic<bool> isDragging{false};
            std::string currentMessage;
            std::mutex dataMutex;

            // 回调函数
            std::function<int(DynamicSliderCallbackMessageType, int)> callback;
            std::mutex callbackMutex;

            // UI资源
            HFONT hFont = nullptr;
            HBRUSH hBackgroundBrush = nullptr;
            HBRUSH hSliderBgBrush = nullptr;
            HBRUSH hSliderThumbBrush = nullptr;
            HPEN hBorderPen = nullptr;
            HPEN hThumbPen = nullptr;

            // 滑条区域
            RECT sliderRect = {0, 0, 0, 0};
            int thumbSize = 20;
            HWND hTooltip = nullptr;

            void UpdateMessage(const std::string &msg)
            {
                std::lock_guard<std::mutex> lock(dataMutex);
                currentMessage = msg;
            }

            std::string GetMessage()
            {
                std::lock_guard<std::mutex> lock(dataMutex);
                return currentMessage;
            }

            void SetCallback(std::function<int(DynamicSliderCallbackMessageType, int)> cb)
            {
                std::lock_guard<std::mutex> lock(callbackMutex);
                callback = cb;
            }

            int CallCallback(DynamicSliderCallbackMessageType type, int value)
            {
                std::lock_guard<std::mutex> lock(callbackMutex);
                if (callback)
                {
                    return callback(type, value);
                }
                else
                {
                    return value;
                }
            }
        };

        // 线程控制
        std::unique_ptr<std::thread> dialogThread;
        std::atomic<bool> threadRunning{false};
        std::condition_variable messageCV;
        std::mutex messageMutex;
        std::queue<SliderMessage> messageQueue;

        // 对话框数据
        std::shared_ptr<DialogData> dialogData;

        // Tooltip 工具函数（使用 Controls::Tooltip 公共 API）
        static void ShowTooltip(HWND hwndSlider, int value)
        {
            DialogData *pData = reinterpret_cast<DialogData *>(GetWindowLongPtrW(hwndSlider, GWLP_USERDATA));
            if (!pData)
                return;

            Controls::Tooltip::Destroy(pData->hTooltip);
            pData->hTooltip = nullptr;

            std::wstring text = std::to_wstring(value);
            pData->hTooltip = Controls::Tooltip::Create(hwndSlider, text, pData->hFont);
            if (!pData->hTooltip)
                return;

            int thumbX = GetThumbPosition(hwndSlider);
            POINT pt = {thumbX + pData->thumbSize / 2, pData->sliderRect.top};
            ClientToScreen(hwndSlider, &pt);

            RECT rc;
            GetWindowRect(pData->hTooltip, &rc);
            int tw = rc.right - rc.left;
            int th = rc.bottom - rc.top;

            Controls::Tooltip::SetPosition(pData->hTooltip, pt.x - tw / 2, pt.y - th - 14, tw, th);
            ShowWindow(pData->hTooltip, SW_SHOW);
        }

        static void UpdateTooltip(HWND hwndSlider, int value)
        {
            DialogData *pData = reinterpret_cast<DialogData *>(GetWindowLongPtrW(hwndSlider, GWLP_USERDATA));
            if (!pData || !pData->hTooltip || !IsWindow(pData->hTooltip))
                return;

            std::wstring text = std::to_wstring(value);
            Controls::Tooltip::SetText(pData->hTooltip, text, pData->hFont);

            int thumbX = GetThumbPosition(hwndSlider);
            POINT pt = {thumbX + pData->thumbSize / 2, pData->sliderRect.top};
            ClientToScreen(hwndSlider, &pt);

            RECT rc;
            GetWindowRect(pData->hTooltip, &rc);
            int tw = rc.right - rc.left;
            int th = rc.bottom - rc.top;

            Controls::Tooltip::SetPosition(pData->hTooltip, pt.x - tw / 2, pt.y - th - 14, tw, th);
        }

        static void HideTooltip(HWND hwndSlider)
        {
            DialogData *pData = reinterpret_cast<DialogData *>(GetWindowLongPtrW(hwndSlider, GWLP_USERDATA));
            if (!pData)
                return;
            Controls::Tooltip::Destroy(pData->hTooltip);
            pData->hTooltip = nullptr;
        }

        // 窗口类注册状态
        static bool IsWindowClassRegistered()
        {
            static std::once_flag registerFlag;
            static bool registered = false;

            std::call_once(registerFlag, []()
                           {
            WNDCLASSEXW wc = {0};
            wc.cbSize = sizeof(WNDCLASSEXW);
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = WindowProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            wc.lpszClassName = L"GL_Commdlg.DynamicSliderClass";
            
            registered = RegisterClassExW(&wc) != 0; });

            return registered;
        }

        // 窗口过程（静态，通过用户数据获取实例）
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            if (msg == WM_NCCREATE)
            {
                CREATESTRUCT *pCreate = reinterpret_cast<CREATESTRUCT *>(lParam);
                DialogData *pData = reinterpret_cast<DialogData *>(pCreate->lpCreateParams);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pData));
                return DefWindowProcW(hwnd, msg, wParam, lParam);
            }

            DialogData *pData = reinterpret_cast<DialogData *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

            switch (msg)
            {
            case WM_CREATE:
            {

                Controls::InitWindowColor(hwnd);

                if (!pData)
                    return -1;

                pData->hwnd = hwnd;

                // 创建字体和画刷
                pData->hFont = CreateFontW(
                    24, 0, 0, 0, FW_NORMAL,
                    FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                    L"Segoe UI");

                pData->hBackgroundBrush = CreateSolidBrush(theme.SecondaryBackground.ToCOLORREF());
                pData->hSliderBgBrush = CreateSolidBrush(theme.ControlFrame.ToCOLORREF());
                pData->hSliderThumbBrush = CreateSolidBrush(theme.PrimaryForeground.ToCOLORREF());
                pData->hBorderPen = CreatePen(PS_SOLID, 1, theme.ControlFrame.ToCOLORREF());
                pData->hThumbPen = CreatePen(PS_SOLID, 1, theme.ControlFrame.ToCOLORREF());

                return 0;
            }

            case WM_PAINT:
            {
                if (!pData)
                    break;

                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);

                int minValue = pData->minValue.load();
                int maxValue = pData->maxValue.load();
                std::string message = pData->GetMessage();

                RECT clientRect;
                GetClientRect(hwnd, &clientRect);
                int width = clientRect.right - clientRect.left;
                int height = clientRect.bottom - clientRect.top;

                HDC memDC = CreateCompatibleDC(hdc);
                HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
                HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

                FillRect(memDC, &clientRect, pData->hBackgroundBrush);

                HFONT oldFont = NULL;
                if (pData->hFont)
                {
                    oldFont = (HFONT)SelectObject(memDC, pData->hFont);
                }

                SetTextColor(memDC, theme.Text.ToCOLORREF());
                SetBkMode(memDC, TRANSPARENT);

                if (!message.empty())
                {
                    std::wstring wmessage = utf8ToWide(message);
                    RECT textRect = {20, 20, clientRect.right - 20, 60};
                    DrawTextW(memDC, wmessage.c_str(), -1, &textRect, DT_LEFT | DT_TOP | DT_WORDBREAK);
                }

                pData->sliderRect.left = 20;
                pData->sliderRect.right = clientRect.right - 20;
                pData->sliderRect.top = 70;
                pData->sliderRect.bottom = 100;

                RECT sliderBgRect = pData->sliderRect;
                FillRect(memDC, &sliderBgRect, pData->hSliderBgBrush);

                HPEN oldPen = (HPEN)SelectObject(memDC, pData->hBorderPen);
                HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(HOLLOW_BRUSH));
                Rectangle(memDC, sliderBgRect.left, sliderBgRect.top,
                          sliderBgRect.right, sliderBgRect.bottom);

                int numTicks = 5;
                for (int i = 0; i <= numTicks; i++)
                {
                    float ratio = static_cast<float>(i) / static_cast<float>(numTicks);
                    int tickX = pData->sliderRect.left +
                                static_cast<int>(ratio * (pData->sliderRect.right - pData->sliderRect.left));

                    MoveToEx(memDC, tickX, pData->sliderRect.bottom + 2, NULL);
                    LineTo(memDC, tickX, pData->sliderRect.bottom + 8);

                    int tickValue = minValue + static_cast<int>(ratio * (maxValue - minValue));
                    std::wstring tickText = std::to_wstring(tickValue);
                    RECT tickRect = {tickX - 40, pData->sliderRect.bottom + 10, tickX + 40, pData->sliderRect.bottom + 30};
                    DrawTextW(memDC, tickText.c_str(), -1, &tickRect, DT_CENTER | DT_TOP);
                }

                int thumbX = GetThumbPosition(hwnd);
                int thumbY = (pData->sliderRect.top + pData->sliderRect.bottom) / 2;
                int thumbRadius = pData->thumbSize / 2;

                RECT thumbRect = {
                    thumbX,
                    thumbY - thumbRadius - 10,
                    thumbX + pData->thumbSize,
                    thumbY + thumbRadius + 10};

                FillRect(memDC, &thumbRect, pData->hSliderThumbBrush);

                // SelectObject(memDC, pData->hSliderThumbBrush);
                // SelectObject(memDC, pData->hThumbPen);
                // Ellipse(memDC,thumbRect.left, thumbRect.top, thumbRect.right, thumbRect.bottom);

                Rectangle(memDC, thumbRect.left, thumbRect.top, thumbRect.right, thumbRect.bottom);

                if (oldFont)
                {
                    SelectObject(memDC, oldFont);
                }
                SelectObject(memDC, oldPen);
                SelectObject(memDC, oldBrush);

                BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

                SelectObject(memDC, oldBitmap);
                DeleteObject(memBitmap);
                DeleteDC(memDC);

                EndPaint(hwnd, &ps);
                return 0;
            }

            case WM_LBUTTONDOWN:
            {
                if (!pData)
                    break;

                int mouseX = GET_X_LPARAM(lParam);
                int mouseY = GET_Y_LPARAM(lParam);

                // 检查是否点击在滑块上
                int thumbX = GetThumbPosition(hwnd);
                int thumbY = (pData->sliderRect.top + pData->sliderRect.bottom) / 2;
                int thumbRadius = pData->thumbSize / 2;

                RECT thumbRect = {
                    thumbX,
                    thumbY - thumbRadius,
                    thumbX + pData->thumbSize,
                    thumbY + thumbRadius};

                // 检查是否点击在滑条区域内
                POINT pt = {mouseX, mouseY};
                if (PtInRect(&thumbRect, pt) ||
                    (mouseY >= pData->sliderRect.top && mouseY <= pData->sliderRect.bottom &&
                     mouseX >= pData->sliderRect.left && mouseX <= pData->sliderRect.right))
                {

                    pData->isDragging.store(true);
                    SetCapture(hwnd);
                    UpdateValueFromMouse(hwnd, mouseX);
                    int newVal = pData->currentValue.load();
                    ShowTooltip(hwnd, newVal);
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                }
                break;
            }
            case WM_MOUSEMOVE:
            {
                if (!pData)
                    break;

                if (pData->isDragging.load())
                {
                    int mouseX = GET_X_LPARAM(lParam);
                    UpdateValueFromMouse(hwnd, mouseX);
                    int curVal = pData->currentValue.load();
                    UpdateTooltip(hwnd, curVal);
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                }
                break;
            }

            case WM_LBUTTONUP:
            {
                if (!pData)
                    break;

                if (pData->isDragging.load())
                {
                    HideTooltip(hwnd);
                    pData->isDragging.store(false);
                    ReleaseCapture();

                    // 触发释放回调
                    pData->currentValue.store(pData->CallCallback(DynamicSliderCallbackMessageType::Released,
                                                                  pData->currentValue.load()));

                    return 0;
                }
                break;
            }

            case WM_CLOSE:
                if (pData)
                {
                    pData->parentObject->PostMessageToThread(SliderMessageType::Close);
                }
                return 0;

            case WM_DESTROY:
                if (pData)
                {
                    pData->isFinished.store(true);

                    // 清理资源
                    Controls::Tooltip::Destroy(pData->hTooltip);
                    pData->hTooltip = nullptr;
                    if (pData->hFont)
                        DeleteObject(pData->hFont);
                    if (pData->hBackgroundBrush)
                        DeleteObject(pData->hBackgroundBrush);
                    if (pData->hSliderBgBrush)
                        DeleteObject(pData->hSliderBgBrush);
                    if (pData->hSliderThumbBrush)
                        DeleteObject(pData->hSliderThumbBrush);
                    if (pData->hBorderPen)
                        DeleteObject(pData->hBorderPen);
                    if (pData->hThumbPen)
                        DeleteObject(pData->hThumbPen);

                    pData->hwnd = nullptr;
                }

                PostQuitMessage(0);
                return 0;

            case WM_ERASEBKGND:
                return 1;

            default:
                return DefWindowProcW(hwnd, msg, wParam, lParam);
            }

            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        // 辅助函数：获取滑块位置
        static int GetThumbPosition(HWND hwnd)
        {
            DialogData *pData = reinterpret_cast<DialogData *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (!pData)
                return 0;

            int value = pData->currentValue.load();
            int min = pData->minValue.load();
            int max = pData->maxValue.load();

            if (max <= min)
                return pData->sliderRect.left;

            float ratio = static_cast<float>(value - min) / static_cast<float>(max - min);
            int range = pData->sliderRect.right - pData->sliderRect.left - pData->thumbSize;
            return pData->sliderRect.left + static_cast<int>(ratio * range);
        }

        // 辅助函数：根据鼠标位置更新值
        static void UpdateValueFromMouse(HWND hwnd, int mouseX)
        {

            DialogData *pData = reinterpret_cast<DialogData *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (!pData)
                return;

            RECT sliderRect = pData->sliderRect;
            int thumbSize = pData->thumbSize;

            // 确保鼠标在滑条区域内
            mouseX = std::max<LONG>(sliderRect.left, std::min<LONG>(sliderRect.right - thumbSize, mouseX));

            int range = sliderRect.right - sliderRect.left - thumbSize;
            if (range <= 0)
                return;

            float ratio = static_cast<float>(mouseX - sliderRect.left) / static_cast<float>(range);

            int minValue = pData->minValue.load();
            int maxValue = pData->maxValue.load();
            int newValue = minValue + static_cast<int>(ratio * (maxValue - minValue));

            // 确保值在范围内
            newValue = std::max(minValue, std::min(maxValue, newValue));

            // 更新值
            newValue = pData->CallCallback(DynamicSliderCallbackMessageType::Dragging, newValue);
            pData->currentValue.store(newValue);
        }

        void DialogThreadProc()
        {
            if (!IsWindowClassRegistered())
            {
                threadRunning.store(false);
                return;
            }

            int screenWidth = GetSystemMetrics(SM_CXSCREEN);
            int screenHeight = GetSystemMetrics(SM_CYSCREEN);
            int windowWidth = 600;
            int windowHeight = 190;
            int x = (screenWidth - windowWidth) / 2;
            int y = (screenHeight - windowHeight) / 2;

            HWND hwnd = CreateWindowExW(
                WS_EX_DLGMODALFRAME,
                L"GL_Commdlg.DynamicSliderClass",
                dialogData->title.c_str(),
                WS_POPUP | WS_CAPTION | WS_SYSMENU,
                x, y, windowWidth, windowHeight,
                dialogData->hwndParent,
                nullptr,
                GetModuleHandleW(nullptr),
                dialogData.get());

            if (!hwnd)
            {
                threadRunning.store(false);
                return;
            }

            dialogData->hwnd = hwnd;

            ShowWindow(hwnd, SW_SHOW);
            UpdateWindow(hwnd);

            MSG msg;
            while (threadRunning.load())
            {

                {
                    std::unique_lock<std::mutex> lock(messageMutex);
                    messageCV.wait_for(lock, std::chrono::milliseconds(10),
                                       [this]()
                                       { return !messageQueue.empty(); });

                    while (!messageQueue.empty())
                    {
                        SliderMessage message = messageQueue.front();
                        messageQueue.pop();
                        lock.unlock();

                        ProcessMessage(message);
                        lock.lock();
                    }
                }

                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
                {
                    if (msg.message == WM_QUIT)
                    {
                        threadRunning.store(false);
                        break;
                    }

                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }

                if (!IsWindow(hwnd) || dialogData->isFinished.load())
                {
                    threadRunning.store(false);
                }
            }

            if (IsWindow(hwnd))
            {
                DestroyWindow(hwnd);
            }
        }

        void ProcessMessage(const SliderMessage &msg)
        {
            switch (msg.type)
            {
            case SliderMessageType::SetValue:
                dialogData->currentValue.store(msg.value);

                if (dialogData->hwnd && IsWindow(dialogData->hwnd))
                {
                    InvalidateRect(dialogData->hwnd, nullptr, TRUE);
                    UpdateWindow(dialogData->hwnd);
                }
                break;

            case SliderMessageType::SetRange:
            {
                dialogData->minValue.store(msg.minValue);
                dialogData->maxValue.store(msg.maxValue);

                // 确保当前值在范围内
                int current = dialogData->currentValue.load();
                if (current < msg.minValue)
                    dialogData->currentValue.store(msg.minValue);
                if (current > msg.maxValue)
                    dialogData->currentValue.store(msg.maxValue);

                if (dialogData->hwnd && IsWindow(dialogData->hwnd))
                {
                    InvalidateRect(dialogData->hwnd, nullptr, TRUE);
                    UpdateWindow(dialogData->hwnd);
                }
                break;
            }
            case SliderMessageType::SetCallback:
                dialogData->SetCallback(msg.callback);
                break;

            case SliderMessageType::Close:
                dialogData->isFinished.store(true);
                threadRunning.store(false);
                if (dialogData->hwnd && IsWindow(dialogData->hwnd))
                {
                    DestroyWindow(dialogData->hwnd);
                }
                break;
            }
        }

        void PostMessageToThread(SliderMessageType type, int value = 0,
                                 int minValue = 0, int maxValue = 100,
                                 std::function<int(DynamicSliderCallbackMessageType, int)> callback = nullptr)
        {
            std::lock_guard<std::mutex> lock(messageMutex);
            messageQueue.emplace(type, value, minValue, maxValue, callback);
            messageCV.notify_one();
        }

    public:
        DynamicSlider(const std::string &title, const std::string &initialMessage,
                      int minValue, int maxValue, int initialValue,
                      std::function<int(DynamicSliderCallbackMessageType, int)> callback,
                      HWND hParent = nullptr)
            : dialogData(std::make_shared<DialogData>())
        {

            dialogData->parentObject = this;
            dialogData->hwndParent = hParent;
            dialogData->title = utf8ToWide(title);
            dialogData->UpdateMessage(initialMessage);
            dialogData->minValue.store(minValue);
            dialogData->maxValue.store(maxValue);
            dialogData->currentValue.store(initialValue);
            dialogData->SetCallback(callback);

            threadRunning.store(true);
            dialogThread = std::make_unique<std::thread>([this]()
                                                         { DialogThreadProc(); });
        }

        ~DynamicSlider() override
        {
            Close();

            if (dialogThread && dialogThread->joinable())
            {
                dialogThread->join();
            }
        }

        DynamicSlider(DynamicSlider &&other) noexcept
            : dialogThread(std::move(other.dialogThread)), threadRunning(other.threadRunning.load()), dialogData(std::move(other.dialogData))
        {
        }

        DynamicSlider &operator=(DynamicSlider &&other) noexcept
        {
            if (this != &other)
            {
                Close();

                if (dialogThread && dialogThread->joinable())
                {
                    dialogThread->join();
                }

                dialogThread = std::move(other.dialogThread);
                threadRunning = other.threadRunning.load();
                dialogData = std::move(other.dialogData);
            }
            return *this;
        }

        void SetValue(int value)
        {
            if (!threadRunning.load() || dialogData->isFinished.load())
            {
                return;
            }

            PostMessageToThread(SliderMessageType::SetValue, value);
        }

        void SetRange(int minValue, int maxValue)
        {
            if (!threadRunning.load() || dialogData->isFinished.load())
            {
                return;
            }

            PostMessageToThread(SliderMessageType::SetRange, 0, minValue, maxValue);
        }

        void SetCallback(std::function<int(DynamicSliderCallbackMessageType, int)> callback)
        {
            if (!threadRunning.load() || dialogData->isFinished.load())
            {
                return;
            }

            PostMessageToThread(SliderMessageType::SetCallback, 0, 0, 0, std::move(callback));
        }

        void GetSliderInfo(int &current, int &min, int &max, std::string &message)
        {
            if (!dialogData)
                return;

            current = dialogData->currentValue.load();
            min = dialogData->minValue.load();
            max = dialogData->maxValue.load();
            message = dialogData->GetMessage();
        }

        void Show() override
        {
            if (dialogData)
            {
                ShowWindow(dialogData->hwnd, SW_SHOW);
            }
        }

        void Close() override
        {
            if (threadRunning.load() && !dialogData->isFinished.load())
            {
                PostMessageToThread(SliderMessageType::Close);
            }
        }

        bool IsFinished() const
        {
            return dialogData ? dialogData->isFinished.load() : true;
        }

        HWND GetWindowHandle() const
        {
            return dialogData ? dialogData->hwnd : nullptr;
        }

        bool IsDragging() const
        {
            return dialogData ? dialogData->isDragging.load() : false;
        }
    };

    /**
     * @brief Create a dynamic slider dialog instance
     * @brief 创建滑动条动态对话框实例
     *
     * @param title Dialog title
     * @param title 对话框标题
     * @param initialMessage Initial prompt text inside the dialog
     * @param initialMessage 初始时对话框内的提示文本
     * @param minValue Minimum slider value
     * @param minValue 滑动条最小值
     * @param maxValue Maximum slider value
     * @param maxValue 滑动条最大值
     * @param initialValue Initial slider value
     * @param callbackOnValueChange Callback function for value changes; parameters are event type and current value
     * @param callbackOnValueChange 值改变时的回调函数，参数为事件类型和当前值
     * @param hParent Parent window handle
     * @param hParent 父窗口句柄
     * @return The created dynamic slider instance
     * @return 创建的滑动条动态对话框实例
     */
    DynamicSlider CreateDynamicSlider(const std::string &title,
                                      const std::string &initialMessage,
                                      int minValue,
                                      int maxValue,
                                      int initialValue,
                                      std::function<int(DynamicSliderCallbackMessageType, int)> callbackOnValueChange,
                                      HWND hParent = nullptr)
    {
        return DynamicSlider(title, initialMessage, minValue, maxValue, initialValue, callbackOnValueChange, hParent);
    }

    // ─── 动态颜色选择器 ─────────────────────────────────────────────

    namespace
    {
        // 颜色工具
        struct HSL
        {
            double h, s, l;
        };

        HSL RgbToHsl(int r, int g, int b)
        {
            double rd = r / 255.0, gd = g / 255.0, bd = b / 255.0;
            double mx = (std::max)({rd, gd, bd}), mn = (std::min)({rd, gd, bd}), delta = mx - mn;
            HSL out = {0, 0, (mx + mn) / 2.0};
            if (delta > 1e-10)
            {
                out.s = out.l > 0.5 ? delta / (2.0 - mx - mn) : delta / (mx + mn);
                if (mx == rd)
                    out.h = 60.0 * std::fmod((gd - bd) / delta, 6.0);
                else if (mx == gd)
                    out.h = 60.0 * ((bd - rd) / delta + 2.0);
                else
                    out.h = 60.0 * ((rd - gd) / delta + 4.0);
                if (out.h < 0)
                    out.h += 360.0;
            }
            return out;
        }

        uint8_t H2R(double p, double q, double t)
        {
            if (t < 0)
                t += 1.0;
            if (t > 1)
                t -= 1.0;
            if (t < 1.0 / 6)
                return (uint8_t)((p + (q - p) * 6.0 * t) * 255.0);
            if (t < 1.0 / 2)
                return (uint8_t)(q * 255.0);
            if (t < 2.0 / 3)
                return (uint8_t)((p + (q - p) * (2.0 / 3.0 - t) * 6.0) * 255.0);
            return (uint8_t)(p * 255.0);
        }

        void HslToRgb(double h, double s, double l, int &r, int &g, int &b)
        {
            if (s < 1e-10)
            {
                r = g = b = (int)(l * 255.0);
                return;
            }
            double q = l < 0.5 ? l * (1.0 + s) : l + s - l * s, p = 2.0 * l - q, hf = h / 360.0;
            r = H2R(p, q, hf + 1.0 / 3);
            g = H2R(p, q, hf);
            b = H2R(p, q, hf - 1.0 / 3);
        }
    }

    /**
     * @brief A non-blocking dynamic color picker dialog
     * @brief 非阻塞的动态颜色选择器对话框
     *
     * Runs in a separate thread and provides a full-featured color picker with
     * HSL colour wheel + hue/alpha sliders + numeric RGBA/HSL/HEX inputs.
     */
    class DynamicColorPicker : public DynamicDialogInterface
    {
    public:
        enum class DynamicColorCallbackMessageType
        {
            Dragging,
            Released
        };

    private:
        enum class ColorPickerMessageType
        {
            SetColor,
            SetCallback,
            Close
        };

        struct ColorPickerMessage
        {
            ColorPickerMessageType type;
            int r, g, b, a;
            std::function<ColorRGBA(DynamicColorCallbackMessageType, ColorRGBA)> callback;

            ColorPickerMessage(ColorPickerMessageType t, int r_ = 0, int g_ = 0, int b_ = 0, int a_ = 255)
                : type(t), r(r_), g(g_), b(b_), a(a_) {}
            ColorPickerMessage(ColorPickerMessageType t,
                               std::function<ColorRGBA(DynamicColorCallbackMessageType, ColorRGBA)> cb)
                : type(t), r(0), g(0), b(0), a(255), callback(std::move(cb)) {}
        };

        struct DialogData
        {
            DynamicColorPicker *parent;
            HWND hwnd = nullptr, hwndParent = nullptr;
            std::wstring title;

            std::atomic<int> curR{255}, curG{255}, curB{255}, curA{255};
            std::atomic<int> oldR{255}, oldG{255}, oldB{255}, oldA{255};
            std::atomic<double> hue{0}, sat{0.5}, light{0.5};
            std::atomic<bool> enableAlpha{true};
            std::atomic<bool> isFinished{false}, draggingCanvas{false}, draggingHue{false}, draggingAlpha{false};

            // 回调函数
            std::function<ColorRGBA(DynamicColorCallbackMessageType, ColorRGBA)> callback;
            std::mutex callbackMutex;

            void SetCallback(std::function<ColorRGBA(DynamicColorCallbackMessageType, ColorRGBA)> cb)
            {
                std::lock_guard<std::mutex> lock(callbackMutex);
                callback = std::move(cb);
            }

            ColorRGBA CallCallback(DynamicColorCallbackMessageType type)
            {
                ColorRGBA col((uint8_t)curR.load(), (uint8_t)curG.load(), (uint8_t)curB.load(), (uint8_t)curA.load());
                std::lock_guard<std::mutex> lock(callbackMutex);
                if (callback)
                {
                    ColorRGBA result = callback(type, col);
                    curR.store(result.r);
                    curG.store(result.g);
                    curB.store(result.b);
                    curA.store(result.a);
                    return result;
                }
                return col;
            }

            // ── 内联字段滑块 ──
            struct FieldSlider {
                RECT rect = {};           // 滑块区域（相对于客户区）
                int displayMin = 0, displayMax = 255;
                std::atomic<int>* targetInt = nullptr;
                std::atomic<double>* targetDouble = nullptr;
                double doubleScale = 1.0; // display = doubleValue * doubleScale
                bool dragging = false;
                int thumbSize = 8;

                int GetDisplayValue() const {
                    if(targetDouble) return (int)(targetDouble->load() * doubleScale);
                    return targetInt ? targetInt->load() : 0;
                }
                void SetFromDisplay(int displayVal) const {
                    if(targetDouble) targetDouble->store(displayVal / doubleScale);
                    else if(targetInt) targetInt->store(displayVal);
                }
                void SetupInt(int mn, int mx, std::atomic<int>* t, const RECT &rc) {
                    displayMin=mn; displayMax=mx; targetInt=t; targetDouble=nullptr; rect=rc;
                }
                void SetupDouble(int mn, int mx, std::atomic<double>* t, double scale, const RECT &rc) {
                    displayMin=mn; displayMax=mx; targetDouble=t; targetInt=nullptr; doubleScale=scale; rect=rc;
                }

                int GetThumbPos() const {
                    int sw = rect.right - rect.left;
                    int val = GetDisplayValue();
                    float ratio = (displayMax > displayMin) ? (float)(val - displayMin) / (float)(displayMax - displayMin) : 0.0f;
                    return rect.left + (int)(ratio * (sw - thumbSize));
                }
                int DisplayFromPos(int mouseX) const {
                    int sw = rect.right - rect.left;
                    int range = sw - thumbSize;
                    if(range <= 0) return displayMin;
                    int left = rect.left;
                    int cx = (std::max)(left, (std::min)(left + range, mouseX));
                    float ratio = (float)(cx - left) / (float)range;
                    return displayMin + (int)(ratio * (displayMax - displayMin));
                }
            } sliderR, sliderG, sliderB, sliderA, sliderH, sliderS, sliderL;

            // 控件句柄
            HWND hR = nullptr, hG = nullptr, hB = nullptr, hA = nullptr;
            HWND hH = nullptr, hS = nullptr, hL = nullptr, hHex = nullptr;
            HWND hBtnOK = nullptr, hBtnCancel = nullptr, hBtnEye = nullptr;

            HFONT hFont = nullptr;
            HBITMAP hCachedWheel = nullptr;
            HDC hCachedDC = nullptr;
            double cachedLight = -1.0;
            HBRUSH hCheckerBrush = nullptr;
            HDC hMemDC = nullptr;
            HBITMAP hMemBmp = nullptr;
            int memW = 0, memH = 0;
            bool updatingInputs = false;

            // ── Eyedropper (screen color picker) ──
            std::atomic<bool> eyeDropperMode{false};
            int eyePreviewR = 0, eyePreviewG = 0, eyePreviewB = 0;
            HWND hOverlay = nullptr;
            HWND hEyePreview = nullptr;
            HWND hMagnifier = nullptr;
            std::atomic<int> zoomLevel{2};

            void SyncFromRgbToInputs()
            {
                if (updatingInputs)
                    return;
                updatingInputs = true;
                wchar_t buf[32];
                int r2 = curR.load(), g2 = curG.load(), b2 = curB.load(), a2 = curA.load();
                double h2 = hue.load(), s2 = sat.load(), l2 = light.load();
                swprintf(buf, L"%d", r2);
                if (hR)
                    SetWindowTextW(hR, buf);
                swprintf(buf, L"%d", g2);
                if (hG)
                    SetWindowTextW(hG, buf);
                swprintf(buf, L"%d", b2);
                if (hB)
                    SetWindowTextW(hB, buf);
                swprintf(buf, L"%d", a2);
                if (hA)
                    SetWindowTextW(hA, buf);
                swprintf(buf, L"%d", (int)h2);
                if (hH)
                    SetWindowTextW(hH, buf);
                swprintf(buf, L"%d", (int)(s2 * 100));
                if (hS)
                    SetWindowTextW(hS, buf);
                swprintf(buf, L"%d", (int)(l2 * 100));
                if (hL)
                    SetWindowTextW(hL, buf);
                swprintf(buf, L"%02X%02X%02X", r2, g2, b2);
                if (hHex)
                    SetWindowTextW(hHex, buf);
                updatingInputs = false;
            }
            void ReadInputsToRgb()
            {
                if (updatingInputs)
                    return;
                updatingInputs = true;
                wchar_t buf[32];
                if (hR && GetWindowTextW(hR, buf, 32))
                    curR.store((std::max)(0, (std::min)(255, _wtoi(buf))));
                if (hG && GetWindowTextW(hG, buf, 32))
                    curG.store((std::max)(0, (std::min)(255, _wtoi(buf))));
                if (hB && GetWindowTextW(hB, buf, 32))
                    curB.store((std::max)(0, (std::min)(255, _wtoi(buf))));
                if (hA && GetWindowTextW(hA, buf, 32))
                    curA.store((std::max)(0, (std::min)(255, _wtoi(buf))));
                if (hHex)
                {
                    GetWindowTextW(hHex, buf, 32);
                    const wchar_t *p = buf;
                    if (*p == L'#')
                        p++;
                    unsigned int val = 0;
                    swscanf_s(p, L"%x", &val);
                    if (wcslen(p) >= 6)
                    {
                        curR.store((val >> 16) & 0xFF);
                        curG.store((val >> 8) & 0xFF);
                        curB.store(val & 0xFF);
                    }
                }
                int r3 = curR.load(), g3 = curG.load(), b3 = curB.load();
                auto hsl2 = RgbToHsl(r3, g3, b3);
                hue.store(hsl2.h);
                sat.store(hsl2.s);
                light.store(hsl2.l);
                swprintf(buf, L"%d", (int)hsl2.h);
                if (hH)
                    SetWindowTextW(hH, buf);
                swprintf(buf, L"%d", (int)(hsl2.s * 100));
                if (hS)
                    SetWindowTextW(hS, buf);
                swprintf(buf, L"%d", (int)(hsl2.l * 100));
                if (hL)
                    SetWindowTextW(hL, buf);
                swprintf(buf, L"%02X%02X%02X", r3, g3, b3);
                if (hHex)
                    SetWindowTextW(hHex, buf);
                updatingInputs = false;
            }
        };

        std::unique_ptr<std::thread> dialogThread;
        std::atomic<bool> threadRunning{false};
        std::condition_variable messageCV;
        std::mutex messageMutex;
        std::queue<ColorPickerMessage> messageQueue;
        std::shared_ptr<DialogData> dialogData;

        static bool IsWindowClassRegistered()
        {
            static std::once_flag f;
            static bool r = false;
            std::call_once(f, []
                           {
                WNDCLASSEXW wc = {sizeof(wc), CS_HREDRAW|CS_VREDRAW, WindowProc,
                   0,0, GetModuleHandleW(nullptr), nullptr, LoadCursor(nullptr,IDC_ARROW),
                   (HBRUSH)(COLOR_WINDOW+1), nullptr, L"DynamicColorPickerClass", nullptr};
                r = RegisterClassExW(&wc) != 0;

                // Register overlay class for eyedropper
                WNDCLASSEXW oc = {sizeof(oc), CS_HREDRAW|CS_VREDRAW, EyeOverlayProc,
                   0,0, GetModuleHandleW(nullptr), nullptr, LoadCursor(nullptr,IDC_CROSS),
                   (HBRUSH)(COLOR_WINDOW+1), nullptr, L"GL_Commdlg.EyeOverlayClass", nullptr};
                RegisterClassExW(&oc);

                // Register preview class for eyedropper tooltip
                WNDCLASSEXW pc = {sizeof(pc), CS_HREDRAW|CS_VREDRAW|CS_SAVEBITS, EyePreviewProc,
                   0,0, GetModuleHandleW(nullptr), nullptr, LoadCursor(nullptr,IDC_ARROW),
                   (HBRUSH)(COLOR_WINDOW+1), nullptr, L"GL_Commdlg.EyePreviewClass", nullptr};
                RegisterClassExW(&pc);

                // Register magnifier class for zoomed pixel view
                WNDCLASSEXW mc = {sizeof(mc), CS_HREDRAW|CS_VREDRAW|CS_SAVEBITS, EyeMagnifierProc,
                   0,0, GetModuleHandleW(nullptr), nullptr, LoadCursor(nullptr,IDC_ARROW),
                   (HBRUSH)(COLOR_WINDOW+1), nullptr, L"GL_Commdlg.EyeMagnifierClass", nullptr};
                RegisterClassExW(&mc); });
            return r;
        }

        // ── Update magnifier content & position ──
        static void UpdateMagnifier(DialogData *pData)
        {
            if (!pData->hMagnifier || !IsWindow(pData->hMagnifier))
                return;

            POINT pt;
            GetCursorPos(&pt);

            // Position magnifier below the preview, or to the right if no preview
            int px = pt.x + 20, py = pt.y + 20;
            RECT scrRect = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};

            if (pData->hEyePreview && IsWindow(pData->hEyePreview))
            {
                RECT pr;
                GetWindowRect(pData->hEyePreview, &pr);
                px = pr.left;
                py = pr.bottom + 6;
            }

            RECT mr;
            GetWindowRect(pData->hMagnifier, &mr);
            int mw = mr.right - mr.left, mh = mr.bottom - mr.top;
            if (px + mw > scrRect.right) px = scrRect.right - mw - 4;
            if (py + mh > scrRect.bottom) py = pt.y - mh - 10;
            if (py < 0) py = 4;

            SetWindowPos(pData->hMagnifier, HWND_TOPMOST, px, py, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
            InvalidateRect(pData->hMagnifier, nullptr, TRUE);
            UpdateWindow(pData->hMagnifier);
        }

        // ── Eyedropper overlay window (full-screen, nearly transparent) ──
        static LRESULT CALLBACK EyeOverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
        {
            if (msg == WM_NCCREATE)
            {
                auto *pCreate = (CREATESTRUCT *)lp;
                auto *pData = (DialogData *)pCreate->lpCreateParams;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pData);
                return DefWindowProcW(hwnd, msg, wp, lp);
            }
            auto *pData = (DialogData *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (!pData) return DefWindowProcW(hwnd, msg, wp, lp);

            switch (msg)
            {
            case WM_SETCURSOR:
                SetCursor(LoadCursor(nullptr, IDC_CROSS));
                return TRUE;

            case WM_MOUSEMOVE:
            {
                POINT pt;
                GetCursorPos(&pt);
                HDC hdcScreen = GetDC(nullptr);
                COLORREF col = GetPixel(hdcScreen, pt.x, pt.y);
                ReleaseDC(nullptr, hdcScreen);
                int r = GetRValue(col), g = GetGValue(col), b = GetBValue(col);
                pData->eyePreviewR = r;
                pData->eyePreviewG = g;
                pData->eyePreviewB = b;

                // Update preview position and redraw
                if (pData->hEyePreview && IsWindow(pData->hEyePreview))
                {
                    int px = pt.x + 20, py = pt.y + 20;
                    RECT scrRect = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
                    RECT pr;
                    GetWindowRect(pData->hEyePreview, &pr);
                    int pw = pr.right - pr.left, ph = pr.bottom - pr.top;
                    if (px + pw > scrRect.right) px = pt.x - pw - 10;
                    if (py + ph > scrRect.bottom) py = pt.y - ph - 10;
                    SetWindowPos(pData->hEyePreview, HWND_TOPMOST, px, py, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
                    InvalidateRect(pData->hEyePreview, nullptr, TRUE);
                    UpdateWindow(pData->hEyePreview);
                }

                // ── Magnifier: show when Ctrl held ──
                bool ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                if (ctrlDown)
                {
                    if (pData->hMagnifier && IsWindow(pData->hMagnifier))
                    {
                        if (!IsWindowVisible(pData->hMagnifier))
                            ShowWindow(pData->hMagnifier, SW_SHOWNOACTIVATE);
                        UpdateMagnifier(pData);
                    }
                }
                else
                {
                    if (pData->hMagnifier && IsWindow(pData->hMagnifier) && IsWindowVisible(pData->hMagnifier))
                        ShowWindow(pData->hMagnifier, SW_HIDE);
                }
                return 0;
            }

            case WM_MOUSEWHEEL:
            {
                bool ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                if (ctrlDown && pData->hMagnifier && IsWindow(pData->hMagnifier))
                {
                    int delta = GET_WHEEL_DELTA_WPARAM(wp);
                    int step = (delta > 0) ? 1 : -1;
                    int newZoom = pData->zoomLevel.load() + step;
                    newZoom = (std::max)(2, (std::min)(10, newZoom));
                    pData->zoomLevel.store(newZoom);
                    InvalidateRect(pData->hMagnifier, nullptr, TRUE);
                    UpdateWindow(pData->hMagnifier);
                    return 0;
                }
                break;
            }

            case WM_LBUTTONDOWN:
            {
                POINT pt;
                GetCursorPos(&pt);
                HDC hdcScreen = GetDC(nullptr);
                COLORREF col = GetPixel(hdcScreen, pt.x, pt.y);
                ReleaseDC(nullptr, hdcScreen);
                int r = GetRValue(col), g = GetGValue(col), b = GetBValue(col);

                // Update color picker values
                pData->curR.store(r);
                pData->curG.store(g);
                pData->curB.store(b);
                auto hsl = RgbToHsl(r, g, b);
                pData->hue.store(hsl.h);
                pData->sat.store(hsl.s);
                pData->light.store(hsl.l);

                // Signal main dialog to exit eyedropper mode (picked = true)
                if (pData->hwnd && IsWindow(pData->hwnd))
                    PostMessageW(pData->hwnd, WM_USER + 100, 1, 0);
                return 0;
            }

            case WM_RBUTTONDOWN:
                // Cancel eyedropper mode
                if (pData->hwnd && IsWindow(pData->hwnd))
                    PostMessageW(pData->hwnd, WM_USER + 100, 0, 0);
                return 0;

            case WM_KEYDOWN:
                if (wp == VK_ESCAPE)
                {
                    if (pData->hwnd && IsWindow(pData->hwnd))
                        PostMessageW(pData->hwnd, WM_USER + 100, 0, 0);
                    return 0;
                }
                if (wp == VK_RETURN)
                {
                    // Enter picks the color at current cursor position
                    POINT pt;
                    GetCursorPos(&pt);
                    HDC hdcScreen = GetDC(nullptr);
                    COLORREF col = GetPixel(hdcScreen, pt.x, pt.y);
                    ReleaseDC(nullptr, hdcScreen);
                    int r = GetRValue(col), g = GetGValue(col), b = GetBValue(col);
                    pData->curR.store(r);
                    pData->curG.store(g);
                    pData->curB.store(b);
                    auto hsl = RgbToHsl(r, g, b);
                    pData->hue.store(hsl.h);
                    pData->sat.store(hsl.s);
                    pData->light.store(hsl.l);
                    if (pData->hwnd && IsWindow(pData->hwnd))
                        PostMessageW(pData->hwnd, WM_USER + 100, 1, 0);
                    return 0;
                }
                // Arrow keys: nudge cursor by 1px (only on initial press, not repeat)
                if (!(lp & 0x40000000))
                {
                    POINT pt;
                    GetCursorPos(&pt);
                    int dx = 0, dy = 0;
                    if (wp == VK_LEFT) dx = -1;
                    else if (wp == VK_RIGHT) dx = 1;
                    else if (wp == VK_UP) dy = -1;
                    else if (wp == VK_DOWN) dy = 1;
                    if (dx != 0 || dy != 0)
                    {
                        SetCursorPos(pt.x + dx, pt.y + dy);
                        return 0;
                    }
                }
                break;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        // ── Eyedropper magnifier (zoomed pixel preview with crosshair) ──
        static LRESULT CALLBACK EyeMagnifierProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
        {
            if (msg == WM_NCCREATE)
            {
                auto *pCreate = (CREATESTRUCT *)lp;
                auto *pData = (DialogData *)pCreate->lpCreateParams;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pData);
                return DefWindowProcW(hwnd, msg, wp, lp);
            }
            auto *pData = (DialogData *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (!pData) return DefWindowProcW(hwnd, msg, wp, lp);

            switch (msg)
            {
            case WM_NCPAINT:
            case WM_NCACTIVATE:
                return 0;

            case WM_PAINT:
            {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                RECT rc;
                GetClientRect(hwnd, &rc);
                int magW = rc.right - rc.left, magH = rc.bottom - rc.top;
                int zoom = pData->zoomLevel.load();
                int srcW = magW / zoom, srcH = magH / zoom;
                if (srcW < 1) srcW = 1;
                if (srcH < 1) srcH = 1;

                POINT pt;
                GetCursorPos(&pt);
                int srcX = pt.x - srcW / 2, srcY = pt.y - srcH / 2;

                // ── Create source DIBSection ──
                BITMAPINFO bmiS = {};
                bmiS.bmiHeader.biSize = sizeof(bmiS.bmiHeader);
                bmiS.bmiHeader.biWidth = srcW;
                bmiS.bmiHeader.biHeight = -srcH;
                bmiS.bmiHeader.biPlanes = 1;
                bmiS.bmiHeader.biBitCount = 32;
                bmiS.bmiHeader.biCompression = BI_RGB;
                BYTE *srcBits = nullptr;
                HBITMAP hSrcBmp = CreateDIBSection(hdc, &bmiS, DIB_RGB_COLORS, (void**)&srcBits, nullptr, 0);
                HDC srcDC = CreateCompatibleDC(hdc);
                HBITMAP oldSrc = (HBITMAP)SelectObject(srcDC, hSrcBmp);

                HDC scrDC = GetDC(nullptr);
                BitBlt(srcDC, 0, 0, srcW, srcH, scrDC, srcX, srcY, SRCCOPY);
                ReleaseDC(nullptr, scrDC);

                // ── Create magnified DIBSection ──
                BITMAPINFO bmiM = {};
                bmiM.bmiHeader.biSize = sizeof(bmiM.bmiHeader);
                bmiM.bmiHeader.biWidth = magW;
                bmiM.bmiHeader.biHeight = -magH;
                bmiM.bmiHeader.biPlanes = 1;
                bmiM.bmiHeader.biBitCount = 32;
                bmiM.bmiHeader.biCompression = BI_RGB;
                BYTE *magBits = nullptr;
                HBITMAP hMagBmp = CreateDIBSection(hdc, &bmiM, DIB_RGB_COLORS, (void**)&magBits, nullptr, 0);
                HDC magDC = CreateCompatibleDC(hdc);
                HBITMAP oldMag = (HBITMAP)SelectObject(magDC, hMagBmp);

                // Nearest-neighbor scaling
                for (int my = 0; my < magH; my++)
                {
                    int sy = my * srcH / magH;
                    if (sy >= srcH) sy = srcH - 1;
                    for (int mx = 0; mx < magW; mx++)
                    {
                        int sx = mx * srcW / magW;
                        if (sx >= srcW) sx = srcW - 1;
                        int si = (sy * srcW + sx) * 4;
                        int mi = (my * magW + mx) * 4;
                        magBits[mi + 0] = srcBits[si + 0];
                        magBits[mi + 1] = srcBits[si + 1];
                        magBits[mi + 2] = srcBits[si + 2];
                        magBits[mi + 3] = 255;
                    }
                }

                // ── Crosshair ──
                int cx = magW / 2, cy = magH / 2;
                int ci = (cy * magW + cx) * 4;
                int cr = magBits[ci + 2], cg = magBits[ci + 1], cb = magBits[ci + 0];
                BYTE xR = (cr > 128) ? 0 : 255;
                BYTE xG = (cg > 128) ? 0 : 255;
                BYTE xB = (cb > 128) ? 0 : 255;
                BYTE ixR = 255 - xR, ixG = 255 - xG, ixB = 255 - xB;

                // Helper lambda: set pixel in magBits
                auto setPix = [&](int x, int y, BYTE r2, BYTE g2, BYTE b2) {
                    if (x < 0 || x >= magW || y < 0 || y >= magH) return;
                    int idx = (y * magW + x) * 4;
                    magBits[idx + 0] = b2;
                    magBits[idx + 1] = g2;
                    magBits[idx + 2] = r2;
                    magBits[idx + 3] = 255;
                };

                int hl = 8; // crosshair arm half-length
                // Horizontal (outline first, then inner)
                for (int x = cx - hl - 1; x <= cx + hl + 1; x++)
                {
                    setPix(x, cy - 1, ixR, ixG, ixB);
                    setPix(x, cy + 1, ixR, ixG, ixB);
                }
                for (int x = cx - hl; x <= cx + hl; x++)
                {
                    setPix(x, cy, xR, xG, xB);
                }
                // Vertical (outline first, then inner)
                for (int y = cy - hl - 1; y <= cy + hl + 1; y++)
                {
                    setPix(cx - 1, y, ixR, ixG, ixB);
                    setPix(cx + 1, y, ixR, ixG, ixB);
                }
                for (int y = cy - hl; y <= cy + hl; y++)
                {
                    setPix(cx, y, xR, xG, xB);
                }
                // Center dot (fill with inverted color)
                setPix(cx, cy, ixR, ixG, ixB);

                // ── Render ──
                BitBlt(hdc, 0, 0, magW, magH, magDC, 0, 0, SRCCOPY);

                // Border
                HPEN bp = CreatePen(PS_SOLID, 2, theme.ControlFrame.ToCOLORREF());
                HPEN op = (HPEN)SelectObject(hdc, bp);
                HBRUSH ob = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
                SelectObject(hdc, op);
                SelectObject(hdc, ob);
                DeleteObject(bp);

                // Zoom level label (top-left corner)
                SetTextColor(hdc, theme.Text.ToCOLORREF());
                SetBkMode(hdc, TRANSPARENT);
                wchar_t zt[16];
                swprintf(zt, L"%dx", zoom);
                RECT zr = {rc.left + 4, rc.top + 2, rc.right - 4, rc.top + 18};
                DrawTextW(hdc, zt, -1, &zr, DT_LEFT | DT_TOP | DT_SINGLELINE);

                SelectObject(srcDC, oldSrc);
                SelectObject(magDC, oldMag);
                DeleteDC(srcDC);
                DeleteDC(magDC);
                DeleteObject(hSrcBmp);
                DeleteObject(hMagBmp);

                EndPaint(hwnd, &ps);
                return 0;
            }

            case WM_ERASEBKGND:
                return 1;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        // ── Eyedropper preview tooltip (shows color swatch + RGB) ──
        static LRESULT CALLBACK EyePreviewProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
        {
            if (msg == WM_NCCREATE)
            {
                auto *pCreate = (CREATESTRUCT *)lp;
                auto *pData = (DialogData *)pCreate->lpCreateParams;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pData);
                return DefWindowProcW(hwnd, msg, wp, lp);
            }
            auto *pData = (DialogData *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (!pData) return DefWindowProcW(hwnd, msg, wp, lp);

            switch (msg)
            {
            case WM_NCPAINT:
            case WM_NCACTIVATE:
                return 0;

            case WM_PAINT:
            {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                RECT rc;
                GetClientRect(hwnd, &rc);

                // Double buffer
                HDC memDC = CreateCompatibleDC(hdc);
                HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
                HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

                auto &th = GetTheme();

                // Background
                HBRUSH bgBr = CreateSolidBrush(th.SecondaryBackground.ToCOLORREF());
                FillRect(memDC, &rc, bgBr);
                DeleteObject(bgBr);

                int r = pData->eyePreviewR;
                int g = pData->eyePreviewG;
                int b = pData->eyePreviewB;

                // Color swatch (left side, 32x32)
                RECT swRect = {6, 10, 38, 42};
                HBRUSH swBr = CreateSolidBrush(RGB(r, g, b));
                FillRect(memDC, &swRect, swBr);
                DeleteObject(swBr);
                HPEN bp = CreatePen(PS_SOLID, 1, th.ControlFrame.ToCOLORREF());
                HPEN oldPen = (HPEN)SelectObject(memDC, bp);
                HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
                Rectangle(memDC, swRect.left, swRect.top, swRect.right, swRect.bottom);
                SelectObject(memDC, oldPen);
                SelectObject(memDC, oldBrush);
                DeleteObject(bp);

                // RGB text (top line)
                SetTextColor(memDC, th.Text.ToCOLORREF());
                SetBkMode(memDC, TRANSPARENT);
                HFONT hFont = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                                          0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
                HFONT oldFont = (HFONT)SelectObject(memDC, hFont);
                wchar_t text[64];
                swprintf(text, L"R:%d G:%d B:%d", r, g, b);
                RECT txtRect = {44, 3, rc.right - 6, 28};
                DrawTextW(memDC, text, -1, &txtRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                SelectObject(memDC, oldFont);
                DeleteObject(hFont);

                // Guide text (two lines)
                HFONT hGuideFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                               0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
                HFONT oldGuideFont = (HFONT)SelectObject(memDC, hGuideFont);
                SetTextColor(memDC, th.Text.ToCOLORREF());
                RECT guideRect1 = {6, 38, rc.right - 6, 52};
                DrawTextW(memDC, L"Arrow:Nudge Ctrl+Wheel:Zoom", -1, &guideRect1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                RECT guideRect2 = {6, 52, rc.right - 6, 66};
                DrawTextW(memDC, L"LMB/Enter:Pick Esc/RMB:Exit", -1, &guideRect2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                SelectObject(memDC, oldGuideFont);
                DeleteObject(hGuideFont);

                // Border
                bp = CreatePen(PS_SOLID, 1, th.ControlFrame.ToCOLORREF());
                oldPen = (HPEN)SelectObject(memDC, bp);
                oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
                RoundRect(memDC, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
                SelectObject(memDC, oldPen);
                SelectObject(memDC, oldBrush);
                DeleteObject(bp);

                BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
                SelectObject(memDC, oldBmp);
                DeleteObject(memBmp);
                DeleteDC(memDC);

                EndPaint(hwnd, &ps);
                return 0;
            }

            case WM_ERASEBKGND:
                return 1;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
        {
            if (msg == WM_NCCREATE)
            {
                auto *pCreate = (CREATESTRUCT *)lp;
                auto *pData = (DialogData *)pCreate->lpCreateParams;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pData);
                return DefWindowProcW(hwnd, msg, wp, lp);
            }
            auto *pData = (DialogData *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (!pData)
                return DefWindowProcW(hwnd, msg, wp, lp);

            const int PAD = 16, WSIZE = 220, WX = PAD + 10, WY = PAD + 30;
            const int EX = WX + WSIZE + 24, EH = 24, GAP = 4;
            const int LW = 16; // label width
            const int SLIDER_X = PAD + 10, SLIDER_H = 24;

            switch (msg)
            {
            case WM_CREATE:
            {
                Controls::InitWindowColor(hwnd);
                pData->hwnd = hwnd;
                pData->hFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                           0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
                auto hInst = GetModuleHandleW(nullptr);

                auto mkEd = [&](int id, int x, int y, int w)
                {
                    HWND he = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_RIGHT | WS_TABSTOP,
                                              x, y, w, EH, hwnd, (HMENU)(INT_PTR)id, hInst, nullptr);
                    if (pData->hFont)
                        SendMessage(he, WM_SETFONT, (WPARAM)pData->hFont, TRUE);
                    SendMessage(he, EM_SETLIMITTEXT, 4, 0);
                    Controls::Edit::Subclass(he);
                    return he;
                };
                auto mkLb = [&](int x, int y, const wchar_t *t)
                {
                    HWND hl = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
                                              x, y, LW, EH, hwnd, nullptr, hInst, nullptr);
                    if (pData->hFont)
                        SendMessage(hl, WM_SETFONT, (WPARAM)pData->hFont, TRUE);
                    return hl;
                };

                // Single-column layout: label + narrow edit + slider on each row
                const int EDIT_W_NARROW = 55;
                RECT clCreate;
                GetClientRect(hwnd, &clCreate);
                int fieldRight = clCreate.right - PAD;
                auto rowY = [&](int row)
                { return WY + row * (EH + GAP); };

                // Helper: compute slider rect for a given row
                const int EDIT_SLIDER_GAP = 10;
                auto sliderRect = [&](int row) -> RECT {
                    int editEnd = (EX + LW + GAP + EDIT_W_NARROW + EDIT_SLIDER_GAP);
                    return {editEnd, rowY(row), fieldRight, rowY(row) + EH};
                };

                mkLb(EX, rowY(0), L"R");
                pData->hR = mkEd(3001, EX + LW + GAP, rowY(0), EDIT_W_NARROW);
                pData->sliderR.SetupInt(0, 255, &pData->curR, sliderRect(0));
                mkLb(EX, rowY(1), L"G");
                pData->hG = mkEd(3002, EX + LW + GAP, rowY(1), EDIT_W_NARROW);
                pData->sliderG.SetupInt(0, 255, &pData->curG, sliderRect(1));
                mkLb(EX, rowY(2), L"B");
                pData->hB = mkEd(3003, EX + LW + GAP, rowY(2), EDIT_W_NARROW);
                pData->sliderB.SetupInt(0, 255, &pData->curB, sliderRect(2));
                if (pData->enableAlpha)
                {
                    mkLb(EX, rowY(3), L"A");
                    pData->hA = mkEd(3004, EX + LW + GAP, rowY(3), EDIT_W_NARROW);
                    pData->sliderA.SetupInt(0, 255, &pData->curA, sliderRect(3));
                }
                int hslRow = pData->enableAlpha ? 4 : 3;
                mkLb(EX, rowY(hslRow), L"H");
                pData->hH = mkEd(3011, EX + LW + GAP, rowY(hslRow), EDIT_W_NARROW);
                pData->sliderH.SetupDouble(0, 360, &pData->hue, 1.0, sliderRect(hslRow));
                mkLb(EX, rowY(hslRow + 1), L"S");
                pData->hS = mkEd(3012, EX + LW + GAP, rowY(hslRow + 1), EDIT_W_NARROW);
                pData->sliderS.SetupDouble(0, 100, &pData->sat, 100.0, sliderRect(hslRow + 1));
                mkLb(EX, rowY(hslRow + 2), L"L");
                pData->hL = mkEd(3013, EX + LW + GAP, rowY(hslRow + 2), EDIT_W_NARROW);
                pData->sliderL.SetupDouble(0, 100, &pData->light, 100.0, sliderRect(hslRow + 2));

                mkLb(EX, rowY(hslRow + 3), L"#");
                pData->hHex = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_UPPERCASE | WS_TABSTOP,
                                              EX + LW + GAP, rowY(hslRow + 3), EDIT_W_NARROW, EH, hwnd, (HMENU)3005, hInst, nullptr);
                if (pData->hFont)
                    SendMessage(pData->hHex, WM_SETFONT, (WPARAM)pData->hFont, TRUE);
                SendMessage(pData->hHex, EM_SETLIMITTEXT, 9, 0);
                Controls::Edit::Subclass(pData->hHex);

                // Create checkerboard pattern brush (12x12, 6x6 cells)
                {
                    HBITMAP hChkBmp = CreateBitmap(12, 12, 1, 1, nullptr); // monochrome
                    HDC chkDC = CreateCompatibleDC(nullptr);
                    HBITMAP oldChk = (HBITMAP)SelectObject(chkDC, hChkBmp);
                    for (int y = 0; y < 12; y++)
                        for (int x = 0; x < 12; x++)
                        {
                            SetPixelV(chkDC, x, y, ((x / 6) + (y / 6)) % 2 ? RGB(255, 255, 255) : RGB(0, 0, 0));
                        }
                    SelectObject(chkDC, oldChk);
                    DeleteDC(chkDC);
                    pData->hCheckerBrush = CreatePatternBrush(hChkBmp);
                    DeleteObject(hChkBmp);
                }
                pData->SyncFromRgbToInputs();

                int btnW = 80, btnH = 28;
                int btnYC = WY + WSIZE + (pData->enableAlpha ? 165 : 133);
                int btnRightX = clCreate.right - PAD;
                pData->hBtnOK = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                                btnRightX - btnW * 2 - 8, btnYC, btnW, btnH, hwnd, (HMENU)IDOK, hInst, nullptr);
                Controls::Button::Subclass(pData->hBtnOK);
                pData->hBtnCancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                                    btnRightX - btnW, btnYC, btnW, btnH, hwnd, (HMENU)IDCANCEL, hInst, nullptr);
                Controls::Button::Subclass(pData->hBtnCancel);

                // Eyedropper (screen color picker) button
                pData->hBtnEye = CreateWindowExW(0, L"BUTTON", L"Pick", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                                 btnRightX - btnW * 3 - 16, btnYC, btnW, btnH, hwnd, (HMENU)3020, hInst, nullptr);
                Controls::Button::Subclass(pData->hBtnEye);

                if (pData->hFont)
                {
                    SendMessage(pData->hBtnOK, WM_SETFONT, (WPARAM)pData->hFont, TRUE);
                    SendMessage(pData->hBtnCancel, WM_SETFONT, (WPARAM)pData->hFont, TRUE);
                    SendMessage(pData->hBtnEye, WM_SETFONT, (WPARAM)pData->hFont, TRUE);
                }
                return 0;
            }

            case WM_CTLCOLOREDIT:
            {
                HDC hdc = (HDC)wp;
                auto &th = GetTheme();
                SetBkColor(hdc, th.SecondaryBackground.ToCOLORREF());
                SetTextColor(hdc, th.Text.ToCOLORREF());
                static HBRUSH hBr = CreateSolidBrush(th.SecondaryBackground.ToCOLORREF());
                return (LRESULT)hBr;
            }
            case WM_CTLCOLORSTATIC:
            {
                HDC hdc = (HDC)wp;
                auto &th = GetTheme();
                SetBkColor(hdc, th.PrimaryBackground.ToCOLORREF());
                SetTextColor(hdc, th.Text.ToCOLORREF());
                static HBRUSH hStBr = CreateSolidBrush(th.PrimaryBackground.ToCOLORREF());
                return (LRESULT)hStBr;
            }

            case WM_SIZE:
            {
                int btnW = 80, btnH = 28;
                RECT cl5;
                GetClientRect(hwnd, &cl5);
                int btnYC = cl5.bottom - btnH - 14;
                int btnRightX = cl5.right - PAD;
                if (pData->hBtnOK)
                {
                    SetWindowPos(pData->hBtnEye, nullptr, btnRightX - btnW * 3 - 16, btnYC, btnW, btnH, SWP_NOZORDER);
                    SetWindowPos(pData->hBtnCancel, nullptr, btnRightX - btnW, btnYC, btnW, btnH, SWP_NOZORDER);
                    SetWindowPos(pData->hBtnOK, nullptr, btnRightX - btnW * 2 - 8, btnYC, btnW, btnH, SWP_NOZORDER);
                }
                return 0;
            }

            case WM_PAINT:
            {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                RECT cl;
                GetClientRect(hwnd, &cl);

                auto &th = GetTheme();

                // Reuse cached double buffer — avoids per-frame GDI alloc
                if (!pData->hMemDC || cl.right != pData->memW || cl.bottom != pData->memH)
                {
                    if (pData->hMemBmp)
                    {
                        DeleteObject(pData->hMemBmp);
                        pData->hMemBmp = nullptr;
                    }
                    if (pData->hMemDC)
                    {
                        DeleteDC(pData->hMemDC);
                        pData->hMemDC = nullptr;
                    }
                    pData->hMemDC = CreateCompatibleDC(hdc);
                    pData->hMemBmp = CreateCompatibleBitmap(hdc, cl.right, cl.bottom);
                    SelectObject(pData->hMemDC, pData->hMemBmp);
                    pData->memW = cl.right;
                    pData->memH = cl.bottom;
                }
                HDC memDC = pData->hMemDC;

                HBRUSH bgBr = CreateSolidBrush(th.PrimaryBackground.ToCOLORREF());
                FillRect(memDC, &cl, bgBr);
                DeleteObject(bgBr);

                int r = pData->curR.load(), g = pData->curG.load(), b = pData->curB.load(), a = pData->curA.load();
                double hue = pData->hue.load(), sat = pData->sat.load(), light = pData->light.load();
                int cx2 = WX + WSIZE / 2, cy2 = WY + WSIZE / 2, radius = WSIZE / 2 - 4;

                // ── Draw cached circular colour wheel (only rebuild when L changes) ──
                if (!pData->hCachedDC || std::abs(pData->cachedLight - light) > 1e-9)
                {
                    if (pData->hCachedWheel)
                    {
                        DeleteObject(pData->hCachedWheel);
                        pData->hCachedWheel = nullptr;
                    }
                    if (pData->hCachedDC)
                    {
                        DeleteDC(pData->hCachedDC);
                        pData->hCachedDC = nullptr;
                    }

                    BITMAPINFO bmi = {};
                    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bmi.bmiHeader.biWidth = WSIZE;
                    bmi.bmiHeader.biHeight = -WSIZE;
                    bmi.bmiHeader.biPlanes = 1;
                    bmi.bmiHeader.biBitCount = 32;
                    bmi.bmiHeader.biCompression = BI_RGB;
                    BYTE *bits = nullptr;
                    pData->hCachedWheel = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, (void **)&bits, nullptr, 0);
                    pData->hCachedDC = CreateCompatibleDC(memDC);
                    SelectObject(pData->hCachedDC, pData->hCachedWheel);
                    for (int y = 0; y < WSIZE; y++)
                        for (int x = 0; x < WSIZE; x++)
                        {
                            int dx = x - WSIZE / 2, dy = y - WSIZE / 2;
                            double dist = std::sqrt((double)(dx * dx + dy * dy));
                            int idx = (y * WSIZE + x) * 4;
                            if (dist <= radius)
                            {
                                double ang = std::atan2((double)dy, (double)dx) * 180.0 / 3.14159265;
                                if (ang < 0)
                                    ang += 360;
                                double st2 = dist / radius;
                                int rr, gg, bb;
                                HslToRgb(ang, st2, light, rr, gg, bb);
                                bits[idx + 0] = (BYTE)bb;
                                bits[idx + 1] = (BYTE)gg;
                                bits[idx + 2] = (BYTE)rr;
                                bits[idx + 3] = 255;
                            }
                            else
                            {
                                bits[idx + 0] = GetBValue(th.PrimaryBackground.ToCOLORREF());
                                bits[idx + 1] = GetGValue(th.PrimaryBackground.ToCOLORREF());
                                bits[idx + 2] = GetRValue(th.PrimaryBackground.ToCOLORREF());
                                bits[idx + 3] = 255;
                            }
                        }
                    pData->cachedLight = light;
                }
                BitBlt(memDC, WX, WY, WSIZE, WSIZE, pData->hCachedDC, 0, 0, SRCCOPY);

                // Wheel indicator (white+black ring)
                double selAng = hue * 3.14159265 / 180.0;
                int sx2 = cx2 + (int)(std::cos(selAng) * sat * radius);
                int sy2 = cy2 + (int)(std::sin(selAng) * sat * radius);
                HPEN hW = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
                HPEN hB = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
                auto oP = (HPEN)SelectObject(memDC, hW);
                SelectObject(memDC, GetStockObject(NULL_BRUSH));
                Ellipse(memDC, sx2 - 7, sy2 - 7, sx2 + 7, sy2 + 7);
                SelectObject(memDC, hB);
                Ellipse(memDC, sx2 - 5, sy2 - 5, sx2 + 5, sy2 + 5);
                SelectObject(memDC, oP);
                DeleteObject(hW);
                DeleteObject(hB);

                // ── Slider helper: render gradient via DIBSection (fast) ──
                auto DrawSliderTrack = [&](const RECT &sr, bool isAlpha2, int cr2, int cg2, int cb2)
                {
                    int sw2 = sr.right - sr.left, ty2 = sr.top + (sr.bottom - sr.top) / 2, th2 = 12;
                    BITMAPINFO bmi2 = {};
                    bmi2.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bmi2.bmiHeader.biWidth = sw2;
                    bmi2.bmiHeader.biHeight = -th2;
                    bmi2.bmiHeader.biPlanes = 1;
                    bmi2.bmiHeader.biBitCount = 32;
                    bmi2.bmiHeader.biCompression = BI_RGB;
                    BYTE *bits2 = nullptr;
                    HBITMAP hbm2 = CreateDIBSection(memDC, &bmi2, DIB_RGB_COLORS, (void **)&bits2, nullptr, 0);
                    HDC sDC = CreateCompatibleDC(memDC);
                    SelectObject(sDC, hbm2);
                    for (int y = 0; y < th2; y++)
                        for (int x = 0; x < sw2; x++)
                        {
                            double t = (double)x / (sw2 - 1);
                            int idx = (y * sw2 + x) * 4;
                            if (isAlpha2)
                            {
                                int chk = ((x / 6) + (y / 6)) % 2;
                                int bgR = chk ? 200 : 140, bgG = chk ? 200 : 140, bgB = chk ? 200 : 140;
                                double af2 = t;
                                bits2[idx + 0] = (BYTE)(cb2 * af2 + bgB * (1.0 - af2));
                                bits2[idx + 1] = (BYTE)(cg2 * af2 + bgG * (1.0 - af2));
                                bits2[idx + 2] = (BYTE)(cr2 * af2 + bgR * (1.0 - af2));
                            }
                            else
                            {
                                int rr, gg, bb;
                                HslToRgb(hue, sat, t, rr, gg, bb);
                                bits2[idx + 0] = (BYTE)bb;
                                bits2[idx + 1] = (BYTE)gg;
                                bits2[idx + 2] = (BYTE)rr;
                            }
                            bits2[idx + 3] = 255;
                        }
                    BitBlt(memDC, sr.left, ty2 - th2 / 2, sw2, th2, sDC, 0, 0, SRCCOPY);
                    DeleteDC(sDC);
                    DeleteObject(hbm2);
                    HPEN bp2 = CreatePen(PS_SOLID, 1, th.ControlFrame.ToCOLORREF());
                    auto op2 = (HPEN)SelectObject(memDC, bp2);
                    SelectObject(memDC, GetStockObject(NULL_BRUSH));
                    RoundRect(memDC, sr.left, ty2 - th2 / 2, sr.right, ty2 + th2 / 2, 4, 4);
                    SelectObject(memDC, op2);
                    DeleteObject(bp2);
                };

                // ── Lightness slider ──
                RECT lsRc = {SLIDER_X, WY + WSIZE + 20, cl.right - PAD, WY + WSIZE + 20 + SLIDER_H};
                DrawSliderTrack(lsRc, false, r, g, b);
                int lx = lsRc.left + (int)(light * (lsRc.right - lsRc.left - 1));
                int ly = (lsRc.top + lsRc.bottom) / 2;
                {
                    HPEN tP2 = CreatePen(PS_SOLID, 1, th.ControlFrame.ToCOLORREF());
                    HBRUSH tB2 = CreateSolidBrush(RGB(255, 255, 255));
                    auto oP2 = (HPEN)SelectObject(memDC, tP2);
                    auto oB2 = (HBRUSH)SelectObject(memDC, tB2);
                    RoundRect(memDC, lx - 5, ly - 8, lx + 5, ly + 8, 3, 3);
                    SelectObject(memDC, oP2);
                    SelectObject(memDC, oB2);
                    DeleteObject(tP2);
                    DeleteObject(tB2);
                }

                // ── Alpha slider (optional) ──
                RECT aRc = {SLIDER_X, lsRc.bottom + 8, cl.right - PAD, lsRc.bottom + 8 + SLIDER_H};
                if (pData->enableAlpha)
                {
                    DrawSliderTrack(aRc, true, r, g, b);
                    int ax2 = aRc.left + (int)((a / 255.0) * (aRc.right - aRc.left - 1));
                    {
                        HPEN tP2 = CreatePen(PS_SOLID, 1, th.ControlFrame.ToCOLORREF());
                        HBRUSH tB2 = CreateSolidBrush(RGB(255, 255, 255));
                        auto oP2 = (HPEN)SelectObject(memDC, tP2);
                        auto oB2 = (HBRUSH)SelectObject(memDC, tB2);
                        RoundRect(memDC, ax2 - 5, (aRc.top + aRc.bottom) / 2 - 8, ax2 + 5, (aRc.top + aRc.bottom) / 2 + 8, 3, 3);
                        SelectObject(memDC, oP2);
                        SelectObject(memDC, oB2);
                        DeleteObject(tP2);
                        DeleteObject(tB2);
                    }
                }

                // ── Draw field sliders (gradient track + thumb) ──
                auto DrawFieldSliderTrack = [&](const auto &fs,
                    int sR, int sG, int sB, int eR, int eG, int eB, bool isHue)
                {
                    int fsl = fs.rect.left, fst = fs.rect.top + (fs.rect.bottom - fs.rect.top) / 2;
                    int fsw = fs.rect.right - fs.rect.left, fth = 12;
                    BITMAPINFO bmi3={}; bmi3.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
                    bmi3.bmiHeader.biWidth=fsw; bmi3.bmiHeader.biHeight=-fth;
                    bmi3.bmiHeader.biPlanes=1; bmi3.bmiHeader.biBitCount=32; bmi3.bmiHeader.biCompression=BI_RGB;
                    BYTE *bits3=nullptr;
                    HBITMAP hbm3=CreateDIBSection(memDC,&bmi3,DIB_RGB_COLORS,(void**)&bits3,nullptr,0);
                    HDC sDC3=CreateCompatibleDC(memDC); SelectObject(sDC3,hbm3);
                    for(int y=0; y<fth; y++) for(int x=0; x<fsw; x++){
                        double t=(double)x/(fsw-1);
                        int idx=(y*fsw+x)*4;
                        if(isHue){
                            int rr,gg,bb; HslToRgb(t*360.0, 1.0, 0.5, rr, gg, bb);
                            bits3[idx+0]=(BYTE)bb; bits3[idx+1]=(BYTE)gg;
                            bits3[idx+2]=(BYTE)rr; bits3[idx+3]=255;
                        } else {
                            int rr=(int)(sR + t*(eR-sR));
                            int gg=(int)(sG + t*(eG-sG));
                            int bb=(int)(sB + t*(eB-sB));
                            bits3[idx+0]=(BYTE)std::clamp(bb,0,255);
                            bits3[idx+1]=(BYTE)std::clamp(gg,0,255);
                            bits3[idx+2]=(BYTE)std::clamp(rr,0,255);
                            bits3[idx+3]=255;
                        }
                    }
                    BitBlt(memDC, fsl, fst-fth/2, fsw, fth, sDC3, 0, 0, SRCCOPY);
                    DeleteDC(sDC3); DeleteObject(hbm3);
                    HPEN bp3=CreatePen(PS_SOLID,1,th.ControlFrame.ToCOLORREF());
                    auto op3=(HPEN)SelectObject(memDC,bp3);
                    SelectObject(memDC,GetStockObject(NULL_BRUSH));
                    RoundRect(memDC, fsl, fst-fth/2, fsl+fsw, fst+fth/2, 3, 3);
                    SelectObject(memDC,op3); DeleteObject(bp3);
                    // Thumb
                    int tx = fs.GetThumbPos();
                    HPEN tp3=CreatePen(PS_SOLID,1,th.ControlFrame.ToCOLORREF());
                    HBRUSH tb3=CreateSolidBrush(RGB(255,255,255));
                    auto otp=(HPEN)SelectObject(memDC,tp3);
                    auto otb=(HBRUSH)SelectObject(memDC,tb3);
                    RoundRect(memDC, tx, fst-fth/2-2, tx+fs.thumbSize, fst+fth/2+2, 3, 3);
                    SelectObject(memDC,otp); SelectObject(memDC,otb);
                    DeleteObject(tp3); DeleteObject(tb3);
                };

                // Render each field slider with its unique gradient
                DrawFieldSliderTrack(pData->sliderR, 0,0,0, 255,0,0, false);
                DrawFieldSliderTrack(pData->sliderG, 0,0,0, 0,255,0, false);
                DrawFieldSliderTrack(pData->sliderB, 0,0,0, 0,0,255, false);
                if(pData->enableAlpha)
                    DrawFieldSliderTrack(pData->sliderA, 0,0,0, 255,255,255, false);
                DrawFieldSliderTrack(pData->sliderH, 0,0,0, 0,0,0, true); // rainbow
                // S: gray → saturated at current hue
                { int sr,sg,sb; HslToRgb(hue, 1.0, light, sr, sg, sb);
                  int gray = (int)(light*255);
                  DrawFieldSliderTrack(pData->sliderS, gray,gray,gray, sr,sg,sb, false); }
                // L: black → white
                DrawFieldSliderTrack(pData->sliderL, 0,0,0, 255,255,255, false);

                // ── Color preview (DIB-based) ──
                int pvY = (pData->enableAlpha ? aRc.bottom : lsRc.bottom) + 14, pvW = (cl.right - SLIDER_X * 2 - 20) / 2, pvH = 36;
                int oldR_ = pData->oldR.load(), oldG_ = pData->oldG.load(), oldB_ = pData->oldB.load(), oldA_ = pData->oldA.load();
                auto drSw = [&](int px, int py, int pw, int ph, int cr2, int cg2, int cb2, int ca2, const wchar_t *lb)
                {
                    BITMAPINFO bmi2 = {};
                    bmi2.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bmi2.bmiHeader.biWidth = pw;
                    bmi2.bmiHeader.biHeight = -ph;
                    bmi2.bmiHeader.biPlanes = 1;
                    bmi2.bmiHeader.biBitCount = 32;
                    bmi2.bmiHeader.biCompression = BI_RGB;
                    BYTE *bits2 = nullptr;
                    HBITMAP hbm2 = CreateDIBSection(memDC, &bmi2, DIB_RGB_COLORS, (void **)&bits2, nullptr, 0);
                    HDC swDC = CreateCompatibleDC(memDC);
                    SelectObject(swDC, hbm2);
                    double af2 = ca2 / 255.0;
                    for (int y = 0; y < ph; y++)
                        for (int x = 0; x < pw; x++)
                        {
                            int idx = (y * pw + x) * 4;
                            int chk = ((x / 6) + (y / 6)) % 2;
                            int bgR = chk ? 200 : 140, bgG = chk ? 200 : 140, bgB = chk ? 200 : 140;
                            bits2[idx + 0] = (BYTE)(cb2 * af2 + bgB * (1.0 - af2));
                            bits2[idx + 1] = (BYTE)(cg2 * af2 + bgG * (1.0 - af2));
                            bits2[idx + 2] = (BYTE)(cr2 * af2 + bgR * (1.0 - af2));
                            bits2[idx + 3] = 255;
                        }
                    BitBlt(memDC, px, py, pw, ph, swDC, 0, 0, SRCCOPY);
                    DeleteDC(swDC);
                    DeleteObject(hbm2);
                    HPEN bp2 = CreatePen(PS_SOLID, 1, th.ControlFrame.ToCOLORREF());
                    auto op2 = (HPEN)SelectObject(memDC, bp2);
                    SelectObject(memDC, GetStockObject(NULL_BRUSH));
                    Rectangle(memDC, px, py, px + pw, py + ph);
                    SelectObject(memDC, op2);
                    DeleteObject(bp2);
                    SetTextColor(memDC, th.Text.ToCOLORREF());
                    SetBkMode(memDC, TRANSPARENT);
                    HFONT oldFont = (HFONT)SelectObject(memDC, pData->hFont);
                    RECT lr2 = {px, py + ph + 2, px + pw, py + ph + 24};
                    DrawTextW(memDC, lb, -1, &lr2, DT_CENTER | DT_TOP | DT_SINGLELINE);
                    SelectObject(memDC, oldFont);
                };
                drSw(SLIDER_X, pvY, pvW, pvH, oldR_, oldG_, oldB_, oldA_, L"Original");
                drSw(SLIDER_X + pvW + 20, pvY, pvW, pvH, r, g, b, a, L"New");

                // ── Title ──
                SetTextColor(memDC, th.Text.ToCOLORREF());
                SetBkMode(memDC, TRANSPARENT);
                HFONT hTF = CreateFontW(-16, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
                auto of_ = (HFONT)SelectObject(memDC, hTF);
                RECT tRc = {PAD, 6, 300, 28};
                DrawTextW(memDC, L"Color Picker", -1, &tRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                SelectObject(memDC, of_);
                DeleteObject(hTF);

                BitBlt(hdc, 0, 0, cl.right, cl.bottom, memDC, 0, 0, SRCCOPY);
                EndPaint(hwnd, &ps);
                return 0;
            }

            case WM_ERASEBKGND:
                return 1;

            case WM_LBUTTONDOWN:
            {
                int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
                SetCapture(hwnd);
                // Wheel
                int cxc = WX + WSIZE / 2, cyc = WY + WSIZE / 2, rad = WSIZE / 2 - 4;
                double dx = mx - cxc, dy = my - cyc, dist = std::sqrt(dx * dx + dy * dy);
                if (dist <= rad && mx >= WX && mx < WX + WSIZE && my >= WY && my < WY + WSIZE)
                {
                    pData->draggingCanvas = true;
                    double ang = std::atan2(dy, dx) * 180.0 / 3.14159265;
                    if (ang < 0)
                        ang += 360;
                    pData->hue.store(ang);
                    pData->sat.store(std::min(dist / rad, 1.0));
                    int rr, gg, bb;
                    HslToRgb(ang, pData->sat.load(), pData->light.load(), rr, gg, bb);
                    pData->curR.store(rr);
                    pData->curG.store(gg);
                    pData->curB.store(bb);
                    pData->SyncFromRgbToInputs();
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                }
                // Lightness slider
                RECT lsRc = {SLIDER_X, WY + WSIZE + 20, 0, WY + WSIZE + 20 + SLIDER_H};
                RECT cl2;
                GetClientRect(hwnd, &cl2);
                lsRc.right = cl2.right - PAD;
                if (mx >= lsRc.left && mx < lsRc.right && my >= lsRc.top && my < lsRc.bottom)
                {
                    pData->draggingHue = true;
                    double lv = (std::max)(0.0, (std::min)(1.0, (double)(mx - lsRc.left) / (lsRc.right - lsRc.left - 1)));
                    pData->light.store(lv);
                    int rr, gg, bb;
                    HslToRgb(pData->hue.load(), pData->sat.load(), lv, rr, gg, bb);
                    pData->curR.store(rr);
                    pData->curG.store(gg);
                    pData->curB.store(bb);
                    pData->SyncFromRgbToInputs();
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                }
                // Alpha slider (optional)
                if (pData->enableAlpha)
                {
                    RECT aRc = {SLIDER_X, lsRc.bottom + 8, 0, lsRc.bottom + 8 + SLIDER_H};
                    aRc.right = cl2.right - PAD;
                    if (mx >= aRc.left && mx < aRc.right && my >= aRc.top && my < aRc.bottom)
                    {
                        pData->draggingAlpha = true;
                        int aa2 = (int)((std::max)(0.0, (std::min)(1.0, (double)(mx - aRc.left) / (aRc.right - aRc.left - 1))) * 255);
                        pData->curA.store(aa2);
                        pData->SyncFromRgbToInputs();
                        InvalidateRect(hwnd, nullptr, TRUE);
                        return 0;
                    }
                }
                // ── Field sliders hit test ──
                {
                    auto hitField = [&](auto &fs, bool isHSL) -> bool {
                        if(mx>=fs.rect.left && mx<fs.rect.right && my>=fs.rect.top && my<fs.rect.bottom){
                            fs.dragging = true;
                            int dv = fs.DisplayFromPos(mx);
                            fs.SetFromDisplay(dv);
                            if(isHSL){ // H/S/L → recalc RGB
                                double hh=pData->hue.load(), ss=pData->sat.load(), ll=pData->light.load();
                                int rr,gg,bb; HslToRgb(hh,ss,ll,rr,gg,bb);
                                pData->curR.store(rr); pData->curG.store(gg); pData->curB.store(bb);
                            } else { // R/G/B → recalc HSL
                                int r2=pData->curR.load(),g2=pData->curG.load(),b2=pData->curB.load();
                                auto hsl2=RgbToHsl(r2,g2,b2);
                                pData->hue.store(hsl2.h); pData->sat.store(hsl2.s); pData->light.store(hsl2.l);
                            }
                            pData->SyncFromRgbToInputs();
                            InvalidateRect(hwnd,nullptr,TRUE);
                            return true;
                        }
                        return false;
                    };
                    if(hitField(pData->sliderR,false)||hitField(pData->sliderG,false)||hitField(pData->sliderB,false)
                        ||(pData->enableAlpha && hitField(pData->sliderA,false))
                        ||hitField(pData->sliderH,true)||hitField(pData->sliderS,true)||hitField(pData->sliderL,true))
                        return 0;
                }
                break;
            }

            case WM_MOUSEMOVE:
            {
                if (pData->draggingCanvas.load())
                {
                    int mx2 = GET_X_LPARAM(lp), my2 = GET_Y_LPARAM(lp);
                    int cxc2 = WX + WSIZE / 2, cyc2 = WY + WSIZE / 2, r2 = WSIZE / 2 - 4;
                    double dx2 = mx2 - cxc2, dy2 = my2 - cyc2, dist2 = std::sqrt(dx2 * dx2 + dy2 * dy2);
                    if (dist2 > r2)
                        dist2 = r2;
                    double ang2 = std::atan2(dy2, dx2) * 180.0 / 3.14159265;
                    if (ang2 < 0)
                        ang2 += 360;
                    pData->hue.store(ang2);
                    pData->sat.store(dist2 / r2);
                    int rr, gg, bb;
                    HslToRgb(ang2, pData->sat.load(), pData->light.load(), rr, gg, bb);
                    pData->curR.store(rr);
                    pData->curG.store(gg);
                    pData->curB.store(bb);
                    pData->SyncFromRgbToInputs();
                    pData->CallCallback(DynamicColorCallbackMessageType::Dragging);
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                }
                if (pData->draggingHue.load())
                {
                    int mx2 = GET_X_LPARAM(lp);
                    RECT lsRc2 = {SLIDER_X, WY + WSIZE + 20, 0, WY + WSIZE + 20 + SLIDER_H};
                    RECT cl3;
                    GetClientRect(hwnd, &cl3);
                    lsRc2.right = cl3.right - PAD;
                    double lv2 = (std::max)(0.0, (std::min)(1.0, (double)(mx2 - lsRc2.left) / (lsRc2.right - lsRc2.left - 1)));
                    pData->light.store(lv2);
                    int rr, gg, bb;
                    HslToRgb(pData->hue.load(), pData->sat.load(), lv2, rr, gg, bb);
                    pData->curR.store(rr);
                    pData->curG.store(gg);
                    pData->curB.store(bb);
                    pData->SyncFromRgbToInputs();
                    pData->CallCallback(DynamicColorCallbackMessageType::Dragging);
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                }
                if (pData->enableAlpha && pData->draggingAlpha.load())
                {
                    int mx2 = GET_X_LPARAM(lp);
                    RECT aRc2 = {SLIDER_X, WY + WSIZE + 28, 0, WY + WSIZE + 28 + SLIDER_H};
                    RECT cl4;
                    GetClientRect(hwnd, &cl4);
                    aRc2.right = cl4.right - PAD;
                    int aa2 = (int)((std::max)(0.0, (std::min)(1.0, (double)(mx2 - aRc2.left) / (aRc2.right - aRc2.left - 1))) * 255);
                    pData->curA.store(aa2);
                    pData->SyncFromRgbToInputs();
                    pData->CallCallback(DynamicColorCallbackMessageType::Dragging);
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                }
                // ── Field sliders dragging ──
                {
                    auto dragField = [&](auto &fs, bool isHSL) -> bool {
                        if(!fs.dragging) return false;
                        int mx2 = GET_X_LPARAM(lp);
                        int dv = fs.DisplayFromPos(mx2);
                        fs.SetFromDisplay(dv);
                        if(isHSL){
                            double hh=pData->hue.load(), ss=pData->sat.load(), ll=pData->light.load();
                            int rr,gg,bb; HslToRgb(hh,ss,ll,rr,gg,bb);
                            pData->curR.store(rr); pData->curG.store(gg); pData->curB.store(bb);
                        } else {
                            int r2=pData->curR.load(),g2=pData->curG.load(),b2=pData->curB.load();
                            auto hsl2=RgbToHsl(r2,g2,b2);
                            pData->hue.store(hsl2.h); pData->sat.store(hsl2.s); pData->light.store(hsl2.l);
                        }
                        pData->SyncFromRgbToInputs();
                        pData->CallCallback(DynamicColorCallbackMessageType::Dragging);
                        InvalidateRect(hwnd,nullptr,TRUE);
                        return true;
                    };
                    if(dragField(pData->sliderR,false)||dragField(pData->sliderG,false)||dragField(pData->sliderB,false)
                        ||(pData->enableAlpha && dragField(pData->sliderA,false))
                        ||dragField(pData->sliderH,true)||dragField(pData->sliderS,true)||dragField(pData->sliderL,true))
                        return 0;
                }
                break;
            }

            case WM_LBUTTONUP:
                pData->draggingCanvas.store(false);
                pData->draggingHue.store(false);
                pData->draggingAlpha.store(false);
                pData->sliderR.dragging=false; pData->sliderG.dragging=false;
                pData->sliderB.dragging=false; pData->sliderA.dragging=false;
                pData->sliderH.dragging=false; pData->sliderS.dragging=false;
                pData->sliderL.dragging=false;
                pData->CallCallback(DynamicColorCallbackMessageType::Released);
                ReleaseCapture();
                break;

            case WM_COMMAND:
            {
                int id = LOWORD(wp), code = HIWORD(wp);
                if (id == IDOK)
                {
                    pData->parent->PostMessageToThread(ColorPickerMessageType::Close);
                    return 0;
                }
                if (id == IDCANCEL)
                {
                    pData->curR.store(pData->oldR.load());
                    pData->curG.store(pData->oldG.load());
                    pData->curB.store(pData->oldB.load());
                    pData->curA.store(pData->oldA.load());
                    pData->CallCallback(DynamicColorCallbackMessageType::Released);
                    pData->parent->PostMessageToThread(ColorPickerMessageType::Close);
                    return 0;
                }
                if (id == 3020) // Eyedropper button
                {
                    if (!pData->eyeDropperMode.load())
                    {
                        auto hInst = GetModuleHandleW(nullptr);
                        int scrW = GetSystemMetrics(SM_CXSCREEN);
                        int scrH = GetSystemMetrics(SM_CYSCREEN);

                        // Create full-screen overlay
                        pData->hOverlay = CreateWindowExW(
                            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                            L"GL_Commdlg.EyeOverlayClass", nullptr,
                            WS_POPUP,
                            0, 0, scrW, scrH,
                            nullptr, nullptr, hInst, pData);

                        if (pData->hOverlay)
                        {
                            SetLayeredWindowAttributes(pData->hOverlay, 0, 1, LWA_ALPHA);

                            // Create preview popup
                            pData->hEyePreview = CreateWindowExW(
                                WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                L"GL_Commdlg.EyePreviewClass", nullptr,
                                WS_POPUP,
                                0, 0, 200, 72,
                                nullptr, nullptr, hInst, pData);

                            pData->eyeDropperMode.store(true);

                            // Hide main dialog, show overlay + preview
                            ShowWindow(hwnd, SW_HIDE);
                            ShowWindow(pData->hOverlay, SW_SHOW);
                            SetForegroundWindow(pData->hOverlay);
                            SetFocus(pData->hOverlay);

                            // Sample initial color at cursor
                            POINT pt;
                            GetCursorPos(&pt);
                            HDC hdcScreen = GetDC(nullptr);
                            COLORREF col = GetPixel(hdcScreen, pt.x, pt.y);
                            ReleaseDC(nullptr, hdcScreen);
                            pData->eyePreviewR = GetRValue(col);
                            pData->eyePreviewG = GetGValue(col);
                            pData->eyePreviewB = GetBValue(col);

                            if (pData->hEyePreview)
                            {
                                SetWindowPos(pData->hEyePreview, HWND_TOPMOST, pt.x + 20, pt.y + 20, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
                                ShowWindow(pData->hEyePreview, SW_SHOWNOACTIVATE);
                            }

                            // Create magnifier window (initially hidden)
                            pData->hMagnifier = CreateWindowExW(
                                WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                L"GL_Commdlg.EyeMagnifierClass", nullptr,
                                WS_POPUP,
                                0, 0, 150, 150,
                                nullptr, nullptr, hInst, pData);
                            pData->zoomLevel.store(2);
                        }
                    }
                    return 0;
                }
                if (id == 3005 && code == EN_CHANGE && !pData->updatingInputs)
                {
                    // HEX was already handled in both ReadInputsToRgb and SyncFromRgbToInputs
                }
                if (code == EN_CHANGE && !pData->updatingInputs)
                {
                    pData->ReadInputsToRgb();
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
                return 0;
            }

            // ── Exit eyedropper mode ──
            case WM_USER + 100:
            {
                bool picked = (wp != 0);

                // Destroy magnifier
                if (pData->hMagnifier && IsWindow(pData->hMagnifier))
                {
                    DestroyWindow(pData->hMagnifier);
                    pData->hMagnifier = nullptr;
                }
                // Destroy preview
                if (pData->hEyePreview && IsWindow(pData->hEyePreview))
                {
                    DestroyWindow(pData->hEyePreview);
                    pData->hEyePreview = nullptr;
                }
                // Destroy overlay
                if (pData->hOverlay && IsWindow(pData->hOverlay))
                {
                    DestroyWindow(pData->hOverlay);
                    pData->hOverlay = nullptr;
                }
                pData->eyeDropperMode.store(false);

                // Show main dialog
                if (IsWindow(hwnd))
                {
                    ShowWindow(hwnd, SW_SHOW);
                    SetForegroundWindow(hwnd);
                    SetFocus(hwnd);
                    pData->SyncFromRgbToInputs();
                    pData->CallCallback(picked ? DynamicColorCallbackMessageType::Released
                                               : DynamicColorCallbackMessageType::Dragging);
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
                return 0;
            }

            case WM_CLOSE:
                pData->parent->PostMessageToThread(ColorPickerMessageType::Close);
                return 0;

            case WM_DESTROY:
                pData->isFinished.store(true);

                // Cleanup eyedropper resources (if active)
                if (pData->hMagnifier && IsWindow(pData->hMagnifier))
                {
                    DestroyWindow(pData->hMagnifier);
                    pData->hMagnifier = nullptr;
                }
                if (pData->hEyePreview && IsWindow(pData->hEyePreview))
                {
                    DestroyWindow(pData->hEyePreview);
                    pData->hEyePreview = nullptr;
                }
                if (pData->hOverlay && IsWindow(pData->hOverlay))
                {
                    DestroyWindow(pData->hOverlay);
                    pData->hOverlay = nullptr;
                }

                if (pData->hMemDC)
                    DeleteDC(pData->hMemDC);
                if (pData->hMemBmp)
                    DeleteObject(pData->hMemBmp);
                if (pData->hCachedDC)
                    DeleteDC(pData->hCachedDC);
                if (pData->hCachedWheel)
                    DeleteObject(pData->hCachedWheel);
                if (pData->hCheckerBrush)
                    DeleteObject(pData->hCheckerBrush);
                if (pData->hFont)
                    DeleteObject(pData->hFont);
                pData->hwnd = nullptr;
                PostQuitMessage(0);
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        void DialogThreadProc()
        {
            if (!IsWindowClassRegistered())
            {
                threadRunning.store(false);
                return;
            }

            int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
            int ww = 520;
            int wh = dialogData->enableAlpha ? 520 : 488;
            int x = (sw - ww) / 2, y = (sh - wh) / 2;

            HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_COMPOSITED, L"DynamicColorPickerClass",
                                        dialogData->title.c_str(), WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                        x, y, ww, wh, dialogData->hwndParent, nullptr,
                                        GetModuleHandleW(nullptr), dialogData.get());
            if (!hwnd)
            {
                threadRunning.store(false);
                return;
            }
            dialogData->hwnd = hwnd;
            ShowWindow(hwnd, SW_SHOW);
            UpdateWindow(hwnd);

            MSG msg;
            while (threadRunning.load())
            {
                {
                    std::unique_lock<std::mutex> lock(messageMutex);
                    messageCV.wait_for(lock, std::chrono::milliseconds(10),
                                       [this]
                                       { return !messageQueue.empty(); });
                    while (!messageQueue.empty())
                    {
                        auto m = messageQueue.front();
                        messageQueue.pop();
                        lock.unlock();
                        if (m.type == ColorPickerMessageType::Close)
                        {
                            dialogData->isFinished.store(true);
                            threadRunning.store(false);
                            if (dialogData->hwnd && IsWindow(dialogData->hwnd))
                                DestroyWindow(dialogData->hwnd);
                        }
                        else if (m.type == ColorPickerMessageType::SetCallback)
                        {
                            dialogData->SetCallback(std::move(m.callback));
                        }
                        lock.lock();
                    }
                }
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
                {
                    if (msg.message == WM_QUIT)
                    {
                        threadRunning.store(false);
                        break;
                    }
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                if (!IsWindow(hwnd) || dialogData->isFinished.load())
                    threadRunning.store(false);
            }
            if (IsWindow(hwnd))
                DestroyWindow(hwnd);
        }

        void PostMessageToThread(ColorPickerMessageType type, int r = 0, int g = 0, int b = 0, int a = 255)
        {
            std::lock_guard<std::mutex> lock(messageMutex);
            messageQueue.emplace(type, r, g, b, a);
            messageCV.notify_one();
        }

        void PostCallbackToThread(std::function<ColorRGBA(DynamicColorCallbackMessageType, ColorRGBA)> cb)
        {
            std::lock_guard<std::mutex> lock(messageMutex);
            messageQueue.emplace(ColorPickerMessageType::SetCallback, std::move(cb));
            messageCV.notify_one();
        }

    public:
        /**
         * @brief Create a dynamic color picker dialog
         * @brief 创建颜色选择器动态对话框实例
         * @param title Dialog title
         * @param initialColor Initial color (ColorRGBA)
         * @param hParent Parent window handle
         */
        DynamicColorPicker(const std::string &title, ColorRGBA initialColor = {255, 255, 255},
                           bool enableAlpha = true,
                           HWND hParent = nullptr,
                           std::function<ColorRGBA(DynamicColorCallbackMessageType, ColorRGBA)> callback = nullptr)
            : dialogData(std::make_shared<DialogData>())
        {
            dialogData->parent = this;
            dialogData->hwndParent = hParent;
            dialogData->enableAlpha.store(enableAlpha);
            dialogData->title = utf8ToWide(title);
            if (callback)
                dialogData->SetCallback(std::move(callback));
            dialogData->curR.store(initialColor.r);
            dialogData->curG.store(initialColor.g);
            dialogData->curB.store(initialColor.b);
            dialogData->curA.store(initialColor.a);
            dialogData->oldR.store(initialColor.r);
            dialogData->oldG.store(initialColor.g);
            dialogData->oldB.store(initialColor.b);
            dialogData->oldA.store(initialColor.a);
            auto hsl = RgbToHsl(initialColor.r, initialColor.g, initialColor.b);
            dialogData->hue.store(hsl.h);
            dialogData->sat.store(hsl.s);
            dialogData->light.store(hsl.l);
            threadRunning.store(true);
            dialogThread = std::make_unique<std::thread>([this]
                                                         { DialogThreadProc(); });
        }

        ~DynamicColorPicker() override
        {
            Close();
            if (dialogThread && dialogThread->joinable())
                dialogThread->join();
        }

        DynamicColorPicker(DynamicColorPicker &&other) noexcept
            : dialogThread(std::move(other.dialogThread)),
              threadRunning(other.threadRunning.load()),
              dialogData(std::move(other.dialogData)) {}

        DynamicColorPicker &operator=(DynamicColorPicker &&other) noexcept
        {
            if (this != &other)
            {
                Close();
                if (dialogThread && dialogThread->joinable())
                    dialogThread->join();
                dialogThread = std::move(other.dialogThread);
                threadRunning = other.threadRunning.load();
                dialogData = std::move(other.dialogData);
            }
            return *this;
        }

        void Show() override
        {
            if (dialogData && dialogData->hwnd)
                ShowWindow(dialogData->hwnd, SW_SHOW);
        }

        void Close() override
        {
            if (threadRunning.load() && !dialogData->isFinished.load())
                PostMessageToThread(ColorPickerMessageType::Close);
        }

        /**
         * @brief Set a callback function for color change events
         * @brief 设置颜色改变时的回调函数
         * @param cb Callback function, receives (event type, current color) and returns potentially modified color
         * @param cb 回调函数，接收(事件类型, 当前颜色)并返回可能被修改的颜色
         */
        void SetCallback(std::function<ColorRGBA(DynamicColorCallbackMessageType, ColorRGBA)> cb)
        {
            if (!threadRunning.load() || dialogData->isFinished.load())
                return;
            PostCallbackToThread(std::move(cb));
        }

        bool IsFinished() const { return dialogData ? dialogData->isFinished.load() : true; }

        /** @brief Get the current color values */
        ColorRGBA GetColor() const
        {
            if (!dialogData)
                throw std::runtime_error("dialogData is NULL");
            ColorRGBA color;
            color.r = (uint8_t)dialogData->curR.load();
            color.g = (uint8_t)dialogData->curG.load();
            color.b = (uint8_t)dialogData->curB.load();
            color.a = (uint8_t)dialogData->curA.load();
            return color;
        }

        HWND GetWindowHandle() const { return dialogData ? dialogData->hwnd : nullptr; }
    };

    /**
     * @brief Create a dynamic color picker dialog instance
     * @brief 创建颜色选择器动态对话框实例
     * @param title Dialog title
     * @param title 对话框标题
     * @param initialColor Initial color (ColorRGBA)
     * @param initialColor 初始颜色 (ColorRGBA)
     * @param enableAlpha Whether to enable alpha channel editing
     * @param enableAlpha 是否启用 Alpha 通道编辑
     * @param hParent Parent window handle
     * @param hParent 父窗口句柄
     * @return The created dynamic color picker instance
     * @return 创建的颜色选择器动态对话框实例
     */
    DynamicColorPicker CreateDynamicColorPicker(const std::string &title,
                                                ColorRGBA initialColor = {255, 255, 255},
                                                bool enableAlpha = true,
                                                HWND hParent = nullptr,
                                                std::function<ColorRGBA(DynamicColorPicker::DynamicColorCallbackMessageType, ColorRGBA)> callback = nullptr)
    {
        return DynamicColorPicker(title, initialColor, enableAlpha, hParent, std::move(callback));
    }
}

#endif