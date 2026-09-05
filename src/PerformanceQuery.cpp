// ============================================================================
// PerformanceQuery.cpp - 性能监控库实现（Windows）
// ----------------------------------------------------------------------------
// 架构说明：
//  1. 每个子系统(CPU/GPU/内存/磁盘/网络)拥有独立的“后台采样线程”，
//     以固定间隔(默认 1000ms)读取系统计数并把结果发布到线程安全的快照中；
//  2. 所有 Get*() 只读取最近一次快照 -> 不阻塞调用线程、天然平滑(防抖动)；
//  3. 同一个实例可被任意多线程并发访问（内部用互斥保护快照）；
//  4. 数据源：
//       CPU     : NtQuerySystemInformation(每逻辑核) / GetSystemTimes 兜底
//       GPU     : DXGI(设备枚举) + PDH "GPU Engine/GPU Adapter Memory" + NVML(NVIDIA,动态加载)
//       MEM     : GlobalMemoryStatusEx
//       DISK    : PDH "PhysicalDisk"
//       NETWORK : PDH "Network Interface"
//     所有 PDH 计数器通过 PdhAddEnglishCounter* 添加，可在非英文系统上工作。
// ============================================================================

#include "PerformanceQuery.hpp"

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <dxgi.h>
#include <dxgi1_2.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

namespace pq_internal {

// ============================================================================
// 小工具
// ============================================================================

inline std::string WideToUtf8(const std::wstring& ws) {
    if (ws.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(),
                                      nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), &s[0], n,
                        nullptr, nullptr);
    return s;
}

/// 名称归一化（用于把不同数据源里的同一设备名对上号）
inline std::wstring NormalizeName(const std::wstring& ws) {
    std::wstring out;
    out.reserve(ws.size());
    bool seenText = false;
    for (wchar_t c : ws) {
        if (c == L' ' || c == L'\t') {
            if (seenText) out.push_back(L' ');
            continue;
        }
        seenText = true;
        if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
        out.push_back(c);
    }
    while (!out.empty() && out.back() == L' ') out.pop_back();
    return out;
}

/// UTF-8 版本名称归一化（设备名通常为 ASCII，按字节折叠即可）
inline std::string NormalizeUtf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool seenText = false;
    for (unsigned char c : s) {
        if (c == ' ' || c == '\t') {
            if (seenText) out.push_back(' ');
            continue;
        }
        seenText = true;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        out.push_back((char)c);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

/// 0~1 归一化（NaN 原样保留，越界值截断）
inline double Clamp01(double v) {
    if (v != v) return v;  // NaN
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

inline std::uint64_t ToU64(double v) {
    if (v != v || v < 0.0) return 0;
    const double r = std::floor(v + 0.5);
    if (r >= (double)(std::numeric_limits<std::uint64_t>::max)()) return 0;
    return (std::uint64_t)r;
}

// ============================================================================
// 线程安全的“最近快照”
// ============================================================================
template <typename T>
class Snapshot {
public:
    void Publish(T value) {
        auto p = std::make_shared<const T>(std::move(value));
        std::lock_guard<std::mutex> lk(mu_);
        latest_ = std::move(p);
    }
    /// 返回最近快照（若从未发布则返回默认构造的无效值）
    std::shared_ptr<const T> Peek() const {
        std::lock_guard<std::mutex> lk(mu_);
        if (latest_) return latest_;
        return std::make_shared<const T>();
    }

private:
    mutable std::mutex mu_;
    std::shared_ptr<const T> latest_;
};

// ============================================================================
// 周期性后台任务
// ============================================================================
class PeriodicSampler {
public:
    using Tick = std::function<void()>;

    PeriodicSampler(std::string tag, Tick tick, int intervalMs)
        : stop_(false), intervalMs_(intervalMs > 0 ? intervalMs : 1000) {
        (void)tag;
        th_ = std::thread([this, tick] { Run(tick); });
    }
    ~PeriodicSampler() { Stop(); }

    void Stop() {
        bool expected = false;
        if (stop_.compare_exchange_strong(expected, true)) {
            cv_.notify_all();
            if (th_.joinable()) th_.join();
        }
    }

private:
    void Run(const Tick& tick) {
        std::unique_lock<std::mutex> lk(mu_);
        for (;;) {
            if (cv_.wait_for(lk, std::chrono::milliseconds(intervalMs_),
                             [this] { return stop_.load(); })) {
                break;  // 收到停止通知
            }
            lk.unlock();
            try {
                tick();
            } catch (...) {
                // 采样线程异常不能外泄，忽略本次
            }
            lk.lock();
        }
    }

    std::atomic<bool> stop_;
    int intervalMs_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::thread th_;
};

// ============================================================================
// CPU 读取：NtQuerySystemInformation(SystemProcessorPerformanceInformation)
// ============================================================================
using NtQuerySystemInformationFn = LONG(WINAPI*)(UINT, PVOID, ULONG, PULONG);

struct ProcessorTimes {
    bool valid = false;
    bool perCore = false;  // false 时只有整体值（GetSystemTimes 兜底）
    std::vector<unsigned long long> idle;
    std::vector<unsigned long long> kernel;
    std::vector<unsigned long long> user;
    // 整体累计值
    unsigned long long sIdle = 0, sKernel = 0, sUser = 0;
};

inline unsigned long long FileTimeToU64(const FILETIME& ft) {
    return ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

inline ProcessorTimes ReadProcessorTimes() {
    ProcessorTimes r;

    static NtQuerySystemInformationFn s_ntQuery = []() -> NtQuerySystemInformationFn {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (!ntdll) return nullptr;
        return reinterpret_cast<NtQuerySystemInformationFn>(
            reinterpret_cast<void (*)()>(
                GetProcAddress(ntdll, "NtQuerySystemInformation")));
    }();

    // ---- 尝试按逻辑核读取（>64 核时该接口会失败，属预期，走兜底） ----
    struct NtPerfInfo {
        LARGE_INTEGER IdleTime, KernelTime, UserTime, DpcTime, InterruptTime;
        ULONG InterruptCount;
    };
    const UINT SystemProcessorPerformanceInformation = 8;

    DWORD active = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (active == 0) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        active = si.dwNumberOfProcessors;
    }

    if (s_ntQuery && active > 0) {
        std::vector<NtPerfInfo> buf((std::size_t)active);
        ULONG size = (ULONG)(buf.size() * sizeof(NtPerfInfo));
        ULONG returned = 0;
        LONG st = s_ntQuery(SystemProcessorPerformanceInformation, buf.data(),
                            size, &returned);
        if (st >= 0 && returned >= sizeof(NtPerfInfo)) {
            r.valid = true;
            r.perCore = true;
            r.idle.reserve(buf.size());
            r.kernel.reserve(buf.size());
            r.user.reserve(buf.size());
            for (const auto& p : buf) {
                const unsigned long long idle = (unsigned long long)p.IdleTime.QuadPart;
                const unsigned long long kernel = (unsigned long long)p.KernelTime.QuadPart;
                const unsigned long long user = (unsigned long long)p.UserTime.QuadPart;
                r.idle.push_back(idle);
                r.kernel.push_back(kernel);
                r.user.push_back(user);
                r.sIdle += idle;
                r.sKernel += kernel;
                r.sUser += user;
            }
            return r;
        }
    }

    // ---- 兜底：GetSystemTimes 只有整体值 ----
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        r.valid = true;
        r.perCore = false;
        r.sIdle = FileTimeToU64(idle);
        r.sKernel = FileTimeToU64(kernel);
        r.sUser = FileTimeToU64(user);
    }
    return r;
}

// kernel 时间包含 idle 时间：繁忙 = kernel + user - idle；总量 = kernel + user
inline double BusyFraction(unsigned long long kernel, unsigned long long user,
                           unsigned long long idle) {
    const unsigned long long total = kernel + user;
    if (total == 0) return 0.0;
    const long long busy = (long long)(kernel - idle) + (long long)user;
    if (busy <= 0) return 0.0;
    double f = (double)busy / (double)total;
    if (f > 1.0) f = 1.0;
    return f;
}

// ============================================================================
// PDH 轻量封装（通配符计数器 + 英文计数器名，兼容非英文系统）
// ============================================================================
class PdhQuery {
public:
    PdhQuery() = default;
    ~PdhQuery() {
        for (auto& c : counters_) {
            if (c.handle) PdhRemoveCounter(c.handle);
        }
        if (query_) PdhCloseQuery(query_);
    }
    PdhQuery(const PdhQuery&) = delete;
    PdhQuery& operator=(const PdhQuery&) = delete;

    /// 添加计数器，返回其序号；失败返回 -1
    int Add(const wchar_t* path) {
        if (!query_) {
            if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS) {
                query_ = nullptr;
                return -1;
            }
        }
        PDH_HCOUNTER hc = nullptr;
        PDH_STATUS st = PdhAddEnglishCounterW(query_, path, 0, &hc);
        if (st != ERROR_SUCCESS) st = PdhAddCounterW(query_, path, 0, &hc);
        if (st != ERROR_SUCCESS || !hc) return -1;
        counters_.push_back({path, hc});
        return (int)counters_.size() - 1;
    }

    bool Collect() {
        if (!query_) return false;
        return PdhCollectQueryData(query_) == ERROR_SUCCESS;
    }

    struct Item {
        std::wstring name;
        double value = 0.0;
        bool ok = false;  // CStatus==ERROR_SUCCESS
    };

    /// 读取序号 idx 的计数器（通配符展开后每个实例一个 Item）
    /// fmtExtra 可附加 PDH_FMT_* 修饰标志（如 PDH_FMT_NOCAP100：不把睿频后的 >100% 截断到 100%）。
    std::vector<Item> Read(int idx, DWORD fmtExtra = 0) const {
        std::vector<Item> out;
        if (!query_ || idx < 0 || idx >= (int)counters_.size()) return out;
        PDH_HCOUNTER hc = counters_[(std::size_t)idx].handle;
        const DWORD fmt = PDH_FMT_DOUBLE | fmtExtra;
        DWORD bufSize = 0, itemCount = 0;
        PDH_STATUS st = PdhGetFormattedCounterArrayW(hc, fmt, &bufSize,
                                                     &itemCount, nullptr);
        if (st != (PDH_STATUS)PDH_MORE_DATA || itemCount == 0) return out;
        std::vector<unsigned char> storage(bufSize);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM*>(storage.data());
        st = PdhGetFormattedCounterArrayW(hc, fmt, &bufSize, &itemCount, items);
        if (st != ERROR_SUCCESS) return out;
        out.reserve(itemCount);
        for (DWORD i = 0; i < itemCount; ++i) {
            if (!items[i].szName) continue;
            Item it;
            it.ok = (items[i].FmtValue.CStatus == ERROR_SUCCESS);
            it.name = items[i].szName;
            if (it.ok) it.value = items[i].FmtValue.doubleValue;
            out.push_back(std::move(it));
        }
        return out;
    }

private:
    struct CounterEntry {
        const wchar_t* path;
        PDH_HCOUNTER handle;
    };
    PDH_HQUERY query_ = nullptr;
    std::vector<CounterEntry> counters_;
};

