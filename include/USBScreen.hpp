#pragma once
// =============================================================================
//  MSNDevice — MSU2-MINI USB 屏幕 (MSN) 开发者友好 C++ API
// -----------------------------------------------------------------------------
//  依据逆向恢复的 MSU2_MINI_DemoV1.6_Output_recovered.py 通信协议封装。
//
//  协议概要 (串口 19200-8N1)：
//    所有命令帧均为 6 字节 [头, 命令/类型, 子命令/数据, D2, D3, D4]
//      头 0x00 —— 寄存器 (SFR) 读写 / 数据表
//      头 0x02 —— LCD 显示指令
//      头 0x03 —— Flash 操作
//      头 0x04 —— 数据写入 (每次 4 字节, 用于 Flash / LCD 像素数据)
//      头 0x08 —— ADC 读取
//
//  使用注意：
//    - 本头文件包含 GL_Serial 实现 (GL_SERIAL_IMPLEMENTATION)，
//      请保证在「同一个 .cpp」中 include 本文件（例如 main.cpp）。
//    - 所有 I/O 方法内部已加锁 (std::recursive_mutex)，可安全跨线程调用；
//      但同一时刻请勿让两个线程操作同一个设备（读写会按调用顺序串行化）。
//    - 通信失败时 IsConnected() 会变为 false，可用 Connect() 重连。
//
//  示例：
//    MSNDevice dev;
//    if (!dev.ConnectAny()) return;
//    dev.RefreshDataTable();                       // 读取设备数据表
//    auto cpu = dev.ReadDataU16("MSN_Status");     // 读数据项
//    dev.LcdShowPhoto(0, 0, 160, 80, 3826);        // 显示 Flash 图片
//    dev.LcdShowGB2312(8, 8, "\xc4\xe3", MSNDevice::kRed, MSNDevice::kBlack); // 显示"你" (GB2312)
// =============================================================================

#include <cstdint>
#include <cstddef>
#include <cctype>
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <algorithm>
#include <chrono>
#include <thread>
#include <mutex>
#include <fstream>
#include <initializer_list>

#include <SDL3/SDL.h>   // 图片工具基于 SDL_Surface

#define GL_SERIAL_IMPLEMENTATION
#include "GL_Serial.hpp"

class MSNDevice {
public:
    // ======================= 基础常量 (RGB565 颜色) =======================
    static constexpr uint16_t kRed    = 63488;
    static constexpr uint16_t kGreen  = 2016;
    static constexpr uint16_t kBlue   = 31;
    static constexpr uint16_t kWhite  = 65535;
    static constexpr uint16_t kBlack  = 0;
    static constexpr uint16_t kYellow = 65504;
    static constexpr uint16_t kGray0  = 61309;
    static constexpr uint16_t kGray1  = 33808;
    static constexpr uint16_t kGray2  = 16904;

    static constexpr uint32_t kDefaultBaudRate = 19200;
    static constexpr uint16_t kLcdWidth  = 160;   // LCD 分辨率
    static constexpr uint16_t kLcdHeight = 80;

    // ======================= 图片缩放适应模式 =======================
    // 用于图片工具 (ConvertImageToLCD565 / ResizeTo), 决定源图如何放进目标区域
    enum class ImageFitMode : uint8_t {
        Fit,      // 适应: 等比缩放完整显示, 留出的空白用背景色填充 (letterbox)
        Fill,     // 填充: 等比缩放铺满目标, 超出部分居中裁剪 (cover, 默认)
        Stretch,  // 拉伸: 直接拉伸到目标尺寸, 可能变形
    };

    // ======================= 数据类型 =======================
    // 设备 SFR 数据表中的一个数据项 (对应 Python MSN_Data)
    struct DataItem {
        std::string name;              // 名称, 如 "MSN_Status"
        std::string unit;              // 单位
        uint8_t     family = 0;        // 类型字段: 高 3 位=类型, 低 5 位=长度
        std::vector<uint8_t> data;     // 地址(1~2字节) 或 原始数据
    };

    // family 高 3 位定义的数据类型
    enum class Family : uint8_t {
        U8Addr  = 0,   // data=u8 SFR 地址(2字节), 读取长度 = family & 0x1F
        U16Addr = 1,   // data=u16 SFR 地址(1字节), 长度 = family & 0x1F
        U32Addr = 2,   // data=u32 SFR 地址(2字节), 长度 = family & 0x1F
        String  = 3,   // 字符串, 长度 = family & 0x1F
        U8Array = 4,   // u8 数组, 长度 = family & 0x1F
    };

    MSNDevice()  = default;
    ~MSNDevice() { Disconnect(); }

    MSNDevice(const MSNDevice&)            = delete;
    MSNDevice& operator=(const MSNDevice&) = delete;
    MSNDevice(MSNDevice&&)                 = delete;   // 内部含互斥锁, 不可移动
    MSNDevice& operator=(MSNDevice&&)      = delete;

    // ======================= 连接管理 =======================
    static std::vector<std::string> ListPorts();                    // 枚举系统串口
    bool Connect(const std::string& port, uint32_t timeout_ms = 3000); // 打开指定端口并完成握手
    bool ConnectAny(uint32_t timeout_ms = 3000);                    // 自动扫描所有串口
    void Disconnect();
    bool IsConnected() const { return connected_; }
    std::string Port() const    { return port_; }
    int  Version() const        { return version_; }

    void SetTimeout(uint32_t ms) { timeout_ms_ = ms; }              // 单次应答超时
    uint32_t Timeout() const     { return timeout_ms_; }

    void SetErrorCallback(std::function<void(const std::string&)> cb) { err_cb_ = std::move(cb); }
    std::string GetLastError() const { return last_error_; }

    // ======================= 寄存器读写 (头 0x00) =======================
    // 注: 原协议 u16 寄存器地址只使用低字节 (与 Python 实现一致)
    uint8_t  ReadReg8 (uint16_t addr);                  // -> 返回寄存器值
    uint16_t ReadReg16(uint16_t addr);                  // -> 返回寄存器值
    bool     WriteReg8 (uint16_t addr, uint8_t  value);
    bool     WriteReg16(uint16_t addr, uint16_t value);
    uint16_t ReadADC(uint8_t channel);                  // 头 0x08, 读取 ADC 通道

