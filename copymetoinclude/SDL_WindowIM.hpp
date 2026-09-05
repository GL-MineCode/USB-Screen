/**
 * @file SDL_WindowIM.hpp
 * @author GL
 * @brief SDL_Window*的封装类，并拓展了特定平台的独有窗口操作
 * @version 2.0
 * 
 */

#ifndef __INC_SDL_WINDOWIM_
#define __INC_SDL_WINDOWIM_

#include <SDL3/SDL.h>
#include <string>
#include <stdexcept>

//选择性配置功能
//#define WINDOWIM_TRANSPARENT_MOUSE_PASSTHROUGH

#ifdef WIN32
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <imm.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "comctl32.lib")

namespace SDL_WindowIM
{
    HWND _GetWindowHandleFromSDLWindow(SDL_Window *win)
    {
        // SDL3移除了SDL_GetWindowWMInfo/SDL_syswm.h，HWND通过窗口属性获取
        return (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(win), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    }

    void _SetIMState(HWND hwnd, bool state)
    {
        HIMC himc = ImmGetContext(hwnd);
        ImmSetOpenStatus(himc, state);
        ImmReleaseContext(hwnd, himc);
    }

    bool _GetIMState(HWND hwnd)
    {
        HIMC himc = ImmGetContext(hwnd);
        bool state = ImmGetOpenStatus(himc);
        ImmReleaseContext(hwnd, himc);
        return state;
    }
}

#endif // WIN32

namespace SDL_WindowIM
{
    class WindowIM
    {
        bool own_window;
        SDL_Window *sdl_window;
#ifdef WIN32
        HWND hwnd;
#endif // WIN32
    public:
        /**
         * @brief 从SDL_Window创建WindowIM对象
         *
         * @param win SDL_Window句柄
         *
         * @note WindowIM不会自动销毁传入的SDL_Window，除非显式调用Destroy()，需要手动销毁SDL_Window
         */
        WindowIM(SDL_Window *win);
        /**
         * @brief 创建空的WindowIM对象
         *
         */
        WindowIM();

        /**
         * @brief 创建窗口
         *
         * @param title 窗口标题
         * @param w 窗口宽度
         * @param h 窗口高度
         * @param flags SDL_Window标志位
         * 
         * @note SDL3的SDL_CreateWindow不再接受x/y坐标参数，窗口默认居中显示
         *       （如需指定位置，可在创建后调用SetPosition()）
         *
         * @throw std::runtime_error 如果创建窗口失败
         */
        void Create(const std::string &title, int w, int h, Uint32 flags);

        /**
         * @brief 销毁窗口
         * 
         */
        void Destroy();

        /**
         * @brief 获取原始SDL_Window指针
         *
         * @return SDL_Window指针，未创建时返回nullptr
         */
        SDL_Window *Get() const;

        /**
         * @brief 判断窗口是否有效（非空）
         *
         * @return 是否有效
         */
        bool IsValid() const;

        /**
         * @brief 设置窗口标题
         *
         * @param title 目标标题
         */
        void SetTitle(const std::string &title);

        /**
         * @brief 获取窗口标题
         *
         * @return 窗口标题字符串
         */
        std::string GetTitle() const;

        /**
         * @brief 设置窗口位置
         *
         * @param x 目标X坐标
         * @param y 目标Y坐标
         */
        void SetPosition(int x, int y);

        /**
         * @brief 获取窗口位置
         *
         * @param x 输出X坐标
         * @param y 输出Y坐标
         */
        void GetPosition(int *x, int *y) const;

        /**
         * @brief 设置窗口大小
         *
         * @param w 目标宽度
         * @param h 目标高度
         */
        void SetSize(int w, int h);

        /**
         * @brief 获取窗口大小
         *
         * @param w 输出宽度
         * @param h 输出高度
         */
        void GetSize(int *w, int *h) const;

        /**
         * @brief 设置窗口最小尺寸
         *
         * @param minW 最小宽度
         * @param minH 最小高度
         */
        void SetMinimumSize(int minW, int minH);

        /**
         * @brief 获取窗口最小尺寸
         *
         * @param minW 输出最小宽度
         * @param minH 输出最小高度
         */
        void GetMinimumSize(int *minW, int *minH) const;

