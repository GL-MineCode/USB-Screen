#pragma once
/**
 * PerformanceQuery - 跨平台性能监控库（当前实现：Windows）
 *
 * 特性：
 *  - 监控 CPU / GPU / 内存 / 磁盘 IO / 网络 IO 等系统性能指标；
 *  - 友好型 API：返回经后台采样、平滑/防抖动处理后的“可直接用于展示”的值；
 *  - 所有读取工作都在独立的采样线程中完成，调用方线程（主线程）不会被阻塞；
 *  - 同一个实例可被任意多个线程并发调用，getter 内部无锁等待、线程安全；
 *  - 值不可用(尚未采样成功 / 硬件不支持 / 读取失败)时：
 *       浮点/双精度字段  -> NaN
 *       无符号整数字段    -> 0xFFFFFFFFFFFFFFFF / 0xFFFFFFFF（全 1 哨兵值）
 *       字符串字段        -> 空串
 *    可用 PerformanceQuery::IsValid(...) 判断。
 *
 * 构建：
 *  默认构建为 Windows 共享库(VC 式 .lib 导入库 + .dll)，可通过 CMake 选项
 *  PERFORMANCEQUERY_BUILD_SHARED 切换为静态库，详见 README.md。
 */

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// 动态库导出宏（VC 风格 __declspec(dllexport / dllimport)）
//   * 构建共享库目标时需定义 PERFORMANCEQUERY_EXPORTS；
//   * 使用共享库的编译单元需定义 PERFORMANCEQUERY_SHARED（通过 CMake 链接本库时自动注入）；
//   * 静态库或非 Windows 平台下此宏为空。
// ---------------------------------------------------------------------------
#if defined(_WIN32) && defined(PERFORMANCEQUERY_SHARED)
#  if defined(PERFORMANCEQUERY_EXPORTS)
#    define PERFORMANCEQUERY_API __declspec(dllexport)
#  else
#    define PERFORMANCEQUERY_API __declspec(dllimport)
#  endif
#else
#  define PERFORMANCEQUERY_API
#endif

// ---------------------------------------------------------------------------
// 初始化标志位（原宏风格的 inline constexpr，避免宏污染）
// ---------------------------------------------------------------------------
inline constexpr int PERFORMANCE_QUERY_INIT_CPU     = 0x01;  // CPU
inline constexpr int PERFORMANCE_QUERY_INIT_GPU     = 0x02;  // GPU
inline constexpr int PERFORMANCE_QUERY_INIT_MEM     = 0x04;  // 内存
inline constexpr int PERFORMANCE_QUERY_INIT_DISK    = 0x08;  // 磁盘 IO
inline constexpr int PERFORMANCE_QUERY_INIT_NETWORK = 0x10;  // 网络 IO
inline constexpr int PERFORMANCE_QUERY_INIT_ALL =
    (PERFORMANCE_QUERY_INIT_CPU | PERFORMANCE_QUERY_INIT_GPU |
     PERFORMANCE_QUERY_INIT_MEM | PERFORMANCE_QUERY_INIT_DISK |
     PERFORMANCE_QUERY_INIT_NETWORK);

namespace pq_internal {
class PerformanceQueryImpl;  // PImpl 实现（内部细节），仅前向声明
}  // namespace pq_internal

/**
 * @brief 性能监控器主类
 *
 * 使用方式：
 * @code
 *   PerformanceQuery pq;
 *   PerformanceQuery pq;
 *   int failed = pq.Init(PERFORMANCE_QUERY_INIT_ALL);  // 非 0 表示有指标初始化失败
 *   auto cpu = pq.GetCpuUsage();
 * @endcode
 *
 * 线程安全约定：
 *  - Init() 应在线程并发使用前调用一次（重复调用是安全的幂等操作）；
 *  - 各 Get*() 返回最近一次后台采样结果，可在任意线程任意时刻并发调用；
 *  - 同一实例在不同线程调用完全线程安全；不同实例之间互相独立。
 */
class PERFORMANCEQUERY_API PerformanceQuery {
public:
    /// 默认采样间隔(毫秒)。该间隔即“平滑窗口”，值每间隔更新一次。
    static constexpr int kDefaultSamplingIntervalMs = 1000;

    // ---- 无效值标记 -------------------------------------------------------
    static constexpr std::uint32_t kInvalidU32 =
        (std::numeric_limits<std::uint32_t>::max)();
    static constexpr std::uint64_t kInvalidU64 =
        (std::numeric_limits<std::uint64_t>::max)();

    // ---- 有效性判断 -------------------------------------------------------
    static bool IsValid(double v) noexcept { return v == v; }  // NaN 即无效
    static bool IsValid(std::uint32_t v) noexcept { return v != kInvalidU32; }
    static bool IsValid(std::uint64_t v) noexcept { return v != kInvalidU64; }

public:
    PerformanceQuery();
    ~PerformanceQuery();