    // ======================= MSN 数据表 (SFR) =======================
    bool RefreshDataTable(uint16_t base_addr = 256);    // 读取并解析 256 字节数据表
    const std::vector<DataItem>& DataTable() const { return table_; }
    static Family FamilyOf(const DataItem& it) { return static_cast<Family>(it.family >> 5); }

    bool HasData(const std::string& name) const;
    std::vector<uint8_t> ReadData(const std::string& name);  // 按项类型读取数据
    std::optional<uint16_t> ReadDataU16(const std::string& name); // u16 便捷读取
    bool WriteData(const std::string& name, uint16_t value); // 写入 u8/u16 项

    // ======================= Flash 操作 (头 0x03) =======================
    bool FlashErase(uint32_t page_addr, uint16_t page_count);
    uint8_t FlashReadByte(uint32_t addr);
    bool FlashWritePage(uint32_t addr, const std::vector<uint8_t>& page_data,
                        uint8_t page_num = 1, bool fast = true);
    bool FlashWrite(uint32_t base_addr, const std::vector<uint8_t>& data); // 自动擦除+分页
    bool FlashWriteFile(uint32_t base_addr, const std::string& path);      // 烧写 .bin
    bool FlashWriteFont(uint32_t base_addr, const std::string& path);      // 字库(去 6 字节头)

    // ======================= LCD 显示 (头 0x02) =======================
    bool LcdSetXY(uint16_t x, uint16_t y);
    bool LcdSetSize(uint16_t w, uint16_t h);
    bool LcdSetColor(uint16_t fg, uint16_t bg);
    bool LcdBeginArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h); // 设置显示区域
    bool LcdSetState(uint8_t state);                                    // LCD_State
    bool LcdShowPhoto(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t page);  // 显示 Flash 彩色图
    bool LcdShowMonoPhoto(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                          uint16_t page, uint16_t fg, uint16_t bg);     // Flash 黑白图
    bool LcdShowMonoMix(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                        uint16_t page, uint16_t fg, uint16_t bg_page);  // 黑白图+背景页
    bool LcdShowChar32x64(uint16_t x, uint16_t y, char ch, uint16_t fg,
                          uint16_t bg, uint16_t num_page);               // 32x64 ASCII
    bool LcdShowChar32x64Mix(uint16_t x, uint16_t y, char ch, uint16_t fg,
                             uint16_t bg_page, uint16_t num_page);
    bool LcdShowGB2312(uint16_t x, uint16_t y, const std::string& text,
                       uint16_t fg, uint16_t bg);                        // 16x16 汉字(GB2312 两字节)
    bool LcdShowGB2312Mix(uint16_t x, uint16_t y, const std::string& text,
                          uint16_t fg, uint16_t bg_page);
    bool LcdFillColor(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
    bool LcdSendData(const std::vector<uint8_t>& data, uint16_t size);   // 发送 256B 显示数据页
    bool LcdShowImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      const std::vector<uint8_t>& rgb565, bool compress = true); // 直接推 RGB565 数据 (每帧重设区域)
    bool LcdShowImageFast(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                          const std::vector<uint8_t>& rgb565, bool compress = true); // 快速推屏: 区域未变时跳过重设+ACK, 适合高频镜像
    bool LcdShowImageFile(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                          const std::string& path);                      // 流式显示 .bin 图片

    // ============ 静态工具 (RGB 用 SDL_Surface*, RGB565 用字节数组) ============
    static std::vector<uint8_t> LoadFile(const std::string& path);                       // 读二进制文件
    static std::vector<uint8_t> SurfaceToRGB565(SDL_Surface* surface);                   // 任意格式 SDL_Surface -> RGB565 字节数组 (高位在前)
    static SDL_Surface* RGB565ToSurface(const std::vector<uint8_t>& rgb565,
                                        uint16_t w, uint16_t h);                         // RGB565 字节数组 -> SDL_Surface (返回新对象, 调用方负责 SDL_DestroySurface)
    static SDL_Surface* ResizeCenterCrop(SDL_Surface* surface,
                                         uint16_t out_w = kLcdWidth,
                                         uint16_t out_h = kLcdHeight);                   // 等比缩放+居中裁剪 (等价 ResizeTo Fill, 返回新对象, 调用方负责 SDL_DestroySurface)
    static SDL_Surface* ResizeTo(SDL_Surface* surface,
                                 uint16_t out_w = kLcdWidth,
                                 uint16_t out_h = kLcdHeight,
                                 ImageFitMode mode = ImageFitMode::Fill,
                                 uint32_t bg_color = 0x000000FF);                       // 按模式缩放/适应/拉伸 (bg_color 为 0xRRGGBBAA, 仅 Fit 模式用; 返回新对象, 调用方负责 SDL_DestroySurface)
    static std::vector<uint8_t> ConvertImageToLCD565(SDL_Surface* surface,
                                                     ImageFitMode mode = ImageFitMode::Fill,
                                                     uint32_t bg_color = 0x000000FF);    // 按模式缩放裁剪到 160x80 并转 RGB565 字节数组

private:
    // ======================= 内部实现 =======================
    bool WriteFrame(const uint8_t* data, size_t n);
    std::vector<uint8_t> RecvResponse(uint32_t min_bytes, uint32_t timeout_ms);
    bool WaitAck(const uint8_t* sent, size_t n, uint32_t timeout_ms);
    bool SendImageData(uint16_t w, uint16_t h, const std::vector<uint8_t>& rgb565, bool compress);
    void SetError(const std::string& msg);

    SerialDevice serial;
    bool     connected_   = false;
    std::string port_;
    int      version_     = 0;
    std::string last_error_;
    std::function<void(const std::string&)> err_cb_;
    std::vector<DataItem> table_;
    std::recursive_mutex io_mutex_;   // 递归锁: 公开方法可能互相调用
    uint32_t timeout_ms_  = 500;
    bool     area_valid_  = false;    // 显示区域已设置且与设备同步 (供 LcdShowImageFast 复用)
    uint16_t area_x_ = 0, area_y_ = 0, area_w_ = 0, area_h_ = 0;
};

