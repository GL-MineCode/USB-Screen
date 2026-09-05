#ifndef __INC_SDL_EVENT_DISPATCHER_
#define __INC_SDL_EVENT_DISPATCHER_

/**
 * @file SDL_EventDispatcher.hpp
 * @author GL
 * @brief SDL事件分发器，解决多线程SDL事件拉取的问题，采用窗口订阅机制，每个线程订阅自己需要的窗口ID的事件，主线程内调用SDL_PollEvent拉取事件后，将事件分发给其他线程。其他线程调用`PollEvent`拉取自己的事件。
 * @version 1.0
 */

#include <SDL3/SDL.h>
#include <thread>
#include <unordered_map>
#include <vector>
#include <unordered_set>
#include <mutex>
#include <stdexcept>
#include <shared_mutex>
#include <queue>

#define EVENT_DISPATCHER_EVENT_QUEUE_MAX_SIZE 1024

class SDL_EventDispatcher{
    #define EVENT_DISPATCHER_FLAG_WANT_NON_WINDOW_RELATED_EVENTS 1

    class ThreadData{
        public:
        int m_Flags;
        std::queue<SDL_Event> m_QueueEvents;
        std::unordered_set<unsigned int> m_SubscribedWindowIDs;
        std::shared_mutex m_Mutex;
    };
    std::unordered_map<std::thread::id, ThreadData> m_ThreadDataMap;
    std::shared_mutex m_ThreadDataMapMutex;
public:
    SDL_EventDispatcher();
    ~SDL_EventDispatcher();

    /**
     * @brief 分发事件，必须在主线程调用
     * 
     * @param event 事件指针
     * 
     * @note 主线程无需调用`InitCurrentThread`初始化，直接调用即可分发事件。
     */
    void DispatchEvents(const SDL_Event* event);

    /**
     * @brief 初始化当前线程的事件分发器
     * 
     * @param windowIDs 订阅的窗口ID列表
     * @param flags 事件分发器标志位
     */
    void InitCurrentThread(const std::vector<unsigned int>& windowIDs = {},int flags = 0);

    /**
     * @brief 设置当前线程的事件分发器标志位
     * 
     * @param flags 事件分发器标志位
     * 
     * @note 必须先调用`InitCurrentThread`对本线程初始化，否则throw std::runtime_error
    */
    void SetCurrentThreadFlags(int flags);

    /**
     * @brief 订阅窗口ID的事件
     * 
     * @param windowID 窗口ID
     * 
     * @note 必须先调用`InitCurrentThread`对本线程初始化，否则throw std::runtime_error
    */
    void SubscribeWindow(unsigned int windowID);

    /**
     * @brief 取消订阅窗口ID的事件
     * 
     * @param windowID 窗口ID
     * 
     * @note 必须先调用`InitCurrentThread`对本线程初始化，否则throw std::runtime_error
    */
    void UnsubscribeWindow(unsigned int windowID);

    /**
     * @brief 获取当前线程的事件分发器标志位
     * 
     * @return int 事件分发器标志位
     * 
     * @note 必须先调用`InitCurrentThread`对本线程初始化，否则throw std::runtime_error
    */
    int GetCurrentThreadFlags();

    /**
     * @brief 和SDL_PollEvent类似，轮询所需事件，返回是否有事件可拉取
     * 
     * @param event 事件指针
     * @return bool 是否拉取到事件
     * 
     * @note 必须先调用`InitCurrentThread`对本线程初始化，否则throw std::runtime_error
     */
    bool PollEvent(SDL_Event* event);

};

#ifdef SDL_EVENT_DISPATCHER_IMPLEMENTATION