        /**
         * @brief 设置窗口最大尺寸
         *
         * @param maxW 最大宽度
         * @param maxH 最大高度
         */
        void SetMaximumSize(int maxW, int maxH);

        /**
         * @brief 获取窗口最大尺寸
         *
         * @param maxW 输出最大宽度
         * @param maxH 输出最大高度
         */
        void GetMaximumSize(int *maxW, int *maxH) const;

        /**
         * @brief 设置窗口是否显示边框
         *
         * @param bordered 是否显示边框
         */
        void SetBordered(bool bordered);

        /**
         * @brief 设置窗口是否可调整大小
         *
         * @param resizable 是否可调整大小
         */
        void SetResizable(bool resizable);

        /**
         * @brief 设置窗口是否始终置顶
         *
         * @param onTop 是否始终置顶
         */
        void SetAlwaysOnTop(bool onTop);

        /**
         * @brief 设置窗口不透明度
         *
         * @param opacity 不透明度(0.0~1.0)
         */
        void SetOpacity(float opacity);

        /**
         * @brief 获取窗口不透明度
         *
         * @return 当前不透明度(0.0~1.0)
         */
        float GetOpacity() const;

        /**
         * @brief 设置鼠标是否锁定在窗口内
         *
         * @param grabbed 是否锁定
         */
        void SetGrab(bool grabbed);

        /**
         * @brief 获取鼠标是否锁定在窗口内
         *
         * @return 是否锁定
         */
        bool GetGrab() const;

        /**
         * @brief 设置窗口图标
         *
         * @param icon 图标Surface
         */
        void SetIcon(SDL_Surface *icon);

        /**
         * @brief 获取窗口标志位
         *
         * @return SDL_WindowFlags
         */
        Uint32 GetFlags() const;

        /**
         * @brief 显示窗口
         */
        void Show();

        /**
         * @brief 隐藏窗口
         */
        void Hide();

        /**
         * @brief 将窗口提升到最前
         */
        void Raise();

        /**
         * @brief 最小化窗口
         */
        void Minimize();

        /**
         * @brief 最大化窗口
         */
        void Maximize();

        /**
         * @brief 恢复窗口（从最小化/最大化恢复）
         */
        void Restore();

        /**
         * @brief 设置全屏模式
         *
         * @param flags 非0进入全屏，0退出全屏
         *
         * @note SDL3中SDL_SetWindowFullscreen参数由flags改为bool。
         *       如需指定独占全屏模式，可使用SDL_SetWindowFullscreenMode()
         */
        void SetFullscreen(Uint32 flags);

        /**
         * @brief 获取窗口ID
         *
         * @return 窗口ID
         */
        Uint32 GetID() const;

        /**
         * @brief 获取窗口所在显示器索引
         *
         * @return 显示器索引，失败返回-1
         */
        int GetDisplayIndex() const;

        /**
         * @brief 设置窗口命中测试回调函数
         * 
         * @param callback 回调函数
         * @param data 回调函数用户数据
         */
        bool SetHitTest(SDL_HitTest callback, void* data);

#ifdef WIN32

        /**
         * @brief 获取窗口句柄
         * 
         * @return HWND 窗口句柄
         */
        HWND GetHWND();

        /**
         * @brief 设置窗口颜色键
         *
         * @param color 目标窗口颜色键颜色
         *
         * @note 在Windows平台，这与SDL2原生的窗口半透明是冲突的，半透明和颜色键只有一个会生效
         */
        void SetColorkey(SDL_Color color);

        /**
         * @brief 强制将窗口置于最前端，这比原生SDL2 API的置顶更有效
         *
         * @note 使用了AttachThreadInput函数，需要在创建窗口的线程调用才能生效
         */
        void ForceTop();

        /**
         * @brief 强制将窗口置顶，并保持在最前端状态，这比原生SDL2 API的置顶更有效
         *
         * @note 使用了AttachThreadInput函数，需要在创建窗口的线程调用才能生效
         */
        void ForceTopmost();

        /**
         * @brief 设置窗口输入法状态
         *
         * @param state 是否开启输入法
         *
         * @note 这能避免在游玩时输入法弹出，影响游戏体验
         */
        void SetIMState(bool state);