// =============================================================================
// 内部实现
// =============================================================================

// ---------------- 错误处理 ----------------
inline void MSNDevice::SetError(const std::string& msg) {
    last_error_ = msg;
    if (err_cb_) err_cb_(msg);
}

// ---------------- 底层收发 ----------------
inline bool MSNDevice::WriteFrame(const uint8_t* data, size_t n) {
    if (!connected_ || !serial.IsOpen()) {
        SetError("设备未连接");
        return false;
    }
    if (!serial.Write(data, n)) {
        SetError("串口发送失败: " + serial.GetLastError());
        connected_ = false;
        return false;
    }
    return true;
}

// 阻塞读取: 累积数据直到达到 min_bytes 且静默 2ms(凑齐整帧), 或超时
inline std::vector<uint8_t> MSNDevice::RecvResponse(uint32_t min_bytes, uint32_t timeout_ms) {
    std::vector<uint8_t> out;
    const auto start = std::chrono::steady_clock::now();
    int idle = 0;
    while (true) {
        std::vector<uint8_t> tmp(64);
        size_t n = serial.ReadAvailable(tmp);
        if (n) {
            out.insert(out.end(), tmp.begin(), tmp.begin() + static_cast<ptrdiff_t>(n));
            idle = 0;
        }
        if (out.size() >= min_bytes && idle >= 2) return out;   // 连续 2ms 无新数据
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= static_cast<std::int64_t>(timeout_ms)) break;
        ++idle;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return out;
}

// 等待应答并校验帧头 (头 2 字节与发送帧一致)
inline bool MSNDevice::WaitAck(const uint8_t* sent, size_t n, uint32_t timeout_ms) {
    (void)n;   // 应答帧头固定为 6 字节, 此处仅需前 2 字节
    auto resp = RecvResponse(2, timeout_ms);
    if (resp.size() < 2) { SetError("等待设备应答超时"); return false; }
    if (resp[0] != sent[0] || resp[1] != sent[1]) {
        SetError("设备应答帧头不匹配");
        connected_ = false;
        return false;
    }
    return true;
}

// ---------------- 连接管理 ----------------
inline std::vector<std::string> MSNDevice::ListPorts() {
    return SerialDevice::ListAvailablePorts();
}

inline bool MSNDevice::Connect(const std::string& port, uint32_t timeout_ms) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    Disconnect();

    SerialDevice::Config cfg;
    cfg.baud_rate        = kDefaultBaudRate;
    cfg.data_bits        = 8;
    cfg.parity           = SerialDevice::Parity::None;
    cfg.stop_bits        = SerialDevice::StopBits::One;
    cfg.read_timeout_ms  = 100;
    cfg.write_timeout_ms = 1000;
    if (!serial.Open(port, cfg)) {
        SetError("打开串口失败: " + serial.GetLastError());
        return false;
    }
    port_ = port;

    // 1) 等待设备上电广播: 读到 "\0MSN" + 两位版本号
    std::string announce;
    const auto start = std::chrono::steady_clock::now();
    while (true) {
        std::vector<uint8_t> tmp(64);
        size_t n = serial.ReadAvailable(tmp);
        if (n) announce.append(reinterpret_cast<const char*>(tmp.data()), n);

        size_t pos = announce.find("MSN");
        while (pos != std::string::npos) {
            if (pos >= 1 && announce[pos - 1] == '\0' &&
                pos + 4 < announce.size() &&
                std::isdigit(static_cast<unsigned char>(announce[pos + 3])) &&
                std::isdigit(static_cast<unsigned char>(announce[pos + 4]))) {
                version_ = (announce[pos + 3] - '0') * 10 + (announce[pos + 4] - '0');

                // 2) 发送握手命令 \0MSNCN
                const uint8_t hb[6] = {0, 'M', 'S', 'N', 'C', 'N'};
                if (!serial.Write(hb, 6)) {
                    SetError("握手发送失败: " + serial.GetLastError());
                    serial.Close();
                    connected_ = false;
                    return false;
                }
                // 3) 校验响应 \0MSNCN
                auto resp = RecvResponse(6, timeout_ms);
                if (resp.size() >= 6 && resp[0] == 0 && resp[1] == 'M' &&
                    resp[2] == 'S' && resp[3] == 'N' && resp[4] == 'C' && resp[5] == 'N') {
                    connected_ = true;
                    return true;
                }
                SetError("设备握手响应不匹配");
                serial.Close();
                connected_ = false;
                return false;
            }
            pos = announce.find("MSN", pos + 1);
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= static_cast<std::int64_t>(timeout_ms)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    SetError("未检测到 MSN 设备应答: " + port);
    serial.Close();
    connected_ = false;
    return false;
}

inline bool MSNDevice::ConnectAny(uint32_t timeout_ms) {
    for (const auto& p : ListPorts()) {
        if (Connect(p, timeout_ms)) return true;
    }
    SetError("未找到可用的 MSN 设备");
    return false;
}

inline void MSNDevice::Disconnect() {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    connected_ = false;
    serial.Close();
    port_.clear();
    version_ = 0;
}

// ---------------- 寄存器读写 ----------------
inline uint8_t MSNDevice::ReadReg8(uint16_t addr) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    const uint8_t f[6] = {0, 48, 0, static_cast<uint8_t>(addr >> 8), static_cast<uint8_t>(addr & 0xFF), 0};
    if (!WriteFrame(f, 6)) return 0;
    auto r = RecvResponse(6, timeout_ms_);
    if (r.size() >= 6) return r[5];
    SetError("读取寄存器超时");
    return 0;
}

inline uint16_t MSNDevice::ReadReg16(uint16_t addr) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    const uint8_t f[6] = {0, 48, 32, static_cast<uint8_t>(addr & 0xFF), 0, 0};
    if (!WriteFrame(f, 6)) return 0;
    auto r = RecvResponse(6, timeout_ms_);
    if (r.size() >= 6) return static_cast<uint16_t>(r[4] * 256 + r[5]);
    SetError("读取寄存器超时");
    return 0;
}