    // 禁止拷贝/赋值（内部持有采样线程与句柄）
    PerformanceQuery(const PerformanceQuery&) = delete;
    PerformanceQuery& operator=(const PerformanceQuery&) = delete;

    /**
     * @brief 初始化并启动对应指标的采样线程
     * @param flags       PERFORMANCE_QUERY_INIT_* 按位或
     * @param intervalMs  采样间隔(毫秒)，<=0 时使用默认值；过小会被抬升到 500ms
     * @return 0 表示全部成功；否则返回“初始化失败”的指标标志位按位或，
     *         例如 CPU 与 GPU 失败则返回 (INIT_CPU | INIT_GPU)
     * @note  重复调用只会在首次生效；再次调用返回当前状态（成功 0 / 失败位）。
     */
    int Init(int flags, int intervalMs = kDefaultSamplingIntervalMs);

    // -----------------------------------------------------------------------
    // 数据定义
    // -----------------------------------------------------------------------

    /// CPU 使用情况。CoreUsage 为 0.0~1.0（1.0=100%）；
    /// Usage 常规为 0.0~1.0，睿频高于基准频率时可短暂 >1.0（>100%），口径同任务管理器。
    struct CpuUsage {
        double Usage = std::numeric_limits<double>::quiet_NaN();  // 总体占用(任务管理器同款 % Processor Utility)
        std::uint32_t CoreCount = kInvalidU32;                    // 逻辑处理器个数
        std::vector<double> CoreUsage;                            // 每个逻辑核的占用(与 CoreCount 等长)
        double SpeedGHz = std::numeric_limits<double>::quiet_NaN();   // 处理器基准/标称频率
        double Temperature = std::numeric_limits<double>::quiet_NaN();// CPU 温度(通用 API 一般读不到→NaN)
    };

    /// 单个 GPU 的使用情况。无 NVIDIA 驱动/计数器时相关字段为 NaN/哨兵值。
    struct GpuUsage {
        std::string DeviceName;                                  // 适配器名称
        double Usage = std::numeric_limits<double>::quiet_NaN();       // GPU 占用 0.0~1.0
        double VRAM_Usage = std::numeric_limits<double>::quiet_NaN(); // 显存占用 0.0~1.0
        std::uint64_t VRAM_byteUsed = kInvalidU64;
        std::uint64_t VRAM_byteTotal = kInvalidU64;
        double Temperature = std::numeric_limits<double>::quiet_NaN(); // ℃
    };

    /// 系统内存使用情况
    struct MemUsage {
        double Usage = std::numeric_limits<double>::quiet_NaN();  // 0.0~1.0
        std::uint64_t byteUsed = kInvalidU64;
        std::uint64_t byteTotal = kInvalidU64;
    };

    /// 单块物理磁盘的 IO 情况
    struct DiskUsage {
        std::string DeviceName;                                  // PDH 实例名，如 "0 C:"
        double ReadUsage = std::numeric_limits<double>::quiet_NaN();   // 0.0~1.0
        double WriteUsage = std::numeric_limits<double>::quiet_NaN();  // 0.0~1.0
        double TotalUsage = std::numeric_limits<double>::quiet_NaN();  // 0.0~1.0
        std::uint64_t byteReadPerSec = kInvalidU64;              // 字节/秒
        std::uint64_t byteWritePerSec = kInvalidU64;             // 字节/秒
    };

    /// 单个网络适配器的 IO 情况
    struct NetworkUsage {
        std::string DeviceName;
        double Usage = std::numeric_limits<double>::quiet_NaN();      // 0.0~1.0(相对链路带宽)
        std::uint64_t byteSentPerSec = kInvalidU64;
        std::uint64_t byteReceivedPerSec = kInvalidU64;
    };

    // -----------------------------------------------------------------------
    // 查询接口：均返回最近一次后台采样结果，非阻塞、线程安全
    // -----------------------------------------------------------------------

    /**
     * @brief 获取 CPU 占用情况
     * @return 总体占用(Usage)与任务管理器同口径：主源为 PDH "% Processor Utility"(_Total)
     *         （计入实际运行频率，睿频时可 >100%），PDH 不可用时退回时钟差值计算；
     *         CoreUsage 为每个逻辑核占用(0.0~1.0)。每 ~intervalMs 更新一次，天然平滑。
     */
    CpuUsage GetCpuUsage() const;

    /** @brief 获取全部 GPU 的使用情况（每个 GPU 一个元素） */
    std::vector<GpuUsage> GetGpuUsage() const;

    /** @brief 获取系统物理内存使用情况（写入 out） */
    void GetSystemMemoryUsage(MemUsage& out) const;

    /** @brief 获取每块物理磁盘的 IO 使用情况 */
    std::vector<DiskUsage> GetDiskIOUsage() const;

    /** @brief 获取每个网络适配器的 IO 使用情况 */
    std::vector<NetworkUsage> GetNetworkIOUsage() const;

private:
    std::unique_ptr<pq_internal::PerformanceQueryImpl> pImpl_;
};