        /**
         * @brief 获取窗口输入法状态
         *
         * @return 是否开启输入法
         */
        bool GetIMState();

        /**
         * @brief 设置窗口标题文本颜色
         *
         * @param color 目标窗口标题文本颜色
         *
         * @note 仅在Windows11上生效
         */
        void SetTitleColor(SDL_Color color);

        /**
         * @brief 设置窗口标题栏颜色
         *
         * @param color 目标窗口标题栏颜色
         *
         * @note 仅在Windows11上生效
         */
        void SetCaptionColor(SDL_Color color);

        /**
         * @brief 设置窗口边框颜色
         *
         * @param color 目标窗口边框颜色
         *
         * @note 仅在Windows11上生效
         */
        void SetBorderColor(SDL_Color color);

        /**
         * @brief 设置窗口圆角偏好
         *
         * @param state 目标窗口圆角偏好
         * @note 仅在Windows11上生效
         */
        void SetCornerPreference(DWM_WINDOW_CORNER_PREFERENCE state);

        /**
         * @brief 设置窗口暗黑模式
         *
         * @param isDark 是否开启暗黑模式
         * @note 仅在Windows11上生效
         */
        void SetDarkMode(bool isDark);

        /**
         * @brief 设置窗口是否绘制非标题栏区域背景(如窗口阴影)
         * 
         * @param state 是否绘制非标题栏区域背景
         */
        //void SetNCPaint(bool state);

        /**
         * @brief 子类化窗口，用于自定义窗口消息处理
         * 
         * @param proc 子类化窗口过程函数指针
         * @param id 标识子类化的窗口ID
         * @param userdata 用户数据指针
         * @return 是否成功子类化窗口
         */
        bool Subclass(SUBCLASSPROC proc,UINT_PTR id,void* userdata);

        /**
         * @brief 取消窗口子类化
         * @param id 标识子类化的窗口ID
         * @return 是否成功取消子类化窗口
         */
        bool Unsubclass(SUBCLASSPROC proc,UINT_PTR id);

#endif // WIN32

        ~WindowIM();
    };
}

#ifdef SDL_WINDOWIM_IMPLEMENTATION

namespace SDL_WindowIM
{
    WindowIM::WindowIM(SDL_Window *win) : own_window(false), sdl_window(win)
    {
#ifdef WIN32
        hwnd = _GetWindowHandleFromSDLWindow(win);
#endif // WIN32
    }

    WindowIM::WindowIM() : own_window(false), sdl_window(nullptr)
    {
#ifdef WIN32
        hwnd = nullptr;
#endif // WIN32
    }

    WindowIM::~WindowIM()
    {
        //防止在SDL_Quit之后多次释放窗口
        if(!SDL_WasInit(SDL_INIT_VIDEO)) return;
        if (own_window)
        {
            if (sdl_window)
                SDL_DestroyWindow(sdl_window);
        }
    }

    void WindowIM::Create(const std::string &title, int w, int h, Uint32 flags)
    {
        this->Destroy();
        // SDL3的SDL_CreateWindow不再接受x/y坐标参数
        sdl_window = SDL_CreateWindow(title.c_str(), w, h, flags);
        if(!sdl_window){
            throw std::runtime_error("SDL_CreateWindow failed: " + std::string(SDL_GetError()));
        }
        own_window = true;
#ifdef WIN32
        hwnd = sdl_window ? _GetWindowHandleFromSDLWindow(sdl_window) : nullptr;
#endif // WIN32
    }

    void WindowIM::Destroy()
    {
        //防止在SDL_Quit之后多次释放窗口
        if(!SDL_WasInit(SDL_INIT_VIDEO)){
            sdl_window = nullptr;
            own_window = false;
#ifdef WIN32
            hwnd = nullptr;
#endif // WIN32
            return;
        }
        if (own_window && sdl_window)
        {
            SDL_DestroyWindow(sdl_window);
        }
        sdl_window = nullptr;
        own_window = false;
#ifdef WIN32
        hwnd = nullptr;
#endif // WIN32
    }

    SDL_Window *WindowIM::Get() const
    {
        return sdl_window;
    }

    bool WindowIM::IsValid() const
    {
        return sdl_window != nullptr;
    }