inline bool MSNDevice::WriteReg8(uint16_t addr, uint8_t value) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    const uint8_t f[6] = {0, 48, 128, static_cast<uint8_t>(addr >> 8),
                          static_cast<uint8_t>(addr & 0xFF), static_cast<uint8_t>(value & 0xFF)};
    if (!WriteFrame(f, 6)) return false;
    if (RecvResponse(1, timeout_ms_).empty()) { SetError("写入寄存器无应答"); return false; }
    return true;
}

inline bool MSNDevice::WriteReg16(uint16_t addr, uint16_t value) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    const uint8_t f[6] = {0, 48, 32, static_cast<uint8_t>(addr & 0xFF),
                          static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value & 0xFF)};
    if (!WriteFrame(f, 6)) return false;
    if (RecvResponse(1, timeout_ms_).empty()) { SetError("写入寄存器无应答"); return false; }
    return true;
}

inline uint16_t MSNDevice::ReadADC(uint8_t channel) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    const uint8_t f[6] = {8, channel, 0, 0, 0, 0};
    if (!WriteFrame(f, 6)) return 0;
    auto r = RecvResponse(6, timeout_ms_);
    if (r.size() >= 6) return static_cast<uint16_t>(r[4] * 256 + r[5]);
    SetError("读取 ADC 超时");
    return 0;
}

// ---------------- MSN 数据表 ----------------
inline bool MSNDevice::RefreshDataTable(uint16_t base_addr) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    std::vector<uint8_t> sfr(256);
    for (int i = 0; i < 256; ++i) sfr[i] = ReadReg8(base_addr + i);

    table_.clear();
    int data_type = 0;      // 0=名称, 1=单位, 2=类型, 3=数据
    int data_len  = 0;
    std::vector<uint8_t> use;
    DataItem item;
    for (int i = 0; i < 256; ++i) {
        const uint8_t v = sfr[i];
        if (data_type < 3) {
            if (v != 0) { use.push_back(v); continue; }
            if (use.empty()) break;                      // 读到空段, 表结束
            if (data_type == 0) item.name.assign(use.begin(), use.end());
            else if (data_type == 1) item.unit.assign(use.begin(), use.end());
            else if (data_type == 2) {
                item.family = use[0];
                const uint8_t t = item.family >> 5;      // 类型
                data_type = 3;
                if (t == 0 || t == 2) data_len = 2;      // u8/u32 地址 = 2 字节
                else if (t == 1)      data_len = 1;      // u16 地址 = 1 字节
                else                 data_len = item.family & 0x1F;  // 字符串长度
            }
            use.clear();
            continue;
        }
        // 数据段
        if (data_len > 0) { use.push_back(v); --data_len; }
        if (data_len == 0) {
            item.data = use;
            table_.push_back(item);
            item = DataItem();
            use.clear();
            data_type = 0;
        }
    }
    return true;
}

inline bool MSNDevice::HasData(const std::string& name) const {
    for (const auto& it : table_)
        if (it.name == name) return true;
    return false;
}

inline std::vector<uint8_t> MSNDevice::ReadData(const std::string& name) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    for (const auto& it : table_) {
        if (it.name != name) continue;
        const uint8_t t = it.family >> 5;
        if (t == 0) {                                    // u8 SFR 地址
            if (it.data.size() < 2) return {};
            const uint16_t sfr_add = static_cast<uint16_t>(it.data[0] * 256 + it.data[1]);
            const int len = it.family & 0x1F;
            std::vector<uint8_t> out;
            out.reserve(len);
            for (int n = 0; n < len; ++n) out.push_back(ReadReg8(sfr_add + n));
            return out;
        } else if (t == 1) {                             // u16 SFR 地址
            if (it.data.empty()) return {};
            const uint16_t v = ReadReg16(it.data[0]);
            return {static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v & 0xFF)};
        } else if (t == 3 || t == 4) {                   // 字符串 / u8 数组
            return it.data;
        }
        return {};
    }
    SetError("数据项不存在: " + name);
    return {};
}

inline std::optional<uint16_t> MSNDevice::ReadDataU16(const std::string& name) {
    auto d = ReadData(name);
    if (d.size() == 2) return static_cast<uint16_t>((d[0] << 8) | d[1]);
    return std::nullopt;
}

inline bool MSNDevice::WriteData(const std::string& name, uint16_t value) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    for (const auto& it : table_) {
        if (it.name != name) continue;
        const uint8_t t = it.family >> 5;
        if (t == 0) {                                    // u8
            if (it.data.size() < 2) return false;
            const uint16_t sfr_add = static_cast<uint16_t>(it.data[0] * 256 + it.data[1]);
            return WriteReg8(sfr_add, static_cast<uint8_t>(value & 0xFF));
        } else if (t == 1) {                             // u16
            if (it.data.empty()) return false;
            return WriteReg16(it.data[0], value);
        }
    }
    SetError("数据项不存在或不可写: " + name);
    return false;
}

// ---------------- Flash ----------------
inline bool MSNDevice::FlashErase(uint32_t page_addr, uint16_t page_count) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    const uint8_t f[6] = {3, 2,
                          static_cast<uint8_t>((page_addr % 65536) >> 8),
                          static_cast<uint8_t>(page_addr % 256),
                          static_cast<uint8_t>((page_count % 65536) >> 8),
                          static_cast<uint8_t>(page_count % 256)};
    if (!WriteFrame(f, 6)) return false;
    return WaitAck(f, 6, timeout_ms_);
}

inline uint8_t MSNDevice::FlashReadByte(uint32_t addr) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    const uint8_t f[6] = {3, 0, static_cast<uint8_t>(addr >> 16),
                          static_cast<uint8_t>((addr >> 8) & 0xFF),
                          static_cast<uint8_t>(addr & 0xFF), 0};
    if (!WriteFrame(f, 6)) return 0;
    auto r = RecvResponse(6, timeout_ms_);
    if (r.size() >= 6) return r[5];
    SetError("读取 Flash 超时");
    return 0;
}