// 磁盘实例按前导数字排序（0 C:, 1 D:, 10 ...）
struct DiskNameLess {
    bool operator()(const std::wstring& a, const std::wstring& b) const {
        auto leadNum = [](const std::wstring& s) -> std::pair<bool, unsigned long long> {
            unsigned long long n = 0;
            bool any = false;
            for (wchar_t c : s) {
                if (c >= L'0' && c <= L'9') {
                    n = n * 10 + (unsigned long long)(c - L'0');
                    any = true;
                } else {
                    break;
                }
            }
            return {any, n};
        };
        const auto na = leadNum(a);
        const auto nb = leadNum(b);
        if (na.first && nb.first && na.second != nb.second)
            return na.second < nb.second;
        return a < b;
    }
};

// Processor Information 的实例名形如 "G,I"（组号,组内处理器号），例如 "0,3"；
// "_Total" / "0,_Total" 为合计项（无法解析），返回 false 表示不可用。
static bool ParseGroupInstance(const std::wstring& name, int& group, int& proc) {
    const std::size_t comma = name.find(L',');
    if (comma == std::wstring::npos) return false;
    const wchar_t* gb = name.c_str();
    wchar_t* ge = nullptr;
    const long g = std::wcstol(gb, &ge, 10);
    if (ge == gb || !ge || ge != name.c_str() + comma || g < 0) return false;
    const wchar_t* pb = name.c_str() + comma + 1;
    wchar_t* pe = nullptr;
    const long p = std::wcstol(pb, &pe, 10);
    if (pe == pb || !pe || *pe != L'\0' || p < 0) return false;
    group = (int)g;
    proc = (int)p;
    return true;
}

// ============================================================================
// CPU 采集器
// ============================================================================
class CpuCollector {
public:
    CpuCollector(Snapshot<PerformanceQuery::CpuUsage>& snap, int intervalMs)
        : snap_(&snap), intervalMs_(intervalMs) {}

    bool Start() {
        DWORD active = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        if (active == 0) {
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            active = si.dwNumberOfProcessors;
        }
        coreCount_ = active;
        speedGHz_ = ReadCpuSpeedGHz();
        // 记录每组处理器在“全局逻辑核编号”中的起始偏移（用于把 PDH "G,I" 映射到核序号）
        groupBase_.clear();
        {
            const USHORT ng = GetActiveProcessorGroupCount();
            int acc = 0;
            for (USHORT gi = 0; gi < ng; ++gi) {
                groupBase_.push_back(acc);
                acc += (int)GetActiveProcessorCount((WORD)gi);
            }
        }

        // 总体 + 逐核使用率主源：任务管理器同款的 "% Processor Utility" 计数器。
        // 该值已计入实际运行频率（睿频时 >100%），故格式化时附加 PDH_FMT_NOCAP100
        // 以免被截断到 100%。添加成功后立即采集一次建立 PDH 基线（二次采集起才有值）。
        idxUtil_ = pdh_.Add(L"\\Processor Information(_Total)\\% Processor Utility");
        idxCore_ = pdh_.Add(L"\\Processor Information(*)\\% Processor Utility");
        if (idxUtil_ >= 0 || idxCore_ >= 0) pdh_.Collect();

        prev_ = ReadProcessorTimes();
        if (prev_.valid)
            PublishFrom(prev_, std::numeric_limits<double>::quiet_NaN(), {});

        worker_ = std::make_unique<PeriodicSampler>(
            "cpu", [this] { Tick(); }, intervalMs_);
        return true;
    }