    void WindowIM::SetTitle(const std::string &title)
    {
        SDL_SetWindowTitle(sdl_window, title.c_str());
    }

    std::string WindowIM::GetTitle() const
    {
        const char *title = SDL_GetWindowTitle(sdl_window);
        return title ? title : "";
    }

    void WindowIM::SetPosition(int x, int y)
    {
        SDL_SetWindowPosition(sdl_window, x, y);
    }

    void WindowIM::GetPosition(int *x, int *y) const
    {
        SDL_GetWindowPosition(sdl_window, x, y);
    }

    void WindowIM::SetSize(int w, int h)
    {
        SDL_SetWindowSize(sdl_window, w, h);
    }

    void WindowIM::GetSize(int *w, int *h) const
    {
        SDL_GetWindowSize(sdl_window, w, h);
    }

    void WindowIM::SetMinimumSize(int minW, int minH)
    {
        SDL_SetWindowMinimumSize(sdl_window, minW, minH);
    }

    void WindowIM::GetMinimumSize(int *minW, int *minH) const
    {
        SDL_GetWindowMinimumSize(sdl_window, minW, minH);
    }

    void WindowIM::SetMaximumSize(int maxW, int maxH)
    {
        SDL_SetWindowMaximumSize(sdl_window, maxW, maxH);
    }

    void WindowIM::GetMaximumSize(int *maxW, int *maxH) const
    {
        SDL_GetWindowMaximumSize(sdl_window, maxW, maxH);
    }

    void WindowIM::SetBordered(bool bordered)
    {
        // SDL3中该函数直接接受bool
        SDL_SetWindowBordered(sdl_window, bordered);
    }

    void WindowIM::SetResizable(bool resizable)
    {
        SDL_SetWindowResizable(sdl_window, resizable);
    }

    void WindowIM::SetAlwaysOnTop(bool onTop)
    {
        SDL_SetWindowAlwaysOnTop(sdl_window, onTop);
    }

    void WindowIM::SetOpacity(float opacity)
    {
        SDL_SetWindowOpacity(sdl_window, opacity);
    }

    float WindowIM::GetOpacity() const
    {
        // SDL3中SDL_GetWindowOpacity直接返回float
        return SDL_GetWindowOpacity(sdl_window);
    }

    void WindowIM::SetGrab(bool grabbed)
    {
        // SDL3中SDL_SetWindowGrab改名为SDL_SetWindowMouseGrab
        SDL_SetWindowMouseGrab(sdl_window, grabbed);
    }

    bool WindowIM::GetGrab() const
    {
        return SDL_GetWindowMouseGrab(sdl_window);
    }

    void WindowIM::SetIcon(SDL_Surface *icon)
    {
        SDL_SetWindowIcon(sdl_window, icon);
    }

    Uint32 WindowIM::GetFlags() const
    {
        return SDL_GetWindowFlags(sdl_window);
    }

    void WindowIM::Show()
    {
        SDL_ShowWindow(sdl_window);
    }

    void WindowIM::Hide()
    {
        SDL_HideWindow(sdl_window);
    }

    void WindowIM::Raise()
    {
        SDL_RaiseWindow(sdl_window);
    }

    void WindowIM::Minimize()
    {
        SDL_MinimizeWindow(sdl_window);
    }

    void WindowIM::Maximize()
    {
        SDL_MaximizeWindow(sdl_window);
    }

    void WindowIM::Restore()
    {
        SDL_RestoreWindow(sdl_window);
    }

    void WindowIM::SetFullscreen(Uint32 flags)
    {
        // SDL3中SDL_SetWindowFullscreen参数由flags改为bool：非0进入全屏，0退出全屏
        SDL_SetWindowFullscreen(sdl_window, flags != 0);
    }

    Uint32 WindowIM::GetID() const
    {
        return SDL_GetWindowID(sdl_window);
    }

    int WindowIM::GetDisplayIndex() const
    {
        // SDL3中SDL_GetWindowDisplayIndex改名为SDL_GetDisplayForWindow（返回SDL_DisplayID），
        // 再通过SDL_GetDisplays找到对应的索引
        int count = 0;
        SDL_DisplayID target = SDL_GetDisplayForWindow(sdl_window);
        SDL_DisplayID *displays = SDL_GetDisplays(&count);
        int index = -1;
        if (displays)
        {
            for (int i = 0; i < count; ++i)
            {
                if (displays[i] == target)
                {
                    index = i;
                    break;
                }
            }
            SDL_free(displays);
        }
        return index;
    }