inline bool MSNDevice::FlashWritePage(uint32_t addr, const std::vector<uint8_t>& page_data,
                                      uint8_t page_num, bool fast) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    uint8_t page[256];
    for (int i = 0; i < 256; ++i)
        page[i] = (i < static_cast<int>(page_data.size())) ? page_data[i] : 0xFF;

    if (!fast) {
        // 慢速: 逐 4 字节发送, 命令 [3,1] 提交
        std::vector<uint8_t> frame(6);
        for (int i = 0; i < 64; ++i) {
            frame[0] = 4; frame[1] = static_cast<uint8_t>(i);
            frame[2] = page[i * 4 + 0]; frame[3] = page[i * 4 + 1];
            frame[4] = page[i * 4 + 2]; frame[5] = page[i * 4 + 3];
            if (!WriteFrame(frame.data(), 6)) return false;
        }
        frame = {3, 1, static_cast<uint8_t>(addr >> 16),
                 static_cast<uint8_t>((addr >> 8) & 0xFF),
                 static_cast<uint8_t>(addr & 0xFF),
                 static_cast<uint8_t>(page_num & 0xFF)};
        if (!WriteFrame(frame.data(), 6)) return false;
        return WaitAck(frame.data(), 6, timeout_ms_);
    }

    // 快速: 整页拼接后一次发送, 命令 [3,3] 提交
    std::vector<uint8_t> frame;
    frame.reserve(64 * 6 + 6);
    for (int i = 0; i < 64; ++i) {
        frame.push_back(4); frame.push_back(static_cast<uint8_t>(i));
        frame.push_back(page[i * 4 + 0]); frame.push_back(page[i * 4 + 1]);
        frame.push_back(page[i * 4 + 2]); frame.push_back(page[i * 4 + 3]);
    }
    frame.push_back(3); frame.push_back(3);
    frame.push_back(static_cast<uint8_t>(addr >> 16));
    frame.push_back(static_cast<uint8_t>((addr >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(addr & 0xFF));
    frame.push_back(static_cast<uint8_t>(page_num & 0xFF));
    if (!WriteFrame(frame.data(), frame.size())) return false;
    return WaitAck(frame.data() + frame.size() - 6, 6, timeout_ms_);
}

inline bool MSNDevice::FlashWrite(uint32_t base_addr, const std::vector<uint8_t>& data) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    if (data.empty()) return true;
    const uint16_t pages = static_cast<uint16_t>((data.size() + 255) / 256);
    if (!FlashErase(base_addr, pages)) return false;
    size_t off = 0;
    uint32_t page_idx = 0;
    while (off < data.size()) {
        const size_t n = std::min<size_t>(256, data.size() - off);
        std::vector<uint8_t> page(data.begin() + static_cast<ptrdiff_t>(off),
                                  data.begin() + static_cast<ptrdiff_t>(off + n));
        if (!FlashWritePage(base_addr + page_idx, page, 1, true)) return false;
        off += n;
        ++page_idx;
    }
    return true;
}

inline bool MSNDevice::FlashWriteFile(uint32_t base_addr, const std::string& path) {
    auto bin = LoadFile(path);
    if (bin.empty()) { SetError("找不到文件: " + path); return false; }
    return FlashWrite(base_addr, bin);
}

inline bool MSNDevice::FlashWriteFont(uint32_t base_addr, const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    auto bin = LoadFile(path);
    if (bin.size() <= 6) { SetError("字库文件无效或无法读取: " + path); return false; }
    // 跳过 6 字节文件头 (与 Python Write_Flash_ZK 一致, 不擦除)
    size_t off = 6;
    uint32_t page_idx = 0;
    while (off < bin.size()) {
        const size_t n = std::min<size_t>(256, bin.size() - off);
        std::vector<uint8_t> page(bin.begin() + static_cast<ptrdiff_t>(off),
                                  bin.begin() + static_cast<ptrdiff_t>(off + n));
        if (!FlashWritePage(base_addr + page_idx, page, 1, false)) return false;
        off += n;
        ++page_idx;
    }
    return true;
}

// ---------------- LCD ----------------
inline bool MSNDevice::LcdSetXY(uint16_t x, uint16_t y) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    area_valid_ = false;   // 坐标改变, 显示区域缓存失效
    const uint8_t f[6] = {2, 0, static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(x & 0xFF),
                          static_cast<uint8_t>(y >> 8), static_cast<uint8_t>(y & 0xFF)};
    return WriteFrame(f, 6);
}

inline bool MSNDevice::LcdSetSize(uint16_t w, uint16_t h) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    area_valid_ = false;   // 尺寸改变, 显示区域缓存失效
    const uint8_t f[6] = {2, 1, static_cast<uint8_t>(w >> 8), static_cast<uint8_t>(w & 0xFF),
                          static_cast<uint8_t>(h >> 8), static_cast<uint8_t>(h & 0xFF)};
    return WriteFrame(f, 6);
}

inline bool MSNDevice::LcdSetColor(uint16_t fg, uint16_t bg) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    const uint8_t f[6] = {2, 2, static_cast<uint8_t>(fg >> 8), static_cast<uint8_t>(fg & 0xFF),
                          static_cast<uint8_t>(bg >> 8), static_cast<uint8_t>(bg & 0xFF)};
    return WriteFrame(f, 6);
}

inline bool MSNDevice::LcdBeginArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    if (!LcdSetXY(x, y)) return false;
    if (!LcdSetSize(w, h)) return false;
    const uint8_t f[6] = {2, 3, 7, 0, 0, 0};
    if (!WriteFrame(f, 6)) return false;
    if (!WaitAck(f, 6, timeout_ms_)) return false;
    area_valid_ = true;                      // 区域已同步到设备
    area_x_ = x; area_y_ = y; area_w_ = w; area_h_ = h;
    return true;
}

inline bool MSNDevice::LcdSetState(uint8_t state) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    const uint8_t f[6] = {2, 3, 10, state, 0, 0};
    if (!WriteFrame(f, 6)) return false;
    return WaitAck(f, 6, timeout_ms_);
}