    void Tick() {
        // PDH "% Processor Utility"：任务管理器同款。先采本窗口数据，再读格式化值。
        double util01 = std::numeric_limits<double>::quiet_NaN();
        std::map<int, double> core01;  // 键=逻辑核序号 -> 0~…(睿频可>1)
        if ((idxUtil_ >= 0 || idxCore_ >= 0) && pdh_.Collect()) {
            // 总体：路径已精确指定 _Total 实例，数组最多一个元素，取首个有效项
            if (idxUtil_ >= 0) {
                for (const auto& it : pdh_.Read(idxUtil_, PDH_FMT_NOCAP100)) {
                    if (it.ok) {
                        util01 = it.value / 100.0;  // PDH 返回百分数，睿频可>100
                        break;
                    }
                }
            }
            // 逐核：通配实例名形如 "G,I"（组号,组内核号，如 "0,3"、"1,0"），
            // 还有合计项 "_Total" / "0,_Total"（跳过）
            if (idxCore_ >= 0) {
                for (const auto& it : pdh_.Read(idxCore_, PDH_FMT_NOCAP100)) {
                    if (!it.ok) continue;
                    int g = 0, p = 0;
                    if (!ParseGroupInstance(it.name, g, p)) continue;
                    if (g >= 0 && g < (int)groupBase_.size()) {
                        const int global = groupBase_[(std::size_t)g] + p;
                        if (global >= 0 && global < (int)coreCount_)
                            core01[global] = it.value / 100.0;
                    }
                }
            }
        }
        // 时钟差值仅作 PDH 不可用时的逐核/总体兜底
        ProcessorTimes cur = ReadProcessorTimes();
        if (!cur.valid) return;
        if (prev_.valid) PublishFrom(cur, util01, core01);
        prev_ = cur;
    }

private:
    static double ReadCpuSpeedGHz() {
        DWORD mhz = 0;
        DWORD size = sizeof(mhz);
        LONG st = RegGetValueW(
            HKEY_LOCAL_MACHINE,
            L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"~MHz",
            RRF_RT_REG_DWORD, nullptr, &mhz, &size);
        if (st != ERROR_SUCCESS || mhz == 0)
            return std::numeric_limits<double>::quiet_NaN();
        return (double)mhz / 1000.0;
    }

    /// 发布一次 CPU 快照。
    /// @param pdhUtil01 PDH "% Processor Utility"(_Total) 换算值(0~…，睿频可>1)；NaN=不可用。
    /// @param pdhCore   PDH "% Processor Utility" 逐逻辑核换算值（键=核序号，值可>1）；
    ///                  0..N-1 齐全才整体采用。总体与逐核均优先 PDH（任务管理器同口径），
    ///                  PDH 完全不可用时才退回时钟差值计算。
    void PublishFrom(const ProcessorTimes& cur, double pdhUtil01,
                     const std::map<int, double>& pdhCore) {
        PerformanceQuery::CpuUsage u;
        u.CoreCount = coreCount_;
        u.SpeedGHz = speedGHz_;
        u.Temperature = std::numeric_limits<double>::quiet_NaN();  // 通用 API 读不到 CPU 温度

        const std::size_t n = (std::size_t)coreCount_;
        u.CoreUsage.assign(n, std::numeric_limits<double>::quiet_NaN());

        // ---- 时钟差值兜底（PDH 不可用时才作为逐核/总体来源）----
        std::vector<double> coreTick(n, std::numeric_limits<double>::quiet_NaN());
        double overallTick = std::numeric_limits<double>::quiet_NaN();
        if (prev_.valid && prev_.perCore && cur.perCore &&
            prev_.idle.size() == n && cur.idle.size() == n) {
            double sumBusy = 0.0, sumTotal = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                const long long dIdle = (long long)(cur.idle[i] - prev_.idle[i]);
                const long long dKernel = (long long)(cur.kernel[i] - prev_.kernel[i]);
                const long long dUser = (long long)(cur.user[i] - prev_.user[i]);
                const unsigned long long k = (unsigned long long)std::max(0LL, dKernel);
                const unsigned long long us = (unsigned long long)std::max(0LL, dUser);
                const unsigned long long id = (unsigned long long)std::max(0LL, dIdle);
                const unsigned long long total = k + us;
                if (total > 0) {
                    coreTick[i] = Clamp01(BusyFraction(k, us, id));
                    sumBusy += coreTick[i] * (double)total;
                    sumTotal += (double)total;
                } else {
                    coreTick[i] = 0.0;
                }
            }
            if (sumTotal > 0.0) overallTick = Clamp01(sumBusy / sumTotal);
        } else if (prev_.valid && !prev_.perCore && !cur.perCore) {
            // 整体兜底：GetSystemTimes 差值
            const unsigned long long dKernel =
                cur.sKernel > prev_.sKernel ? cur.sKernel - prev_.sKernel : 0;
            const unsigned long long dUser =
                cur.sUser > prev_.sUser ? cur.sUser - prev_.sUser : 0;
            const unsigned long long dIdle =
                cur.sIdle > prev_.sIdle ? cur.sIdle - prev_.sIdle : 0;
            const unsigned long long total = dKernel + dUser;
            if (total > 0)
                overallTick = Clamp01(BusyFraction(dKernel, dUser, dIdle));
        } else if (cur.perCore) {
            // 只有一次样本：自开机以来的累计平均（立即可用）
            double sumBusy = 0.0, sumTotal = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                const double f = BusyFraction(cur.kernel[i], cur.user[i], cur.idle[i]);
                coreTick[i] = Clamp01(f);
                sumBusy += f * (double)(cur.kernel[i] + cur.user[i]);
                sumTotal += (double)(cur.kernel[i] + cur.user[i]);
            }
            if (sumTotal > 0.0) overallTick = Clamp01(sumBusy / sumTotal);
        } else if (cur.sKernel + cur.sUser > 0) {
            overallTick = Clamp01(BusyFraction(cur.sKernel, cur.sUser, cur.sIdle));
        }

        // ---- 逐核：PDH per-core 齐全则整体采用，否则用时钟差值 ----
        const bool coreFromPdh = pdhCore.size() >= n;
        if (coreFromPdh) {
            for (std::size_t i = 0; i < n; ++i) {
                const auto it = pdhCore.find((int)i);
                if (it != pdhCore.end()) u.CoreUsage[i] = Clamp01(it->second);
            }
        } else {
            u.CoreUsage = std::move(coreTick);
        }

        // ---- 总体：PDH _Total > per-core 平均 > 时钟差值兜底 ----
        if (pdhUtil01 == pdhUtil01) {
            u.Usage = pdhUtil01;  // 与任务管理器一致；睿频可 >100%，不截断
        } else if (coreFromPdh) {
            double s = 0.0;
            int c = 0;
            for (double v : u.CoreUsage)
                if (v == v) { s += v; ++c; }
            u.Usage = c > 0 ? s / (double)c
                            : std::numeric_limits<double>::quiet_NaN();
        } else {
            u.Usage = overallTick;
        }
        snap_->Publish(std::move(u));
    }

    Snapshot<PerformanceQuery::CpuUsage>* snap_;
    int intervalMs_;
    std::uint32_t coreCount_ = 0;
    double speedGHz_ = std::numeric_limits<double>::quiet_NaN();
    std::vector<int> groupBase_;  // 每组在全局逻辑核编号中的起始偏移
    ProcessorTimes prev_;
    PdhQuery pdh_;            // 任务管理器同款 "% Processor Utility" 计数器（PDH）
    int idxUtil_ = -1;        // (_Total) 总体计数器序号；<0 = 不可用
    int idxCore_ = -1;        // (*) 逐逻辑核计数器序号；<0 = 不可用（退回时钟差值）
    std::unique_ptr<PeriodicSampler> worker_;
};

