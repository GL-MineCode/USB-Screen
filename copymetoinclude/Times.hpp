#ifndef __INC_TIMES_
#define __INC_TIMES_

#include <SDL3/SDL.h>
#include <mutex>

class FPSLimiter {  
private:  
    Uint64 targetTicksPerFrame; 
    Uint64 lastFrameTicks;
    bool disabled;
    double current;
public:  
    FPSLimiter() {  
        targetTicksPerFrame = 1'000'000'000 / 30;  
        lastFrameTicks = SDL_GetTicksNS();
        disabled = false;
        current = 30.0;
    }  
    void Disable(){
        disabled = true;
    }
    void Enable(){
        disabled = false;
    }
    void Set(double maxFPS){
        targetTicksPerFrame = 1'000'000'000 / maxFPS;  
        lastFrameTicks = SDL_GetTicksNS();
        current = maxFPS;
    }
    double Get() const{
        return disabled ? (-1.0) : current;
    }
    FPSLimiter(double maxFPS,bool disabled = false) {  
        this->Set(maxFPS); 
        this->disabled = disabled;
    }  
    void Delay() {  
        if(disabled){
            return;
        }
        Uint64 elapsedTicks = SDL_GetTicksNS() - lastFrameTicks;   
        if (elapsedTicks < targetTicksPerFrame) {  
            SDL_DelayNS(static_cast<Uint32>(targetTicksPerFrame - elapsedTicks));
        }
        lastFrameTicks = SDL_GetTicksNS();
    }  
};

typedef FPSLimiter Timer;

// 滑动时间窗口 FPS 计数器（线程安全）
//  Update()  : 每渲染/发送一帧调用一次，仅入队一个时间戳，O(1) 无动态分配，不影响性能
//  GetFPS()  : 返回最近 1 秒窗口内的平均帧数；窗口未满时按实际经过时间折算
class FPSCounter {
private:
    static constexpr int    kCapacity = 512;                   // 环形缓冲容量（1s 内可容纳的帧数上限）
    static constexpr Uint64 kWindowNS = 1'000'000'000ULL;      // 1 秒窗口（纳秒）

    mutable std::mutex mtx_;                    // 保护以下共享状态（Update/GetFPS 跨线程）
    Uint64             stamps_[kCapacity] = {}; // 各帧时间戳（纳秒，单调递增）
    int                head_ = 0;               // 下一个写入槽
    int                count_ = 0;              // 有效条目数

public:
    // 每帧调用一次；仅写入一个时间戳，常数时间，不会影响性能
    Uint64 Update() {
        const Uint64 now = SDL_GetTicksNS();
        std::lock_guard<std::mutex> lk(mtx_);
        stamps_[head_] = now;
        head_ = (head_ + 1) % kCapacity;
        if (count_ < kCapacity) ++count_;
        return now;
    }

    // 返回最近 1 秒内的平均帧数；帧数不足 2 时返回 0
    double GetFPS() const {
        const Uint64 now = SDL_GetTicksNS();
        std::lock_guard<std::mutex> lk(mtx_);
        if (count_ < 2) return 0.0;

        int    frames   = 0;                       // 窗口内帧数
        Uint64 earliest = 0;                       // 窗口内最早帧的时间戳
        int    idx = (head_ - 1 + kCapacity) % kCapacity;   // 从最新一帧向前遍历
        for (int i = 0; i < count_; ++i) {
            const Uint64 t = stamps_[idx];
            if (now - t > kWindowNS) break;        // 超过 1s 窗口，更早的必然也过期
            earliest = t;
            ++frames;
            idx = (idx - 1 + kCapacity) % kCapacity;
        }
        if (frames < 2) return 0.0;

        const double elapsed = (now - earliest) * 1e-9;   // 窗口实际时长（秒）
        if (elapsed <= 0.0) return 0.0;
        return static_cast<double>(frames) / elapsed;
    }
};

#endif