namespace {

/**
 * @brief 判断事件是否为"窗口相关"事件（即事件结构中携带有效的 windowID）
 */
bool IsWindowRelatedEvent(const SDL_Event* event)
{
    if (event == nullptr)
        return false;

    switch (event->type)
    {
        /* ---- 窗口事件 ---- */
        case SDL_EVENT_WINDOW_SHOWN:
        case SDL_EVENT_WINDOW_HIDDEN:
        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_MOVED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED:
        case SDL_EVENT_WINDOW_MINIMIZED:
        case SDL_EVENT_WINDOW_MAXIMIZED:
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        case SDL_EVENT_WINDOW_HIT_TEST:
        case SDL_EVENT_WINDOW_ICCPROF_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
        case SDL_EVENT_WINDOW_OCCLUDED:
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        case SDL_EVENT_WINDOW_DESTROYED:
        case SDL_EVENT_WINDOW_HDR_STATE_CHANGED:
        /* ---- 键盘 ---- */
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_TEXT_EDITING:
        case SDL_EVENT_TEXT_INPUT:
        case SDL_EVENT_TEXT_EDITING_CANDIDATES:
        /* ---- 鼠标 ---- */
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
        /* ---- 触摸 ---- */
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_CANCELED:
        /* ---- 捏合手势 ---- */
        case SDL_EVENT_PINCH_BEGIN:
        case SDL_EVENT_PINCH_UPDATE:
        case SDL_EVENT_PINCH_END:
        /* ---- 手写笔 ---- */
        case SDL_EVENT_PEN_PROXIMITY_IN:
        case SDL_EVENT_PEN_PROXIMITY_OUT:
        case SDL_EVENT_PEN_DOWN:
        case SDL_EVENT_PEN_UP:
        case SDL_EVENT_PEN_BUTTON_DOWN:
        case SDL_EVENT_PEN_BUTTON_UP:
        case SDL_EVENT_PEN_MOTION:
        case SDL_EVENT_PEN_AXIS:
        /* ---- 拖放 ---- */
        case SDL_EVENT_DROP_FILE:
        case SDL_EVENT_DROP_TEXT:
        case SDL_EVENT_DROP_BEGIN:
        case SDL_EVENT_DROP_COMPLETE:
        case SDL_EVENT_DROP_POSITION:
        /* ---- 渲染 ---- */
        case SDL_EVENT_RENDER_TARGETS_RESET:
        case SDL_EVENT_RENDER_DEVICE_RESET:
        case SDL_EVENT_RENDER_DEVICE_LOST:
            return true;

        default:
            /* 其余事件（QUIT、DISPLAY_*、JOYSTICK_*、GAMEPAD_*、CLIPBOARD_*、
               AUDIO_DEVICE_*、SENSOR_*、CAMERA_*、KEYMAP_CHANGED、用户自定义事件等）
               视为非窗口相关事件 */
            return false;
    }
}

/**
 * @brief 获取事件关联的窗口ID（仅对窗口相关事件有效，其余返回0）
 */
SDL_WindowID GetEventWindowID(const SDL_Event* event)
{
    switch (event->type)
    {
        /* ---- 窗口事件 ---- */
        case SDL_EVENT_WINDOW_SHOWN:
        case SDL_EVENT_WINDOW_HIDDEN:
        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_MOVED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED:
        case SDL_EVENT_WINDOW_MINIMIZED:
        case SDL_EVENT_WINDOW_MAXIMIZED:
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        case SDL_EVENT_WINDOW_HIT_TEST:
        case SDL_EVENT_WINDOW_ICCPROF_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
        case SDL_EVENT_WINDOW_OCCLUDED:
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        case SDL_EVENT_WINDOW_DESTROYED:
        case SDL_EVENT_WINDOW_HDR_STATE_CHANGED:
            return event->window.windowID;
        /* ---- 键盘 ---- */
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            return event->key.windowID;
        case SDL_EVENT_TEXT_EDITING:
            return event->edit.windowID;
        case SDL_EVENT_TEXT_INPUT:
            return event->text.windowID;
        case SDL_EVENT_TEXT_EDITING_CANDIDATES:
            return event->edit_candidates.windowID;
        /* ---- 鼠标 ---- */
        case SDL_EVENT_MOUSE_MOTION:
            return event->motion.windowID;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            return event->button.windowID;
        case SDL_EVENT_MOUSE_WHEEL:
            return event->wheel.windowID;
        /* ---- 触摸 ---- */
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_CANCELED:
            return event->tfinger.windowID;
        /* ---- 捏合手势 ---- */
        case SDL_EVENT_PINCH_BEGIN:
        case SDL_EVENT_PINCH_UPDATE:
        case SDL_EVENT_PINCH_END:
            return event->pinch.windowID;
        /* ---- 手写笔 ---- */
        case SDL_EVENT_PEN_PROXIMITY_IN:
        case SDL_EVENT_PEN_PROXIMITY_OUT:
            return event->pproximity.windowID;
        case SDL_EVENT_PEN_DOWN:
        case SDL_EVENT_PEN_UP:
            return event->ptouch.windowID;
        case SDL_EVENT_PEN_BUTTON_DOWN:
        case SDL_EVENT_PEN_BUTTON_UP:
            return event->pbutton.windowID;
        case SDL_EVENT_PEN_MOTION:
            return event->pmotion.windowID;
        case SDL_EVENT_PEN_AXIS:
            return event->paxis.windowID;
        /* ---- 拖放 ---- */
        case SDL_EVENT_DROP_FILE:
        case SDL_EVENT_DROP_TEXT:
        case SDL_EVENT_DROP_BEGIN:
        case SDL_EVENT_DROP_COMPLETE:
        case SDL_EVENT_DROP_POSITION:
            return event->drop.windowID;
        /* ---- 渲染 ---- */
        case SDL_EVENT_RENDER_TARGETS_RESET:
        case SDL_EVENT_RENDER_DEVICE_RESET:
        case SDL_EVENT_RENDER_DEVICE_LOST:
            return event->render.windowID;

        default:
            return 0;
    }
}

} // namespace

/* ======================= 成员函数实现 ======================= */

SDL_EventDispatcher::SDL_EventDispatcher() = default;
SDL_EventDispatcher::~SDL_EventDispatcher() = default;