// ============================================================================
// 内存采集器
// ============================================================================
class MemCollector {
public:
    MemCollector(Snapshot<PerformanceQuery::MemUsage>& snap, int intervalMs)
        : snap_(&snap), intervalMs_(intervalMs) {}

    bool Start() {
        Tick();  // 立即出一份数据
        worker_ = std::make_unique<PeriodicSampler>(
            "mem", [this] { Tick(); }, intervalMs_);
        return true;
    }

    void Tick() {
        MEMORYSTATUSEX ms;
        ms.dwLength = sizeof(ms);
        if (!GlobalMemoryStatusEx(&ms)) return;
        PerformanceQuery::MemUsage u;
        u.byteTotal = ms.ullTotalPhys;
        u.byteUsed = ms.ullTotalPhys - ms.ullAvailPhys;
        u.Usage = u.byteTotal > 0
                      ? Clamp01((double)u.byteUsed / (double)u.byteTotal)
                      : std::numeric_limits<double>::quiet_NaN();
        snap_->Publish(std::move(u));
    }

private:
    Snapshot<PerformanceQuery::MemUsage>* snap_;
    int intervalMs_;
    std::unique_ptr<PeriodicSampler> worker_;
};

// ============================================================================
// 磁盘 IO 采集器（PDH PhysicalDisk）
// ============================================================================
class DiskCollector {
public:
    DiskCollector(Snapshot<std::vector<PerformanceQuery::DiskUsage>>& snap,
                  int intervalMs)
        : snap_(&snap), intervalMs_(intervalMs) {}

    bool Start() {
        idxRead_ = pdh_.Add(L"\\PhysicalDisk(*)\\Disk Read Bytes/sec");
        idxWrite_ = pdh_.Add(L"\\PhysicalDisk(*)\\Disk Write Bytes/sec");
        idxReadTime_ = pdh_.Add(L"\\PhysicalDisk(*)\\% Disk Read Time");
        idxWriteTime_ = pdh_.Add(L"\\PhysicalDisk(*)\\% Disk Write Time");
        idxDiskTime_ = pdh_.Add(L"\\PhysicalDisk(*)\\% Disk Time");
        idxIdleTime_ = pdh_.Add(L"\\PhysicalDisk(*)\\% Idle Time");
        // 用于解析本地化的 "_Total" 实例名
        idxTotalName_ = pdh_.Add(L"\\PhysicalDisk(_Total)\\% Disk Time");

        if (idxRead_ < 0 && idxWrite_ < 0 && idxDiskTime_ < 0) return false;

        pdh_.Collect();  // 第一次采集建立基线
        ResolveTotalName();
        worker_ = std::make_unique<PeriodicSampler>(
            "disk", [this] { Tick(); }, intervalMs_);
        return true;
    }

    void Tick() {
        if (!pdh_.Collect()) return;
        if (totalName_.empty()) ResolveTotalName();  // 数据就绪后再解析 "_Total" 名
        auto readB = Read_(idxRead_);
        auto writeB = Read_(idxWrite_);
        auto readT = Read_(idxReadTime_);
        auto writeT = Read_(idxWriteTime_);
        auto diskT = Read_(idxDiskTime_);
        auto idleT = Read_(idxIdleTime_);

        std::map<std::wstring, PerformanceQuery::DiskUsage, DiskNameLess> merged;
        auto touch = [&merged](const std::vector<PdhQuery::Item>& items) {
            for (const auto& it : items)
                if (!it.name.empty()) merged[it.name];
        };
        touch(readB);
        touch(writeB);
        touch(readT);
        touch(writeT);
        touch(diskT);
        touch(idleT);

        std::vector<PerformanceQuery::DiskUsage> out;
        out.reserve(merged.size());
        for (auto& kv : merged) {
            if (!totalName_.empty() && kv.first == totalName_) continue;
            PerformanceQuery::DiskUsage d;
            d.DeviceName = WideToUtf8(kv.first);

            auto take = [](const std::vector<PdhQuery::Item>& items,
                           const std::wstring& name) -> std::pair<bool, double> {
                for (const auto& it : items)
                    if (it.ok && it.name == name) return {true, it.value};
                return {false, 0.0};
            };
            const auto rb = take(readB, kv.first);
            const auto wb = take(writeB, kv.first);
            const auto rt = take(readT, kv.first);
            const auto wt = take(writeT, kv.first);
            const auto dt = take(diskT, kv.first);
            const auto it_ = take(idleT, kv.first);

            d.byteReadPerSec =
                rb.first ? ToU64(rb.second) : PerformanceQuery::kInvalidU64;
            d.byteWritePerSec =
                wb.first ? ToU64(wb.second) : PerformanceQuery::kInvalidU64;
            if (rt.first) d.ReadUsage = Clamp01(rt.second / 100.0);
            if (wt.first) d.WriteUsage = Clamp01(wt.second / 100.0);
            if (it_.first) {  // 优先 % Idle Time（现代驱动器更准确）
                d.TotalUsage = Clamp01(1.0 - it_.second / 100.0);
            } else if (dt.first) {
                d.TotalUsage = Clamp01(dt.second / 100.0);
            }
            out.push_back(std::move(d));
        }
        snap_->Publish(std::move(out));
    }

private:
    std::vector<PdhQuery::Item> Read_(int idx) const {
        if (idx < 0) return {};
        return pdh_.Read(idx);
    }

    void ResolveTotalName() {
        if (idxTotalName_ < 0) return;
        for (auto& it : pdh_.Read(idxTotalName_)) {
            if (it.ok) {
                totalName_ = it.name;
                return;
            }
        }
    }

    Snapshot<std::vector<PerformanceQuery::DiskUsage>>* snap_;
    int intervalMs_;
    PdhQuery pdh_;
    int idxRead_ = -1, idxWrite_ = -1, idxReadTime_ = -1, idxWriteTime_ = -1;
    int idxDiskTime_ = -1, idxIdleTime_ = -1, idxTotalName_ = -1;
    std::wstring totalName_;
    std::unique_ptr<PeriodicSampler> worker_;
};

// ============================================================================
// 网络 IO 采集器（PDH Network Interface）
// ============================================================================
class NetCollector {
public:
    NetCollector(Snapshot<std::vector<PerformanceQuery::NetworkUsage>>& snap,
                 int intervalMs)
        : snap_(&snap), intervalMs_(intervalMs) {}

    bool Start() {
        idxSent_ = pdh_.Add(L"\\Network Interface(*)\\Bytes Sent/sec");
        idxRecv_ = pdh_.Add(L"\\Network Interface(*)\\Bytes Received/sec");
        idxBw_ = pdh_.Add(L"\\Network Interface(*)\\Current Bandwidth");
        if (idxSent_ < 0 && idxRecv_ < 0 && idxBw_ < 0) return false;
        pdh_.Collect();  // 基线
        worker_ = std::make_unique<PeriodicSampler>(
            "net", [this] { Tick(); }, intervalMs_);
        return true;
    }