    bool WindowIM::SetHitTest(SDL_HitTest callback, void* data)
    {
        return SDL_SetWindowHitTest(sdl_window, callback, data);
    }

#ifdef WINDOWIM_TRANSPARENT_MOUSE_PASSTHROUGH

    void SetTransparentMousePassthrough(){

    }

#endif

#ifdef WIN32
    HWND WindowIM::GetHWND()
    {
        return hwnd;
    }

    void WindowIM::SetColorkey(SDL_Color color)
    {
        // 先查看是否存在WS_EX_LAYERED窗口属性
        LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        if (!(exStyle & WS_EX_LAYERED))
        {
            // 如果不存在WS_EX_LAYERED窗口属性，则添加
            SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
        }
        SetLayeredWindowAttributes(hwnd, RGB(color.r, color.g, color.b), 0, LWA_COLORKEY);
    }

    void WindowIM::ForceTop()
    {
        HWND hForeWnd = NULL;
        DWORD dwForeID = 0;
        DWORD dwCurID = 0;
        hForeWnd = GetForegroundWindow();
        dwCurID = GetCurrentThreadId();
        dwForeID = GetWindowThreadProcessId(hForeWnd, NULL);
        AttachThreadInput(dwCurID, dwForeID, TRUE);
        ShowWindow(hwnd, SW_SHOWNORMAL);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
        SetForegroundWindow(hwnd);
        AttachThreadInput(dwCurID, dwForeID, FALSE);
    }

    void WindowIM::ForceTopmost()
    {
        HWND hForeWnd = NULL;
        DWORD dwForeID = 0;
        DWORD dwCurID = 0;
        hForeWnd = GetForegroundWindow();
        dwCurID = GetCurrentThreadId();
        dwForeID = GetWindowThreadProcessId(hForeWnd, NULL);
        AttachThreadInput(dwCurID, dwForeID, TRUE);
        ShowWindow(hwnd, SW_SHOWNORMAL);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
        SetForegroundWindow(hwnd);
        AttachThreadInput(dwCurID, dwForeID, FALSE);
    }

    void WindowIM::SetIMState(bool state)
    {
        _SetIMState(hwnd, state);
    }

    bool WindowIM::GetIMState()
    {
        return _GetIMState(hwnd);
    }

    void WindowIM::SetTitleColor(SDL_Color color)
    {
        COLORREF text_color = RGB(color.r, color.g, color.b);
        DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &text_color, sizeof(COLORREF));
    }

    void WindowIM::SetCaptionColor(SDL_Color color)
    {
        COLORREF caption_color = RGB(color.r, color.g, color.b);
        DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &caption_color, sizeof(COLORREF));
    }

    void WindowIM::SetBorderColor(SDL_Color color)
    {
        COLORREF border_color = RGB(color.r, color.g, color.b);
        DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border_color, sizeof(COLORREF));
    }

    void WindowIM::SetCornerPreference(DWM_WINDOW_CORNER_PREFERENCE state)
    {
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &state, sizeof(DWM_WINDOW_CORNER_PREFERENCE));
    }

    void WindowIM::SetDarkMode(bool isDark)
    {
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &isDark, sizeof(BOOL));
    }

    bool WindowIM::Subclass(SUBCLASSPROC proc,UINT_PTR id,void* userdata)
    {
        return SetWindowSubclass(hwnd, proc, id, (DWORD_PTR)userdata);
    }

    bool WindowIM::Unsubclass(SUBCLASSPROC proc,UINT_PTR id)
    {
        return RemoveWindowSubclass(hwnd, proc, id);
    }

    // void WindowIM::SetNCPaint(bool state)
    // {
    //     DWMNCRENDERINGPOLICY policy = state ? DWMNCRP_DISABLED : DWMNCRP_ENABLED;
    //     DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &policy, sizeof(DWMNCRENDERINGPOLICY));
    // }
#endif // WIN32
}

#endif // SDL_WINDOWIM_IMPLEMENTATION

#endif // __INC_SDL_WINDOWIM_