inline bool MSNDevice::LcdShowPhoto(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t page) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    if (!LcdSetXY(x, y)) return false;
    if (!LcdSetSize(w, h)) return false;
    const uint8_t f[6] = {2, 3, 0, static_cast<uint8_t>(page >> 8), static_cast<uint8_t>(page & 0xFF), 0};
    if (!WriteFrame(f, 6)) return false;
    return WaitAck(f, 6, timeout_ms_);
}

inline bool MSNDevice::LcdShowMonoPhoto(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                        uint16_t page, uint16_t fg, uint16_t bg) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    if (!LcdSetXY(x, y)) return false;
    if (!LcdSetSize(w, h)) return false;
    if (!LcdSetColor(fg, bg)) return false;
    const uint8_t f[6] = {2, 3, 1, static_cast<uint8_t>(page >> 8), static_cast<uint8_t>(page & 0xFF), 0};
    if (!WriteFrame(f, 6)) return false;
    return WaitAck(f, 6, timeout_ms_);
}

inline bool MSNDevice::LcdShowMonoMix(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                      uint16_t page, uint16_t fg, uint16_t bg_page) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    if (!LcdSetXY(x, y)) return false;
    if (!LcdSetSize(w, h)) return false;
    if (!LcdSetColor(fg, bg_page)) return false;
    const uint8_t f[6] = {2, 3, 4, static_cast<uint8_t>(page >> 8), static_cast<uint8_t>(page & 0xFF), 0};
    if (!WriteFrame(f, 6)) return false;
    return WaitAck(f, 6, timeout_ms_);
}

inline bool MSNDevice::LcdShowChar32x64(uint16_t x, uint16_t y, char ch, uint16_t fg,
                                        uint16_t bg, uint16_t num_page) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    if (!LcdSetXY(x, y)) return false;
    if (!LcdSetColor(fg, bg)) return false;
    const uint8_t f[6] = {2, 3, 2, static_cast<uint8_t>(ch),
                          static_cast<uint8_t>(num_page >> 8), static_cast<uint8_t>(num_page & 0xFF)};
    if (!WriteFrame(f, 6)) return false;
    return WaitAck(f, 6, timeout_ms_);
}

inline bool MSNDevice::LcdShowChar32x64Mix(uint16_t x, uint16_t y, char ch, uint16_t fg,
                                           uint16_t bg_page, uint16_t num_page) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    if (!LcdSetXY(x, y)) return false;
    if (!LcdSetColor(fg, bg_page)) return false;
    const uint8_t f[6] = {2, 3, 5, static_cast<uint8_t>(ch),
                          static_cast<uint8_t>(num_page >> 8), static_cast<uint8_t>(num_page & 0xFF)};
    if (!WriteFrame(f, 6)) return false;
    return WaitAck(f, 6, timeout_ms_);
}

inline bool MSNDevice::LcdShowGB2312(uint16_t x, uint16_t y, const std::string& text,
                                     uint16_t fg, uint16_t bg) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    if (text.size() < 2) { SetError("GB2312 需要至少 2 字节"); return false; }
    if (!LcdSetXY(x, y)) return false;
    if (!LcdSetColor(fg, bg)) return false;
    const uint8_t f[6] = {2, 3, 3, static_cast<uint8_t>(text[0]),
                          static_cast<uint8_t>(text[1]), 0};
    if (!WriteFrame(f, 6)) return false;
    return WaitAck(f, 6, timeout_ms_);
}

inline bool MSNDevice::LcdShowGB2312Mix(uint16_t x, uint16_t y, const std::string& text,
                                        uint16_t fg, uint16_t bg_page) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    if (text.size() < 2) { SetError("GB2312 需要至少 2 字节"); return false; }
    if (!LcdSetXY(x, y)) return false;
    if (!LcdSetColor(fg, bg_page)) return false;
    const uint8_t f[6] = {2, 3, 6, static_cast<uint8_t>(text[0]),
                          static_cast<uint8_t>(text[1]), 0};
    if (!WriteFrame(f, 6)) return false;
    return WaitAck(f, 6, timeout_ms_);
}

inline bool MSNDevice::LcdFillColor(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    if (!LcdSetXY(x, y)) return false;
    if (!LcdSetSize(w, h)) return false;
    const uint8_t f[6] = {2, 3, 11, static_cast<uint8_t>(color >> 8),
                          static_cast<uint8_t>(color & 0xFF), 0};
    if (!WriteFrame(f, 6)) return false;
    return WaitAck(f, 6, timeout_ms_);
}

// 发送一页(256B)显示数据, size 为实际有效字节数 (不足部分按 0xFF 补齐)
inline bool MSNDevice::LcdSendData(const std::vector<uint8_t>& data, uint16_t size) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    std::vector<uint8_t> frame;
    frame.reserve(64 * 6 + 6);
    uint8_t page[256];
    for (int i = 0; i < 256; ++i)
        page[i] = (i < static_cast<int>(data.size())) ? data[i] : 0xFF;
    for (int i = 0; i < 64; ++i) {
        frame.push_back(4); frame.push_back(static_cast<uint8_t>(i));
        frame.push_back(page[i * 4 + 0]); frame.push_back(page[i * 4 + 1]);
        frame.push_back(page[i * 4 + 2]); frame.push_back(page[i * 4 + 3]);
    }
    frame.push_back(2); frame.push_back(3); frame.push_back(8);
    frame.push_back(static_cast<uint8_t>(size >> 8));
    frame.push_back(static_cast<uint8_t>(size & 0xFF));
    frame.push_back(0);
    return WriteFrame(frame.data(), frame.size());
}

// 直接推送 RGB565 数据 (每个像素 2 字节, 高位在前) 到 LCD, 每帧都会重设显示区域
inline bool MSNDevice::LcdShowImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                    const std::vector<uint8_t>& rgb565, bool compress) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    if (!LcdBeginArea(x, y, w, h)) return false;
    return SendImageData(w, h, rgb565, compress);
}

// 快速推屏: 显示区域与上次一致时跳过 SetXY/SetSize/指令7+ACK, 适合屏幕镜像等高频场景
inline bool MSNDevice::LcdShowImageFast(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                        const std::vector<uint8_t>& rgb565, bool compress) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    if (!area_valid_ || area_x_ != x || area_y_ != y || area_w_ != w || area_h_ != h) {
        if (!LcdBeginArea(x, y, w, h)) return false;
    }
    return SendImageData(w, h, rgb565, compress);
}