    void Tick() {
        if (!pdh_.Collect()) return;
        const auto sent = Read_(idxSent_);
        const auto recv = Read_(idxRecv_);
        const auto bw = Read_(idxBw_);

        std::map<std::wstring, PerformanceQuery::NetworkUsage> merged;
        auto touch = [&merged](const std::vector<PdhQuery::Item>& items) {
            for (const auto& it : items)
                if (!it.name.empty()) merged[it.name];
        };
        touch(sent);
        touch(recv);
        touch(bw);

        std::vector<PerformanceQuery::NetworkUsage> out;
        out.reserve(merged.size());
        for (auto& kv : merged) {
            PerformanceQuery::NetworkUsage n;
            n.DeviceName = WideToUtf8(kv.first);
            auto take = [](const std::vector<PdhQuery::Item>& items,
                           const std::wstring& name) -> std::pair<bool, double> {
                for (const auto& it : items)
                    if (it.ok && it.name == name) return {true, it.value};
                return {false, 0.0};
            };
            const auto s = take(sent, kv.first);
            const auto r = take(recv, kv.first);
            const auto b = take(bw, kv.first);

            n.byteSentPerSec = s.first ? ToU64(s.second)
                                       : PerformanceQuery::kInvalidU64;
            n.byteReceivedPerSec = r.first ? ToU64(r.second)
                                           : PerformanceQuery::kInvalidU64;
            if (b.first && b.second > 0.0 && (s.first || r.first)) {
                const double bitsPerSec =
                    ((s.first ? s.second : 0.0) + (r.first ? r.second : 0.0)) * 8.0;
                n.Usage = Clamp01(bitsPerSec / b.second);
            }
            out.push_back(std::move(n));
        }
        snap_->Publish(std::move(out));
    }

private:
    std::vector<PdhQuery::Item> Read_(int idx) const {
        if (idx < 0) return {};
        return pdh_.Read(idx);
    }

    Snapshot<std::vector<PerformanceQuery::NetworkUsage>>* snap_;
    int intervalMs_;
    PdhQuery pdh_;
    int idxSent_ = -1, idxRecv_ = -1, idxBw_ = -1;
    std::unique_ptr<PeriodicSampler> worker_;
};

// ============================================================================
// GPU 采集器：DXGI(设备枚举) + PDH GPU Engine/GPU Adapter Memory + NVML(NVIDIA)
// ============================================================================

/// NVML 动态加载（不硬依赖 NVIDIA 驱动，未安装时整段跳过）
class NvmlBackend {
public:
    NvmlBackend() = default;
    ~NvmlBackend() {
        if (lib_ && fShutdown_ && fInitOk_) fShutdown_();
        if (lib_) FreeLibrary(lib_);
    }
    NvmlBackend(const NvmlBackend&) = delete;
    NvmlBackend& operator=(const NvmlBackend&) = delete;

    bool Init() {
        lib_ = LoadLibraryW(L"nvml.dll");
        if (!lib_) return false;
        auto fn = [this](const char* name) {
            return reinterpret_cast<void*>(GetProcAddress(lib_, name));
        };
        fInit_ = (int (*)(void))fn("nvmlInit_v2");
        if (!fInit_) fInit_ = (int (*)(void))fn("nvmlInit");
        fShutdown_ = (int (*)(void))fn("nvmlShutdown");
        fCount_ = (int (*)(unsigned int*))fn("nvmlDeviceGetCount_v2");
        if (!fCount_) fCount_ = (int (*)(unsigned int*))fn("nvmlDeviceGetCount");
        fHandle_ = (int (*)(unsigned int, void**))fn("nvmlDeviceGetHandleByIndex_v2");
        if (!fHandle_)
            fHandle_ = (int (*)(unsigned int, void**))fn("nvmlDeviceGetHandleByIndex");
        fName_ = (int (*)(void*, char*, unsigned int))fn("nvmlDeviceGetName");
        fUtil_ = (int (*)(void*, NvmlUtil*))fn("nvmlDeviceGetUtilizationRates");
        fMem_ = (int (*)(void*, NvmlMem*))fn("nvmlDeviceGetMemoryInfo");
        fTemp_ = (int (*)(void*, unsigned int, unsigned int*))fn("nvmlDeviceGetTemperature");
        if (!fInit_ || !fShutdown_ || !fCount_ || !fHandle_) return false;
        if (fInit_() != 0) return false;
        fInitOk_ = true;
        return true;
    }

    struct Gpu {
        std::wstring name;
        double usage = std::numeric_limits<double>::quiet_NaN();  // 0~1
        bool usageValid = false;
        std::uint64_t memUsed = 0, memTotal = 0;
        bool memValid = false;
        double temp = std::numeric_limits<double>::quiet_NaN();
        bool tempValid = false;
    };

    bool QueryAll(std::vector<Gpu>& out) const {
        if (!fInitOk_) return false;
        unsigned int count = 0;
        if (fCount_(&count) != 0 || count == 0) return false;
        const unsigned int NVML_TEMPERATURE_GPU = 0;
        for (unsigned int i = 0; i < count; ++i) {
            void* dev = nullptr;
            if (fHandle_(i, &dev) != 0 || !dev) continue;
            Gpu g;
            if (fName_) {
                char buf[256] = {0};
                if (fName_(dev, buf, (unsigned int)sizeof(buf)) == 0) {
                    const int n = MultiByteToWideChar(CP_UTF8, 0, buf, -1, nullptr, 0);
                    if (n > 1) {
                        std::wstring ws((std::size_t)n - 1, L'\0');
                        MultiByteToWideChar(CP_UTF8, 0, buf, -1, &ws[0], n);
                        g.name = std::move(ws);
                    }
                }
            }
            if (fUtil_) {
                NvmlUtil u{};
                if (fUtil_(dev, &u) == 0) {
                    g.usage = Clamp01((double)u.gpu / 100.0);
                    g.usageValid = true;
                }
            }
            if (fMem_) {
                NvmlMem m{};
                if (fMem_(dev, &m) == 0 && m.total > 0) {
                    g.memUsed = (std::uint64_t)m.used;
                    g.memTotal = (std::uint64_t)m.total;
                    g.memValid = true;
                }
            }
            if (fTemp_) {
                unsigned int t = 0;
                if (fTemp_(dev, NVML_TEMPERATURE_GPU, &t) == 0) {
                    g.temp = (double)t;
                    g.tempValid = true;
                }
            }
            out.push_back(std::move(g));
        }
        return !out.empty();
    }

private:
    struct NvmlUtil {
        unsigned int gpu;
        unsigned int memory;
    };
    struct NvmlMem {
        unsigned long long total;
        unsigned long long free;
        unsigned long long used;
    };

    HMODULE lib_ = nullptr;
    bool fInitOk_ = false;
    int (*fInit_)(void) = nullptr;
    int (*fShutdown_)(void) = nullptr;
    int (*fCount_)(unsigned int*) = nullptr;
    int (*fHandle_)(unsigned int, void**) = nullptr;
    int (*fName_)(void*, char*, unsigned int) = nullptr;
    int (*fUtil_)(void*, NvmlUtil*) = nullptr;
    int (*fMem_)(void*, NvmlMem*) = nullptr;
    int (*fTemp_)(void*, unsigned int, unsigned int*) = nullptr;
};