void SDL_EventDispatcher::DispatchEvents(const SDL_Event* event)
{
    if (event == nullptr)
        return;

    // 只读遍历线程表，多个分发/查询可并发，共享锁即可
    std::shared_lock mapLock(m_ThreadDataMapMutex);

    if (IsWindowRelatedEvent(event))
    {
        // 窗口相关事件：按窗口ID分发给订阅了该窗口的线程
        const SDL_WindowID windowID = GetEventWindowID(event);
        for (auto& pair : m_ThreadDataMap)
        {
            ThreadData& td = pair.second;
            std::unique_lock threadLock(td.m_Mutex);
            if (td.m_SubscribedWindowIDs.count(static_cast<int>(windowID)) == 0)
                continue; // 该线程未订阅此窗口
            if (td.m_QueueEvents.size() >= EVENT_DISPATCHER_EVENT_QUEUE_MAX_SIZE)
                continue; // 队列已满，丢弃该事件（防止内存无限增长）
            td.m_QueueEvents.push(*event);
        }
    }
    else
    {
        // 非窗口相关事件：分发给设置了"需要非窗口事件"标志的线程
        for (auto& pair : m_ThreadDataMap)
        {
            ThreadData& td = pair.second;
            std::unique_lock threadLock(td.m_Mutex);
            if ((td.m_Flags & EVENT_DISPATCHER_FLAG_WANT_NON_WINDOW_RELATED_EVENTS) == 0)
                continue; // 该线程不关心非窗口事件
            if (td.m_QueueEvents.size() >= EVENT_DISPATCHER_EVENT_QUEUE_MAX_SIZE)
                continue; // 队列已满，丢弃该事件
            td.m_QueueEvents.push(*event);
        }
    }
}

void SDL_EventDispatcher::InitCurrentThread(const std::vector<unsigned int>& windowIDs, int flags)
{
    // operator[] 会在线程首次初始化时创建对应条目
    std::unique_lock mapLock(m_ThreadDataMapMutex);
    ThreadData& td = m_ThreadDataMap[std::this_thread::get_id()];
    std::unique_lock threadLock(td.m_Mutex);
    td.m_Flags = flags;
    td.m_SubscribedWindowIDs.clear();
    td.m_SubscribedWindowIDs.insert(windowIDs.begin(), windowIDs.end());
    // 注意：不主动清空 m_QueueEvents，避免重复初始化时丢失尚未拉取的事件
}

void SDL_EventDispatcher::SetCurrentThreadFlags(int flags)
{
    std::shared_lock mapLock(m_ThreadDataMapMutex);
    auto it = m_ThreadDataMap.find(std::this_thread::get_id());
    if (it == m_ThreadDataMap.end())
        throw std::runtime_error("SDL_EventDispatcher::SetCurrentThreadFlags: 当前线程尚未初始化，请先调用InitCurrentThread");
    ThreadData& td = it->second;
    std::unique_lock threadLock(td.m_Mutex);
    td.m_Flags = flags;
}

void SDL_EventDispatcher::SubscribeWindow(unsigned int windowID)
{
    std::shared_lock mapLock(m_ThreadDataMapMutex);
    auto it = m_ThreadDataMap.find(std::this_thread::get_id());
    if (it == m_ThreadDataMap.end())
        throw std::runtime_error("SDL_EventDispatcher::SubscribeWindow: 当前线程尚未初始化，请先调用InitCurrentThread");
    ThreadData& td = it->second;
    std::unique_lock threadLock(td.m_Mutex);
    td.m_SubscribedWindowIDs.insert(windowID);
}

void SDL_EventDispatcher::UnsubscribeWindow(unsigned int windowID)
{
    std::shared_lock mapLock(m_ThreadDataMapMutex);
    auto it = m_ThreadDataMap.find(std::this_thread::get_id());
    if (it == m_ThreadDataMap.end())
        throw std::runtime_error("SDL_EventDispatcher::UnsubscribeWindow: 当前线程尚未初始化，请先调用InitCurrentThread");
    ThreadData& td = it->second;
    std::unique_lock threadLock(td.m_Mutex);
    td.m_SubscribedWindowIDs.erase(windowID);
}

int SDL_EventDispatcher::GetCurrentThreadFlags()
{
    std::shared_lock mapLock(m_ThreadDataMapMutex);
    auto it = m_ThreadDataMap.find(std::this_thread::get_id());
    if (it == m_ThreadDataMap.end())
        throw std::runtime_error("SDL_EventDispatcher::GetCurrentThreadFlags: 当前线程尚未初始化，请先调用InitCurrentThread");
    ThreadData& td = it->second;
    std::shared_lock threadLock(td.m_Mutex);
    return td.m_Flags;
}

bool SDL_EventDispatcher::PollEvent(SDL_Event* event)
{
    if (event == nullptr)
        return false;

    std::shared_lock mapLock(m_ThreadDataMapMutex);
    auto it = m_ThreadDataMap.find(std::this_thread::get_id());
    if (it == m_ThreadDataMap.end())
        throw std::runtime_error("SDL_EventDispatcher::PollEvent: 当前线程尚未初始化，请先调用InitCurrentThread");
    ThreadData& td = it->second;
    std::unique_lock threadLock(td.m_Mutex);
    if (td.m_QueueEvents.empty())
        return false;
    *event = td.m_QueueEvents.front();
    td.m_QueueEvents.pop();
    return true;
}

#endif

#endif
