// 临时验证工具: 打印 PerformanceMonitor 各接口枚举结果
#include <winsock2.h>
#include <cstring>
#include "../include/PerformanceMonitor.hpp"
#include <cstdio>
#include <cmath>
#include <thread>
#include <chrono>
#include <atomic>

static void PrintCpu(PerformanceMonitor& pm) {
    auto c = pm.GetCpuUsage();
    printf("CPU: usage=%.1f%% cores=%u", c.Usage, c.CoreCount);
    printf(" coreusage=[");
    for (size_t i = 0; i < c.CoreUsage.size(); ++i)
        printf("%s%.0f", i ? "," : "", c.CoreUsage[i]);
    printf("] speed=%.2fGHz temp=%s\n", c.SpeedGHz,
           std::isnan(c.Temperature) ? "N/A" : "ok");
}

static void PrintGpu(PerformanceMonitor& pm) {
    auto gpus = pm.GetGpuUsage();
    printf("GPU count=%zu\n", gpus.size());
    for (auto& g : gpus) {
        printf("  [%s]\n", g.DeviceName.c_str());
        printf("    util=%s vram=%s temp=%s\n",
               std::isnan(g.GpuUsage) ? "N/A" : "ok",
               g.VRAM_byteTotal ? "ok" : "N/A",
               std::isnan(g.Temperature) ? "N/A" : "ok");
        if (!std::isnan(g.GpuUsage))     printf("      GpuUsage=%.1f%%\n", g.GpuUsage);
        if (g.VRAM_byteTotal)            printf("      VRAM=%.0f%% (%llu/%llu MB)\n", g.VRAM_Usage,
                                                (unsigned long long)(g.VRAM_byteUsed / 1048576),
                                                (unsigned long long)(g.VRAM_byteTotal / 1048576));
        if (!std::isnan(g.Temperature))  printf("      Temperature=%.0f C\n", g.Temperature);
    }
}

static void PrintMem(PerformanceMonitor& pm) {
    PerformanceMonitor::MemUsage m;
    pm.GetSystemMemoryUsage(m);
    printf("MEM: usage=%.1f%% used=%lluMB total=%lluMB\n", m.Usage,
           (unsigned long long)(m.byteUsed / 1048576),
           (unsigned long long)(m.byteTotal / 1048576));
}

static void PrintDisk(PerformanceMonitor& pm) {
    auto disks = pm.GetDiskIOUsage();
    printf("DISK count=%zu\n", disks.size());
    for (auto& d : disks)
        printf("  [%s] R=%.0f%% W=%.0f%% T=%.0f%% %lluB/s rd %lluB/s wr\n", d.DeviceName.c_str(),
               d.ReadUsage, d.WriteUsage, d.TotalUsage,
               (unsigned long long)d.byteReadPerSec, (unsigned long long)d.byteWritePerSec);
}

static void PrintNet(PerformanceMonitor& pm) {
    auto nets = pm.GetNetworkIOUsage();
    printf("NET count=%zu\n", nets.size());
    for (auto& n : nets)
        printf("  [%s] up=%lluB/s dn=%lluB/s usage=%.1f%%\n", n.DeviceName.c_str(),
               (unsigned long long)n.byteSentPerSec,
               (unsigned long long)n.byteReceivedPerSec, n.Usage);
}

int main() {
    PerformanceMonitor pm;
    int ok = pm.Init();
    printf("Init ok flags=0x%X\n", ok);

    PrintGpu(pm);
    PrintCpu(pm);
    PrintMem(pm);
    PrintDisk(pm);
    PrintNet(pm);

    // 主动制造磁盘写流量 + 网络流量, 验证速率数据真实可读
    std::vector<char> buf(1024 * 1024, 'B');
    FILE* f = fopen("perf_tmp.bin", "wb");
    for (int i = 0; i < 32 && f; ++i)
        fwrite(buf.data(), 1, buf.size(), f);
    if (f) fclose(f);

    // 网络流量: 尝试向本地网关发起连接 (有数据则会产生流量)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(80);
        addr.sin_addr.s_addr = htonl(0x08080808); // 8.8.8.8
        if (connect(s, (sockaddr*)&addr, sizeof(addr)) == 0) {
            const char* req = "GET / HTTP/1.1\r\nHost: 8.8.8.8\r\n\r\n";
            send(s, req, (int)strlen(req), 0);
            char tmp[4096];
            recv(s, tmp, sizeof(tmp), 0);
        }
        closesocket(s);
        WSACleanup();
    }
    remove("perf_tmp.bin");

    std::this_thread::sleep_for(std::chrono::seconds(1));
    printf("---- after IO + 1s ----\n");
    PrintCpu(pm);
    PrintDisk(pm);
    PrintNet(pm);

    // CPU 压力测试: 忙循环 1s, 期间连续采样验证使用率准确稳定
    printf("---- CPU stress test ----\n");
    std::atomic<bool> stop{ false };
    std::thread worker([&] {
        volatile double x = 1.0;
        while (!stop.load(std::memory_order_relaxed)) x = x * 1.0000001 + 0.0000001;
    });
    for (int i = 0; i < 5; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        auto c = pm.GetCpuUsage();
        printf("  t%d: usage=%.1f%% cores=%u coreusage=[", i, c.Usage, c.CoreCount);
        for (size_t k = 0; k < c.CoreUsage.size(); ++k) printf("%s%.0f", k ? "," : "", c.CoreUsage[k]);
        printf("]\n");
    }
    stop.store(true, std::memory_order_relaxed);
    worker.join();
    remove("perf_tmp.bin");
    return 0;
}