class GpuCollector {
public:
    GpuCollector(Snapshot<std::vector<PerformanceQuery::GpuUsage>>& snap,
                 int intervalMs)
        : snap_(&snap), intervalMs_(intervalMs) {}

    bool Start() {
        nvmlOk_ = nvml_.Init();
        if (nvmlOk_) nvml_.QueryAll(nvmlCache_);
        EnumerateDxgi();

        engineIdx_ = pdh_.Add(L"\\GPU Engine(*)\\Utilization Percentage");
        memUsedIdx_ = pdh_.Add(L"\\GPU Adapter Memory(*)\\Dedicated Usage");
        memLimitIdx_ = pdh_.Add(L"\\GPU Adapter Memory(*)\\Dedicated Limit");

        if (!nvmlOk_ && engineIdx_ < 0 && memUsedIdx_ < 0 &&
            dxgiAdapters_.empty()) {
            return false;
        }
        if (engineIdx_ >= 0 || memUsedIdx_ >= 0) pdh_.Collect();  // 基线
        worker_ = std::make_unique<PeriodicSampler>(
            "gpu", [this] { Tick(); }, intervalMs_);
        Tick();  // 立即发布一次，首轮查询即可看到设备列表
        return true;
    }

    void Tick() {
        // 1) NVML（NVIDIA）
        std::vector<NvmlBackend::Gpu> nvml;
        if (nvmlOk_) nvml_.QueryAll(nvml);

        // 2) PDH
        const bool pdhReady = (engineIdx_ >= 0 || memUsedIdx_ >= 0) && pdh_.Collect();
        std::map<LuidKey, double> enginePct;  // luid -> 适配器引擎使用率(0~100)
        if (pdhReady && engineIdx_ >= 0) CollectEnginePct(enginePct);
        std::map<std::wstring, std::pair<std::uint64_t, std::uint64_t>> vram;
        if (pdhReady) CollectVram(vram);

        std::vector<PerformanceQuery::GpuUsage> out;
        std::vector<bool> nvmlMatched(nvml.size(), false);

        // ---- 按 DXGI 顺序输出 ----
        for (const auto& ad : dxgiAdapters_) {
            PerformanceQuery::GpuUsage g;
            g.DeviceName = WideToUtf8(ad.name);
            const std::wstring keyName = NormalizeName(ad.name);

            // NVIDIA：优先合并 NVML（占用/显存/温度更权威且完整）
            const int mi = ad.isNvidia ? FindNvml(nvml, keyName, nvmlMatched) : -1;
            if (mi >= 0) {
                if (nvml[(std::size_t)mi].usageValid)
                    g.Usage = nvml[(std::size_t)mi].usage;
                if (nvml[(std::size_t)mi].memValid) {
                    g.VRAM_byteUsed = nvml[(std::size_t)mi].memUsed;
                    g.VRAM_byteTotal = nvml[(std::size_t)mi].memTotal;
                }
                if (nvml[(std::size_t)mi].tempValid)
                    g.Temperature = nvml[(std::size_t)mi].temp;
            } else {
                // 其它 GPU / NVML 不可用：用 PDH GPU Engine 汇总各引擎占用(LUID)
                for (const auto& kv : enginePct) {
                    if (LuidMatches(ad.luidToken, kv.first)) {
                        const double v = Clamp01(kv.second / 100.0);
                        if (g.Usage != g.Usage || v < g.Usage)
                            g.Usage = v;
                    }
                }
                // 显存：PDH GPU Adapter Memory 优先；否则用 DXGI 专用显存总量
                const auto itV = vram.find(keyName);
                if (itV != vram.end()) {
                    g.VRAM_byteUsed = itV->second.first;
                    g.VRAM_byteTotal = itV->second.second;
                } else if (ad.dedicatedVRAM > 0) {
                    g.VRAM_byteTotal = ad.dedicatedVRAM;
                }
            }
            if (g.VRAM_byteTotal != PerformanceQuery::kInvalidU64 &&
                g.VRAM_byteTotal > 0 &&
                g.VRAM_byteUsed != PerformanceQuery::kInvalidU64) {
                g.VRAM_Usage =
                    Clamp01((double)g.VRAM_byteUsed / (double)g.VRAM_byteTotal);
            }
            out.push_back(std::move(g));
        }

        // 已出现的名称（供 NVML 兜底去重）
        std::unordered_set<std::string> seenNames;
        for (const auto& e : out) seenNames.insert(NormalizeUtf8(e.DeviceName));

        // ---- DXGI 之外剩下的 NVML 设备（如无头机器）；与已有同名者则跳过 ----
        for (std::size_t i = 0; i < nvml.size(); ++i) {
            if (nvmlMatched[i]) continue;
            const std::string nm = WideToUtf8(nvml[i].name);
            if (seenNames.count(NormalizeUtf8(nm))) continue;
            PerformanceQuery::GpuUsage g;
            g.DeviceName = nm;
            if (nvml[i].usageValid) g.Usage = nvml[i].usage;
            if (nvml[i].memValid) {
                g.VRAM_byteUsed = nvml[i].memUsed;
                g.VRAM_byteTotal = nvml[i].memTotal;
                if (g.VRAM_byteTotal > 0)
                    g.VRAM_Usage =
                        Clamp01((double)g.VRAM_byteUsed / (double)g.VRAM_byteTotal);
            }
            if (nvml[i].tempValid) g.Temperature = nvml[i].temp;
            out.push_back(std::move(g));
        }

        // ---- DXGI 不可用时退回 PDH 显存列表 ----
        if (dxgiAdapters_.empty()) {
            for (const auto& kv : vram) {
                PerformanceQuery::GpuUsage g;
                g.DeviceName = WideToUtf8(kv.first);
                g.VRAM_byteUsed = kv.second.first;
                g.VRAM_byteTotal = kv.second.second;
                if (g.VRAM_byteTotal > 0)
                    g.VRAM_Usage =
                        Clamp01((double)g.VRAM_byteUsed / (double)g.VRAM_byteTotal);
                out.push_back(std::move(g));
            }
        }

        // ---- 归一化去重：同名条目只保留“真正有数据”的。
        // 原因：某些机器上 DXGI 会把同一 GPU 枚举出两个 LUID 实例
        // （如 Intel iGPU 的“幽灵”重复项），幽灵项没有任何引擎/NVML 数据。
        // 规则：同名时优先保留有数据的条目；若两条都真有数据（双卡同型号），则都保留。
        {
            std::vector<PerformanceQuery::GpuUsage> dedup;
            std::vector<bool> dedupHasData;
            std::map<std::string, std::size_t> slotByName;
            for (auto& g : out) {
                const std::string key = NormalizeUtf8(g.DeviceName);
                const bool hasData =
                    PerformanceQuery::IsValid(g.Usage) ||
                    g.VRAM_byteUsed != PerformanceQuery::kInvalidU64 ||
                    PerformanceQuery::IsValid(g.Temperature);
                const auto it = slotByName.find(key);
                if (it == slotByName.end()) {
                    slotByName[key] = dedup.size();
                    dedup.push_back(std::move(g));
                    dedupHasData.push_back(hasData);
                    continue;
                }
                const std::size_t idx = it->second;
                if (!dedupHasData[idx] && hasData) {
                    dedup[idx] = std::move(g);  // 用有数据的替换无数据的
                    dedupHasData[idx] = true;
                } else if (dedupHasData[idx] && hasData) {
                    slotByName[key] = dedup.size();  // 两条都有数据：都保留
                    dedup.push_back(std::move(g));
                    dedupHasData.push_back(true);
                }
                // 两条都无数据：丢弃后面这条
            }
            out = std::move(dedup);
        }
        snap_->Publish(std::move(out));
    }

private:
    struct DxgiAdapter {
        std::wstring name;
        unsigned long long luidToken[2] = {0, 0};
        unsigned __int64 dedicatedVRAM = 0;
        bool isNvidia = false;
    };
    using LuidKey = std::pair<unsigned long long, unsigned long long>;