// 纯数据发送 (不设置显示区域)
// compress=true  : 主色压缩 (对应 Write_LCD_Screen_fast, 适合截图/大面积同色)
// compress=false : 逐像素发送 (对应 Write_LCD_Screen_fast1)
inline bool MSNDevice::SendImageData(uint16_t w, uint16_t h,
                                     const std::vector<uint8_t>& rgb565, bool compress) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    const size_t total = static_cast<size_t>(w) * h * 2;
    if (total == 0) return true;
    size_t src_off = 0;
    std::vector<uint8_t> frame;
    frame.reserve(total / 256 * 64 * 6 + 512);

    const size_t full_pages = total / 256;
    for (size_t j = 0; j < full_pages; ++j) {
        uint8_t page[256];
        for (int i = 0; i < 256; ++i)
            page[i] = (src_off < rgb565.size()) ? rgb565[src_off++] : 0xFF;

        if (compress) {
            // 64 组 4 字节(2 像素) 取众数作为主色
            uint32_t group[64];
            for (int i = 0; i < 64; ++i)
                group[i] = (static_cast<uint32_t>(page[i * 4 + 0]) << 24) |
                           (static_cast<uint32_t>(page[i * 4 + 1]) << 16) |
                           (static_cast<uint32_t>(page[i * 4 + 2]) << 8) |
                           (static_cast<uint32_t>(page[i * 4 + 3]));
            uint32_t mode = group[0];
            int best = 0;
            for (int i = 0; i < 64; ++i) {
                int c = 0;
                for (int k = 0; k < 64; ++k) if (group[k] == group[i]) ++c;
                if (c > best) { best = c; mode = group[i]; }
            }
            frame.push_back(2); frame.push_back(4);              // 主色指令
            frame.push_back(static_cast<uint8_t>(mode >> 24));
            frame.push_back(static_cast<uint8_t>(mode >> 16));
            frame.push_back(static_cast<uint8_t>(mode >> 8));
            frame.push_back(static_cast<uint8_t>(mode));
            for (int i = 0; i < 64; ++i) {
                if (group[i] == mode) continue;
                frame.push_back(4); frame.push_back(static_cast<uint8_t>(i));
                frame.push_back(page[i * 4 + 0]); frame.push_back(page[i * 4 + 1]);
                frame.push_back(page[i * 4 + 2]); frame.push_back(page[i * 4 + 3]);
            }
        } else {
            for (int i = 0; i < 64; ++i) {
                frame.push_back(4); frame.push_back(static_cast<uint8_t>(i));
                frame.push_back(page[i * 4 + 0]); frame.push_back(page[i * 4 + 1]);
                frame.push_back(page[i * 4 + 2]); frame.push_back(page[i * 4 + 3]);
            }
        }
        frame.push_back(2); frame.push_back(3); frame.push_back(8);
        frame.push_back(1); frame.push_back(0); frame.push_back(0);
    }

    const size_t tail = total % 256;
    if (tail != 0) {
        uint8_t page[256];
        for (size_t i = 0; i < 256; ++i)
            page[i] = (i < tail && src_off < rgb565.size()) ? rgb565[src_off++] : 0xFF;
        for (int i = 0; i < 64; ++i) {
            frame.push_back(4); frame.push_back(static_cast<uint8_t>(i));
            frame.push_back(page[i * 4 + 0]); frame.push_back(page[i * 4 + 1]);
            frame.push_back(page[i * 4 + 2]); frame.push_back(page[i * 4 + 3]);
        }
        frame.push_back(2); frame.push_back(3); frame.push_back(8);
        frame.push_back(static_cast<uint8_t>((tail >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(tail & 0xFF));
        frame.push_back(0);
    }

    if (!compress) {                                            // fast1 结尾刷新
        frame.push_back(2); frame.push_back(3); frame.push_back(9);
        frame.push_back(0); frame.push_back(0); frame.push_back(0);
    }

    return WriteFrame(frame.data(), frame.size());
}

inline bool MSNDevice::LcdShowImageFile(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                        const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(io_mutex_);
    auto bin = LoadFile(path);
    if (bin.empty()) { SetError("找不到文件: " + path); return false; }
    if (!LcdBeginArea(x, y, w, h)) return false;
    size_t off = 0;
    while (bin.size() - off >= 256) {
        std::vector<uint8_t> page(bin.begin() + static_cast<ptrdiff_t>(off),
                                  bin.begin() + static_cast<ptrdiff_t>(off + 256));
        if (!LcdSendData(page, 256)) return false;
        off += 256;
    }
    if (off < bin.size()) {
        std::vector<uint8_t> page(bin.begin() + static_cast<ptrdiff_t>(off), bin.end());
        if (!LcdSendData(page, static_cast<uint16_t>(bin.size() - off))) return false;
    }
    return true;
}

// ---------------- 静态工具 (基于 SDL_Surface) ----------------
inline std::vector<uint8_t> MSNDevice::LoadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    f.seekg(0, std::ios::end);
    const std::streampos sz = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(sz));
    if (sz > 0) f.read(reinterpret_cast<char*>(data.data()), sz);
    return data;
}

// 任意格式 SDL_Surface -> RGB565 字节数组 (每像素 2 字节, 高位在前)
// 像素打包与 Python 一致: [高字节=(r>>3)<<3|g>>5, 低字节=((g&0x1F)>>2)<<5|b>>3]
inline std::vector<uint8_t> MSNDevice::SurfaceToRGB565(SDL_Surface* surface) {
    std::vector<uint8_t> out;
    if (!surface) return out;
    const int w = surface->w, h = surface->h;
    if (w <= 0 || h <= 0) return out;
    out.reserve(static_cast<size_t>(w) * h * 2);
    Uint8 r = 0, g = 0, b = 0, a = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a);
            out.push_back(static_cast<uint8_t>(((r >> 3) << 3) | (g >> 5)));
            out.push_back(static_cast<uint8_t>((((g & 0x1F) >> 2) << 5) | (b >> 3)));
        }
    }
    return out;
}

