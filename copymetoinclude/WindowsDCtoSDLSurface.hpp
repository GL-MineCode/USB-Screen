#ifndef __INC_WINDOW_DC_TO_SDL_SURFACE_
#define __INC_WINDOW_DC_TO_SDL_SURFACE_

#include <Windows.h>
#include <SDL3/SDL.h>
#include <vector>
#include <mutex>

SDL_Surface* GetWindowSurface(HWND window) {
    HDC target = GetDC(window);
    if (!target) return nullptr;

    HDC hdcMemDC = CreateCompatibleDC(target);
    if (!hdcMemDC) {
        ReleaseDC(window, target);
        return nullptr;
    }

    RECT rect;
    if (!GetClientRect(window, &rect)) {
        DeleteDC(hdcMemDC);
        ReleaseDC(window, target);
        return nullptr;
    }

    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    HBITMAP hBitmap = CreateCompatibleBitmap(target, width, height);
    if (!hBitmap) {
        DeleteDC(hdcMemDC);
        ReleaseDC(window, target);
        return nullptr;
    }

    HGDIOBJ oldBitmap = SelectObject(hdcMemDC, hBitmap);
    BitBlt(hdcMemDC, 0, 0, width, height, target, 0, 0, SRCCOPY);

    SDL_Surface* sur = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBX8888);
    if (sur) {
        BITMAPINFOHEADER bi = {0};
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = width;
        bi.biHeight = -height;  // Top-down
        bi.biPlanes = 1;
        bi.biBitCount = 32;
        bi.biCompression = BI_RGB;

        SDL_LockSurface(sur);
        GetDIBits(hdcMemDC, hBitmap, 0, height, sur->pixels, (BITMAPINFO*)&bi, DIB_RGB_COLORS);
        SDL_UnlockSurface(sur);
    }

    SelectObject(hdcMemDC, oldBitmap);
    DeleteDC(hdcMemDC);
    ReleaseDC(window, target);
    DeleteObject(hBitmap);

    return sur;
}

class WindowCapturer {
    HWND window = nullptr;
    HDC target = nullptr;
    HDC hdcMemDC = nullptr;
    HBITMAP hBitmap = nullptr;
    mutable std::mutex mtx_;   // 保护捕获上下文/像素缓冲，支持跨线程 SetWindow/ReadPixel
public:
    int width = 0;
    int height = 0;
    std::vector<BYTE> pixelBuffer;

    // 指定目标窗口构造；之后可用 SetWindow 重新指定
    explicit WindowCapturer(HWND wnd = nullptr) {
        SetWindow(wnd);
    }

    ~WindowCapturer() {
        ReleaseContext();
    }

    WindowCapturer(const WindowCapturer&) = delete;
    void operator=(const WindowCapturer&) = delete;

    // ==================== 重新设置目标窗口 ====================
    // 释放旧窗口的捕获上下文，为 wnd 重建 DC/位图/像素缓冲。
    // 返回 false 表示窗口无效或初始化失败（可稍后重试或换窗口）。
    bool SetWindow(HWND wnd) {
        std::lock_guard<std::mutex> lk(mtx_);
        ReleaseContext();
        if (!wnd || !IsWindow(wnd)) return false;
        window = wnd;
        return RebuildContext();
    }

    // 当前捕获的目标窗口句柄（捕获桌面时为 GetDesktopWindow()）
    HWND Window() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return window;
    }

    // 捕获上下文是否已就绪
    bool IsValid() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return window && target && hdcMemDC && hBitmap && !pixelBuffer.empty();
    }

    // 自动检测目标窗口尺寸变化：检测到变化则重建上下文（缓冲自动扩容/收缩）。
    // 返回 true 表示尺寸已变化并重新适配；窗口销毁/最小化等非法状态返回 false 且保留原上下文。
    bool Refresh() {
        std::lock_guard<std::mutex> lk(mtx_);
        return RefreshLocked();
    }

    // 截图一帧（内部自动检测并适应目标窗口大小变化）
    void ReadPixel() {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!window) return;
        RefreshLocked();                     // 自动检测并适应目标窗口大小变化
        if (!hdcMemDC || !hBitmap) return;

        HGDIOBJ oldBitmap = SelectObject(hdcMemDC, hBitmap);
        BitBlt(hdcMemDC, 0, 0, width, height, target, 0, 0, SRCCOPY);

        BITMAPINFOHEADER bi = {0};
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = width;
        bi.biHeight = -height;
        bi.biPlanes = 1;
        bi.biBitCount = 32;
        bi.biCompression = BI_RGB;

        GetDIBits(hdcMemDC, hBitmap, 0, height, pixelBuffer.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);
        SelectObject(hdcMemDC, oldBitmap);
    }

    // 将最近一次 ReadPixel 的结果包装为 SDL_Surface*（引用内部缓冲，调用方负责 SDL_DestroySurface；
    // 返回后请勿并发调用 SetWindow/ReadPixel，否则该表面可能失效）
    SDL_Surface* GetSurface() const {
        std::lock_guard<std::mutex> lk(mtx_);
        if (pixelBuffer.empty()) return nullptr;
        return SDL_CreateSurfaceFrom(
            width, height, SDL_PIXELFORMAT_XRGB8888,
            (void*)pixelBuffer.data(), width * 4);
    }

private:
    // 假定 mtx_ 已持有：检测目标窗口尺寸变化并重建上下文
    bool RefreshLocked() {
        if (!window || !IsWindow(window)) return false;
        RECT rect;
        if (!GetClientRect(window, &rect)) return false;
        int w = rect.right - rect.left;
        int h = rect.bottom - rect.top;
        if (w == width && h == height) return false;   // 尺寸未变
        if (w <= 0 || h <= 0) return false;            // 非法尺寸(销毁/最小化)保留旧上下文
        return RebuildContext();
    }

    // 假定 mtx_ 已持有：按 window 当前客户区尺寸重建 DC/位图/像素缓冲
    bool RebuildContext() {
        RECT rect;
        if (!GetClientRect(window, &rect)) return false;
        int w = rect.right - rect.left;
        int h = rect.bottom - rect.top;
        if (w <= 0 || h <= 0) return false;

        HDC newTarget = GetDC(window);
        if (!newTarget) return false;
        HDC newMemDC = CreateCompatibleDC(newTarget);
        if (!newMemDC) {
            ReleaseDC(window, newTarget);
            return false;
        }
        HBITMAP newBitmap = CreateCompatibleBitmap(newTarget, w, h);
        if (!newBitmap) {
            DeleteDC(newMemDC);
            ReleaseDC(window, newTarget);
            return false;
        }

        // 新资源全部创建成功后再替换旧资源
        ReleaseContext();
        target    = newTarget;
        hdcMemDC  = newMemDC;
        hBitmap   = newBitmap;
        width     = w;
        height    = h;
        pixelBuffer.resize(static_cast<size_t>(w) * h * 4);
        return true;
    }

    // 假定 mtx_ 已持有：释放捕获上下文（保留 window 字段，便于重建）
    void ReleaseContext() {
        if (hdcMemDC) { DeleteDC(hdcMemDC); hdcMemDC = nullptr; }
        if (target) {
            if (window) ReleaseDC(window, target);
            target = nullptr;
        }
        if (hBitmap) { DeleteObject(hBitmap); hBitmap = nullptr; }
        pixelBuffer.clear();
        pixelBuffer.shrink_to_fit();
        width = 0;
        height = 0;
    }
};

#endif // __INC_WINDOW_DC_TO_SDL_SURFACE_
