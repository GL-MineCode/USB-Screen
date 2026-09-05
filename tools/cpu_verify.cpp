// 临时验证工具：对照任务管理器核对 CPU 使用率数值
#include <cstdio>
#include <cmath>
#include <thread>
#include <chrono>
#include "../include/PerformanceQuery.hpp"

int main() {
    PerformanceQuery pq;
    int fail = pq.Init(PERFORMANCE_QUERY_INIT_CPU);
    printf("Init CPU failed=0x%X (mask CPU=0x%X)\n", fail,
           (unsigned)PERFORMANCE_QUERY_INIT_CPU);

    // 首条多为初始帧(无效/占位)，等 1.2s 让后台采样产出真数据后再记录
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    for (int i = 0; i < 10; ++i) {
        auto c = pq.GetCpuUsage();
        double avgCore = 0.0;
        int cnt = 0;
        for (double v : c.CoreUsage)
            if (v == v) { avgCore += v; ++cnt; }
        if (cnt > 0) avgCore /= cnt;

        printf("[%d] Usage=%6.2f%% (PDH _Total Utility, same as TaskMgr)"
               "  coreAvg=%6.2f%%  cores=%u  core=[",
               i, c.Usage * 100.0, avgCore * 100.0, c.CoreCount);
        for (size_t k = 0; k < c.CoreUsage.size(); ++k)
            printf("%s%.0f", k ? "," : "", c.CoreUsage[k] * 100.0);
        printf("]\n");

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}