// RGB565 字节数组 -> SDL_Surface (RGB24), 返回的新 Surface 由调用方 SDL_DestroySurface 释放
inline SDL_Surface* MSNDevice::RGB565ToSurface(const std::vector<uint8_t>& rgb565,
                                               uint16_t w, uint16_t h) {
    if (w == 0 || h == 0) return nullptr;
    SDL_Surface* surf = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGB24);
    if (!surf) return nullptr;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = (static_cast<size_t>(y) * w + x) * 2;
            Uint8 r = 0, g = 0, b = 0;
            if (i + 1 < rgb565.size()) {
                const Uint8 hi = rgb565[i];
                const Uint8 lo = rgb565[i + 1];
                const Uint8 r5 = hi >> 3;
                const Uint8 g6 = static_cast<Uint8>(((hi & 0x07) << 3) | (lo >> 6));
                const Uint8 b5 = lo & 0x1F;
                r = static_cast<Uint8>((r5 << 3) | (r5 >> 2));
                g = static_cast<Uint8>((g6 << 2) | (g6 >> 4));
                b = static_cast<Uint8>((b5 << 3) | (b5 >> 2));
            }
            SDL_WriteSurfacePixel(surf, x, y, r, g, b, 255);
        }
    }
    return surf;
}

// 用 RGBA(0xRRGGBBAA) 颜色整体填充 surface (用于 Fit 模式 letterbox 背景)
static inline bool MSNDevice_FillSurfaceRGBA(SDL_Surface* dst, uint32_t rgba) {
    const SDL_PixelFormatDetails* details = SDL_GetPixelFormatDetails(dst->format);
    if (!details) return false;
    const Uint32 color = SDL_MapRGBA(details, SDL_GetSurfacePalette(dst),
                                     static_cast<Uint8>((rgba >> 24) & 0xFF),
                                     static_cast<Uint8>((rgba >> 16) & 0xFF),
                                     static_cast<Uint8>((rgba >>  8) & 0xFF),
                                     static_cast<Uint8>( rgba        & 0xFF));
    return SDL_FillSurfaceRect(dst, nullptr, color);
}

// 按模式把 surface 缩放到 out_w x out_h, 行为与 Python Writet_Photo_Path 一致
//   Fit     — 等比缩放完整显示, 留白填 bg_color (letterbox)
//   Fill    — 等比缩放铺满, 超出居中裁剪 (cover)
//   Stretch — 直接拉伸到目标尺寸 (可能变形)
// 返回的新 Surface (RGBA32) 由调用方 SDL_DestroySurface 释放
inline SDL_Surface* MSNDevice::ResizeTo(SDL_Surface* surface,
                                        uint16_t out_w, uint16_t out_h,
                                        ImageFitMode mode, uint32_t bg_color) {
    if (!surface || out_w == 0 || out_h == 0) return nullptr;
    const int sw = surface->w, sh = surface->h;
    if (sw <= 0 || sh <= 0) return nullptr;

    SDL_Surface* dst = SDL_CreateSurface(out_w, out_h, SDL_PIXELFORMAT_RGBA32);
    if (!dst) return nullptr;

    const float fw = static_cast<float>(out_w) / sw;
    const float fh = static_cast<float>(out_h) / sh;
    SDL_Rect srcrect = { 0, 0, sw, sh };

    if (mode == ImageFitMode::Stretch) {
        // 拉伸: 整幅直接拉满目标
        if (!SDL_BlitSurfaceScaled(surface, &srcrect, dst, nullptr, SDL_SCALEMODE_LINEAR)) {
            SDL_DestroySurface(dst);
            return nullptr;
        }
        return dst;
    }

    if (mode == ImageFitMode::Fit) {
        // 适应: 取最小比例, 完整显示, 居中, 四周留白填背景色
        const float scale = std::min(fw, fh);
        const int dw = std::max(1, static_cast<int>(sw * scale));
        const int dh = std::max(1, static_cast<int>(sh * scale));
        if (!MSNDevice_FillSurfaceRGBA(dst, bg_color)) {
            SDL_DestroySurface(dst);
            return nullptr;
        }
        SDL_Rect dstrect = { (out_w - dw) / 2, (out_h - dh) / 2, dw, dh };
        if (!SDL_BlitSurfaceScaled(surface, &srcrect, dst, &dstrect, SDL_SCALEMODE_LINEAR)) {
            SDL_DestroySurface(dst);
            return nullptr;
        }
        return dst;
    }

    // 默认 Fill (cover): 取最大比例, 短边对齐目标, 长边居中裁剪
    const float scale = std::max(fw, fh);
    const int srcW = std::min(sw, static_cast<int>(out_w / scale));
    const int srcH = std::min(sh, static_cast<int>(out_h / scale));
    srcrect = { (sw - srcW) / 2, (sh - srcH) / 2, srcW, srcH };

    if (!SDL_BlitSurfaceScaled(surface, &srcrect, dst, nullptr, SDL_SCALEMODE_LINEAR)) {
        SDL_DestroySurface(dst);
        return nullptr;
    }
    return dst;
}

// 等比缩放(覆盖)+居中裁剪到 out_w x out_h (Fill 模式), 兼容旧接口
// 返回的新 Surface (RGBA32) 由调用方 SDL_DestroySurface 释放
inline SDL_Surface* MSNDevice::ResizeCenterCrop(SDL_Surface* surface,
                                                uint16_t out_w, uint16_t out_h) {
    return ResizeTo(surface, out_w, out_h, ImageFitMode::Fill);
}

// 便捷: 按模式缩放裁剪到 160x80 并转 RGB565, 可直接用于 LcdShowImage / FlashWrite
inline std::vector<uint8_t> MSNDevice::ConvertImageToLCD565(SDL_Surface* surface,
                                                            ImageFitMode mode,
                                                            uint32_t bg_color) {
    SDL_Surface* resized = ResizeTo(surface, kLcdWidth, kLcdHeight, mode, bg_color);
    if (!resized) return {};
    std::vector<uint8_t> out = SurfaceToRGB565(resized);
    SDL_DestroySurface(resized);
    return out;
}