    static bool LuidMatches(const unsigned long long adapterLuid[2],
                            const LuidKey& key) {
        return (adapterLuid[0] == key.first && adapterLuid[1] == key.second) ||
               (adapterLuid[0] == key.second && adapterLuid[1] == key.first);
    }

    static unsigned long long ParseHex(const std::wstring& s) {
        std::size_t pos = 0;
        try {
            if (s.size() > 2 && s[0] == L'0' && (s[1] == L'x' || s[1] == L'X'))
                pos = 2;
            return std::stoull(s.substr(pos), nullptr, 16);
        } catch (...) {
            return 0;
        }
    }

    /// 解析 "pid_.._luid_0x.._0x.._phys_N_eng_M_engtype_XXX"
    static bool ParseEngineInstance(const std::wstring& name,
                                    unsigned long long luid[2], int* phys,
                                    int* eng) {
        *phys = -1;
        *eng = -1;
        std::size_t a = name.find(L"_luid_");
        if (a == std::wstring::npos) return false;
        // luid 之后跟两个 0x 十六进制 token
        std::size_t p = a + 6;
        auto nextToken = [&name, &p]() -> std::wstring {
            if (p >= name.size()) return L"";
            const std::size_t e = name.find(L'_', p);
            if (e == std::wstring::npos) {
                std::wstring t = name.substr(p);
                p = name.size();
                return t;
            }
            std::wstring t = name.substr(p, e - p);
            p = e + 1;
            return t;
        };
        const std::wstring t1 = nextToken();
        const std::wstring t2 = nextToken();
        if (t1.size() < 3 || t2.size() < 3) return false;
        luid[0] = ParseHex(t1);
        luid[1] = ParseHex(t2);
        if (luid[0] == 0 && luid[1] == 0) return false;
        // phys / eng 序号
        auto numOf = [&name](const wchar_t* key) -> int {
            const std::wstring k = key;
            const std::size_t q = name.find(k);
            if (q == std::wstring::npos) return -1;
            std::size_t d = q + k.size();
            while (d < name.size() && (name[d] < L'0' || name[d] > L'9')) ++d;
            std::size_t e = d;
            while (e < name.size() && name[e] >= L'0' && name[e] <= L'9') ++e;
            if (e == d) return -1;
            try {
                return std::stoi(name.substr(d, e - d));
            } catch (...) {
                return -1;
            }
        };
        *phys = numOf(L"_phys_");
        *eng = numOf(L"_eng_");
        return true;
    }

    /// 把每个 (luid,phys,eng) 的所有进程实例使用率求和 -> 每 luid 引擎总使用率
    void CollectEnginePct(std::map<LuidKey, double>& out) const {
        const auto items = pdh_.Read(engineIdx_);
        std::map<std::tuple<unsigned long long, unsigned long long, int, int>, double> perEngine;
        for (const auto& it : items) {
            if (!it.ok || it.name.empty()) continue;
            unsigned long long luid[2] = {0, 0};
            int phys = -1, eng = -1;
            if (!ParseEngineInstance(it.name, luid, &phys, &eng)) continue;
            if (phys >= 0 && eng >= 0) {
                perEngine[std::make_tuple(luid[0], luid[1], phys, eng)] += it.value;
            } else {
                // 解析不出 phys/eng 时视为独立引擎，直接累加到适配器
                auto& v = out[{luid[0], luid[1]}];
                v = std::min(100.0, v + it.value);
            }
        }
        for (const auto& kv : perEngine) {
            auto& v = out[{std::get<0>(kv.first), std::get<1>(kv.first)}];
            v = std::min(100.0, v + kv.second);  // 每个引擎最多 100%
        }
    }

    void CollectVram(
        std::map<std::wstring, std::pair<std::uint64_t, std::uint64_t>>& out) const {
        const auto used = pdh_.Read(memUsedIdx_);
        const auto limit = pdh_.Read(memLimitIdx_);
        for (const auto& it : used) {
            if (!it.ok) continue;
            out[NormalizeName(it.name)].first = ToU64(it.value);
        }
        for (const auto& it : limit) {
            if (!it.ok) continue;
            out[NormalizeName(it.name)].second = ToU64(it.value);
        }
    }

    int FindNvml(const std::vector<NvmlBackend::Gpu>& nvml,
                 const std::wstring& keyName, std::vector<bool>& matched) const {
        for (std::size_t i = 0; i < nvml.size(); ++i) {
            if (matched[i]) continue;
            if (NormalizeName(nvml[i].name) == keyName) {
                matched[i] = true;
                return (int)i;
            }
        }
        return -1;
    }

    void EnumerateDxgi() {
        // IID_IDXGIFactory1（EnumAdapters1 为成员函数，无需 IID_IDXGIAdapter1）
        static const GUID kIIDFactory1 = {0x770aae78, 0xf26f, 0x4dba, 0xa8, 0x29,
                                          0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87};
        IDXGIFactory1* factory = nullptr;
        const HRESULT hr = CreateDXGIFactory1(kIIDFactory1, (void**)&factory);
        if (FAILED(hr) || !factory) return;
        for (UINT i = 0;; ++i) {
            IDXGIAdapter1* adapter = nullptr;
            if (factory->EnumAdapters1(i, &adapter) != S_OK || !adapter) break;
            DXGI_ADAPTER_DESC1 desc{};
            if (SUCCEEDED(adapter->GetDesc1(&desc))) {
                const bool software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
                const bool msVendor = (desc.VendorId == 0x1414);
                std::wstring name(desc.Description);
                if (!software && !msVendor && !name.empty()) {
                    DxgiAdapter ad;
                    ad.name = name;
                    ad.luidToken[0] = (unsigned long long)desc.AdapterLuid.LowPart;
                    ad.luidToken[1] = (unsigned long long)desc.AdapterLuid.HighPart;
                    ad.dedicatedVRAM = (unsigned __int64)desc.DedicatedVideoMemory;
                    ad.isNvidia = (desc.VendorId == 0x10DE);
                    dxgiAdapters_.push_back(std::move(ad));
                }
            }
            adapter->Release();
        }
        factory->Release();
    }

    Snapshot<std::vector<PerformanceQuery::GpuUsage>>* snap_;
    int intervalMs_;
    NvmlBackend nvml_;
    bool nvmlOk_ = false;
    std::vector<NvmlBackend::Gpu> nvmlCache_;
    std::vector<DxgiAdapter> dxgiAdapters_;
    PdhQuery pdh_;
    int engineIdx_ = -1, memUsedIdx_ = -1, memLimitIdx_ = -1;
    std::unique_ptr<PeriodicSampler> worker_;
};

// ============================================================================
// 实现类：持有快照 + 各采集器
// ============================================================================
class PerformanceQueryImpl {
public:
    PerformanceQueryImpl() = default;
    ~PerformanceQueryImpl() = default;

    int Init(int flags, int intervalMs) {
        std::lock_guard<std::mutex> lk(initMu_);
        if (started_) {
            // 幂等：报告本次请求中“未运行或已失败”的部分
            return ((flags & ~startMask_) | (flags & failedMask_)) & ALL_FLAGS;
        }
        started_ = true;
        startMask_ = flags & ALL_FLAGS;
        intervalMs_ = std::max(
            250, intervalMs > 0 ? intervalMs
                                : PerformanceQuery::kDefaultSamplingIntervalMs);

        int failed = 0;

        if (flags & PERFORMANCE_QUERY_INIT_CPU) {
            cpu_ = std::make_unique<CpuCollector>(cpuSnap_, intervalMs_);
            if (!cpu_->Start()) failed |= PERFORMANCE_QUERY_INIT_CPU;
        }
        if (flags & PERFORMANCE_QUERY_INIT_MEM) {
            mem_ = std::make_unique<MemCollector>(memSnap_, intervalMs_);
            if (!mem_->Start()) failed |= PERFORMANCE_QUERY_INIT_MEM;
        }
        if (flags & PERFORMANCE_QUERY_INIT_DISK) {
            disk_ = std::make_unique<DiskCollector>(diskSnap_, intervalMs_);
            if (!disk_->Start()) failed |= PERFORMANCE_QUERY_INIT_DISK;
        }
        if (flags & PERFORMANCE_QUERY_INIT_NETWORK) {
            net_ = std::make_unique<NetCollector>(netSnap_, intervalMs_);
            if (!net_->Start()) failed |= PERFORMANCE_QUERY_INIT_NETWORK;
        }
        if (flags & PERFORMANCE_QUERY_INIT_GPU) {
            gpu_ = std::make_unique<GpuCollector>(gpuSnap_, intervalMs_);
            if (!gpu_->Start()) failed |= PERFORMANCE_QUERY_INIT_GPU;
        }

        failedMask_ = failed;
        return failed;
    }

    PerformanceQuery::CpuUsage GetCpuUsage() const { return *cpuSnap_.Peek(); }
    std::vector<PerformanceQuery::GpuUsage> GetGpuUsage() const {
        return *gpuSnap_.Peek();
    }
    void GetSystemMemoryUsage(PerformanceQuery::MemUsage& out) const {
        out = *memSnap_.Peek();
    }
    std::vector<PerformanceQuery::DiskUsage> GetDiskIOUsage() const {
        return *diskSnap_.Peek();
    }
    std::vector<PerformanceQuery::NetworkUsage> GetNetworkIOUsage() const {
        return *netSnap_.Peek();
    }

private:
    static constexpr int ALL_FLAGS = PERFORMANCE_QUERY_INIT_ALL;

    std::mutex initMu_;
    bool started_ = false;
    int startMask_ = 0;
    int failedMask_ = 0;
    int intervalMs_ = PerformanceQuery::kDefaultSamplingIntervalMs;

    // 注意成员声明顺序：快照在前、采集器在后，
    // 析构时采集器(会先停止采样线程)先于快照被销毁。
    Snapshot<PerformanceQuery::CpuUsage> cpuSnap_;
    Snapshot<PerformanceQuery::MemUsage> memSnap_;
    Snapshot<std::vector<PerformanceQuery::DiskUsage>> diskSnap_;
    Snapshot<std::vector<PerformanceQuery::NetworkUsage>> netSnap_;
    Snapshot<std::vector<PerformanceQuery::GpuUsage>> gpuSnap_;

    std::unique_ptr<CpuCollector> cpu_;
    std::unique_ptr<MemCollector> mem_;
    std::unique_ptr<DiskCollector> disk_;
    std::unique_ptr<NetCollector> net_;
    std::unique_ptr<GpuCollector> gpu_;
};

}  // namespace pq_internal

// ============================================================================
// 公共 API 实现
// ============================================================================
PerformanceQuery::PerformanceQuery()
    : pImpl_(std::make_unique<pq_internal::PerformanceQueryImpl>()) {}

PerformanceQuery::~PerformanceQuery() = default;

int PerformanceQuery::Init(int flags, int intervalMs) {
    return pImpl_->Init(flags, intervalMs);
}

PerformanceQuery::CpuUsage PerformanceQuery::GetCpuUsage() const {
    return pImpl_->GetCpuUsage();
}

std::vector<PerformanceQuery::GpuUsage> PerformanceQuery::GetGpuUsage() const {
    return pImpl_->GetGpuUsage();
}

void PerformanceQuery::GetSystemMemoryUsage(MemUsage& out) const {
    pImpl_->GetSystemMemoryUsage(out);
}

std::vector<PerformanceQuery::DiskUsage> PerformanceQuery::GetDiskIOUsage() const {
    return pImpl_->GetDiskIOUsage();
}

std::vector<PerformanceQuery::NetworkUsage> PerformanceQuery::GetNetworkIOUsage() const {
    return pImpl_->GetNetworkIOUsage();
}

#else  // !defined(_WIN32) ----------------------------------------------------

// 非 Windows 平台的桩实现：库可正常编译链接，所有指标按“不可读”处理。
// 后续接入 Linux/macOS 时替换为 /proc、sysctl 等数据源即可。

namespace pq_internal {
class PerformanceQueryImpl {
public:
    int Init(int flags, int /*intervalMs*/) {
        return flags & (PERFORMANCE_QUERY_INIT_CPU | PERFORMANCE_QUERY_INIT_GPU |
                        PERFORMANCE_QUERY_INIT_MEM | PERFORMANCE_QUERY_INIT_DISK |
                        PERFORMANCE_QUERY_INIT_NETWORK);
    }
    PerformanceQuery::CpuUsage GetCpuUsage() const { return {}; }
    std::vector<PerformanceQuery::GpuUsage> GetGpuUsage() const { return {}; }
    void GetSystemMemoryUsage(PerformanceQuery::MemUsage& out) const { out = {}; }
    std::vector<PerformanceQuery::DiskUsage> GetDiskIOUsage() const { return {}; }
    std::vector<PerformanceQuery::NetworkUsage> GetNetworkIOUsage() const { return {}; }
};
}  // namespace pq_internal

PerformanceQuery::PerformanceQuery()
    : pImpl_(std::make_unique<pq_internal::PerformanceQueryImpl>()) {}
PerformanceQuery::~PerformanceQuery() = default;

int PerformanceQuery::Init(int flags, int intervalMs) {
    return pImpl_->Init(flags, intervalMs);
}
PerformanceQuery::CpuUsage PerformanceQuery::GetCpuUsage() const {
    return pImpl_->GetCpuUsage();
}
std::vector<PerformanceQuery::GpuUsage> PerformanceQuery::GetGpuUsage() const {
    return pImpl_->GetGpuUsage();
}
void PerformanceQuery::GetSystemMemoryUsage(MemUsage& out) const {
    pImpl_->GetSystemMemoryUsage(out);
}
std::vector<PerformanceQuery::DiskUsage> PerformanceQuery::GetDiskIOUsage() const {
    return pImpl_->GetDiskIOUsage();
}
std::vector<PerformanceQuery::NetworkUsage> PerformanceQuery::GetNetworkIOUsage() const {
    return pImpl_->GetNetworkIOUsage();
}

#endif  // _WIN32
