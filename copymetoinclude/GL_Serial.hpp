#ifndef __INC_GL_SERIAL_
#define __INC_GL_SERIAL_

// GL_Serial.hpp — 跨平台串口封装类

#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <cstdlib>
#include <stdexcept>

#ifdef _WIN32
#include <Windows.h>
#endif

class SerialDevice {
public:
    
    enum class Parity   { None, Odd, Even, Mark, Space };
    enum class StopBits { One, OnePointFive, Two };
    enum class FlowControl { None, Hardware, Software };

    struct Config {
        uint32_t    baud_rate        = 115200;
        uint8_t     data_bits        = 8;
        Parity      parity           = Parity::None;
        StopBits    stop_bits        = StopBits::One;
        FlowControl flow_control     = FlowControl::None;
        uint32_t    read_timeout_ms  = 500;   // Read 阻塞总超时（0 = 无限等待）
        uint32_t    read_interval_ms = 0;     // 接收间隔超时（0 = 禁用，只按总超时返回）
        uint32_t    write_timeout_ms = 500;
    };

    // ================= 生命周期 =================
    SerialDevice() = default;
    ~SerialDevice();

    SerialDevice(const SerialDevice&)            = delete;   // 句柄不可复制
    SerialDevice& operator=(const SerialDevice&) = delete;
    SerialDevice(SerialDevice&&) noexcept;
    SerialDevice& operator=(SerialDevice&&) noexcept;

    // ================= 打开 / 关闭 =================
    bool Open(const std::string& port_name, uint32_t baud_rate);   // 兼容原始接口
    bool Open(const std::string& port_name, const Config& config); // 完整参数
    void Close();
    bool IsOpen() const;

    bool SetConfig(const Config& config);   // 在已打开的端口上重新应用参数

    // ================= 发送（STL 容器适配） =================
    bool Write(const uint8_t* data, size_t size);
    bool Write(const std::vector<uint8_t>& data);
    bool Write(const std::string& data);
    template <typename Container>   // 任意拥有 .data()/.size() 的连续容器（array/vector 等）
    bool Write(const Container& data) {
        return Write(reinterpret_cast<const uint8_t*>(data.data()),
                     data.size() * sizeof(typename Container::value_type));
    }

    // ================= 接收（STL 容器适配） =================
    size_t Read(uint8_t* buffer, size_t size);            // 阻塞，最多 size 字节（受 read_timeout_ms 限制）
    size_t Read(std::vector<uint8_t>& buffer);            // 阻塞，最多 buffer.size() 字节
    size_t Read(std::string& str, size_t max_bytes);      // 阻塞，读入 std::string
    std::string ReadString(size_t max_bytes);             // 阻塞，返回 std::string
    size_t ReadAvailable(std::vector<uint8_t>& buffer);   // 非阻塞，只读已到达的数据
    size_t BytesAvailable() const;                        // 输入队列中待读字节数

    // ================= 其它操作 =================
    void Flush();               // 等待发送缓冲全部写出
    void ClearRx();             // 丢弃接收缓冲
    void ClearTx();             // 丢弃发送缓冲
    bool SetDTR(bool on);       // 数据终端就绪信号（常用于复位下位机，如 Arduino）
    bool SetRTS(bool on);       // 请求发送信号
    bool SendBreak(uint32_t duration_ms);

    // ================= 工具 =================
    std::string GetLastError() const;
    static std::vector<std::string> ListAvailablePorts();   // 枚举系统中可用的串口

#ifdef _WIN32
    HANDLE GetNativeHandle() const;
#endif

private:
    bool ApplyConfig(const Config& config);

    Config      config_;
    std::string port_name_;
    std::string last_error_;
#ifdef _WIN32
    HANDLE      hSerial_ = INVALID_HANDLE_VALUE;
#endif
};

// ============================================================================
// 全局函数：扫描系统中可用的 COM 串口设备
// 返回 "COM3" 形式的名称列表，按端口号升序排列（COM2 < COM10）
// 等价于 SerialDevice::ListAvailablePorts()
// ============================================================================
std::vector<std::string> ScanAvailableComPorts();

#ifdef GL_SERIAL_IMPLEMENTATION

// ---------------- 内部工具 ----------------
namespace gl_serial_detail {
inline std::string FormatWinError(DWORD code) {
    std::string msg = "Unknown error";
#ifdef _WIN32
    LPSTR buf = nullptr;
    DWORD n = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                             FORMAT_MESSAGE_FROM_SYSTEM |
                             FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, code, 0, (LPSTR)&buf, 0, nullptr);
    if (n > 0 && buf) {
        msg.assign(buf, n);
        LocalFree(buf);
    }
    while (!msg.empty() && (msg.back() == '\r' || msg.back() == '\n'))
        msg.pop_back();
#endif
    return msg;
}

// 枚举系统中可用的 COM 串口（返回 "COM3" 形式，按端口号升序排序）
inline std::vector<std::string> EnumerateComPorts() {
    std::vector<std::string> ports;
#ifdef _WIN32
    wchar_t devices[65536] = {};
    if (QueryDosDeviceW(nullptr, devices, 65536) == 0) return ports;

    const wchar_t* p = devices;
    while (*p) {
        size_t len = wcslen(p);
        if (len >= 4 && wcsncmp(p, L"COM", 3) == 0) {
            bool all_digits = true;
            for (size_t i = 3; i < len; ++i) {
                if (!iswdigit(p[i])) { all_digits = false; break; }
            }
            if (all_digits) {
                std::string name;
                name.reserve(len);
                for (size_t i = 0; i < len; ++i) name.push_back(static_cast<char>(p[i]));
                ports.push_back(name);
            }
        }
        p += len + 1;
    }

    // 按端口号排序：COM2 < COM10
    std::sort(ports.begin(), ports.end(), [](const std::string& a, const std::string& b) {
        auto port_num = [](const std::string& s) {
            size_t i = 0;
            while (i < s.size() && !std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
            return std::atoi(s.c_str() + i);
        };
        return port_num(a) < port_num(b);
    });
#else
    (void)0;
#endif
    return ports;
}
} // namespace gl_serial_detail

// ---------------- 生命周期 ----------------
SerialDevice::~SerialDevice() { Close(); }

SerialDevice::SerialDevice(SerialDevice&& other) noexcept
    : config_(std::move(other.config_))
    , port_name_(std::move(other.port_name_))
    , last_error_(std::move(other.last_error_))
#ifdef _WIN32
    , hSerial_(other.hSerial_)
#endif
{
#ifdef _WIN32
    other.hSerial_ = INVALID_HANDLE_VALUE;
#endif
}

SerialDevice& SerialDevice::operator=(SerialDevice&& other) noexcept {
    if (this != &other) {
        Close();
        config_     = std::move(other.config_);
        port_name_  = std::move(other.port_name_);
        last_error_ = std::move(other.last_error_);
#ifdef _WIN32
        hSerial_       = other.hSerial_;
        other.hSerial_ = INVALID_HANDLE_VALUE;
#endif
    }
    return *this;
}

// ---------------- 打开 / 关闭 ----------------
bool SerialDevice::Open(const std::string& port_name, uint32_t baud_rate) {
    Config cfg;
    cfg.baud_rate = baud_rate;
    return Open(port_name, cfg);
}

bool SerialDevice::Open(const std::string& port_name, const Config& config) {
#ifdef _WIN32
    Close();

    // 构造设备路径："COM3" -> "\\.\COM3"，已带前缀则直接使用
    std::string device;
    if (port_name.rfind("\\\\.\\", 0) == 0) device = port_name;
    else device = "\\\\.\\" + port_name;

    hSerial_ = CreateFileA(device.c_str(),
                           GENERIC_READ | GENERIC_WRITE,
                           0,                     // 独占访问
                           nullptr,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, // 同步 I/O
                           nullptr);
    if (hSerial_ == INVALID_HANDLE_VALUE) {
        last_error_ = "Open " + port_name + " failed: " + gl_serial_detail::FormatWinError(::GetLastError());
        return false;
    }

    port_name_ = port_name;
    if (!ApplyConfig(config)) {
        std::string err = "Configure " + port_name + " failed: " + last_error_;
        Close();                    // Close 会清空 last_error_
        last_error_ = err;
        return false;
    }

    PurgeComm(hSerial_, PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT);
    return true;
#else
    (void)port_name; (void)config;
    last_error_ = "SerialDevice::Open not implemented on this platform";
    return false;
#endif
}

void SerialDevice::Close() {
#ifdef _WIN32
    if (hSerial_ != INVALID_HANDLE_VALUE) {
        CloseHandle(hSerial_);
        hSerial_ = INVALID_HANDLE_VALUE;
    }
    port_name_.clear();
    last_error_.clear();
#endif
}

bool SerialDevice::IsOpen() const {
#ifdef _WIN32
    return hSerial_ != INVALID_HANDLE_VALUE;
#else
    return false;
#endif
}

bool SerialDevice::ApplyConfig(const Config& config) {
#ifdef _WIN32
    if (!IsOpen()) {
        last_error_ = "Port not open";
        return false;
    }

    DCB dcb = {};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(hSerial_, &dcb)) {
        last_error_ = gl_serial_detail::FormatWinError(::GetLastError());
        return false;
    }

    dcb.BaudRate = config.baud_rate;
    dcb.ByteSize = config.data_bits;
    dcb.fBinary  = TRUE;

    switch (config.parity) {
        case Parity::None:  dcb.Parity = NOPARITY;    dcb.fParity = FALSE; break;
        case Parity::Odd:   dcb.Parity = ODDPARITY;   dcb.fParity = TRUE;  break;
        case Parity::Even:  dcb.Parity = EVENPARITY;  dcb.fParity = TRUE;  break;
        case Parity::Mark:  dcb.Parity = MARKPARITY;  dcb.fParity = TRUE;  break;
        case Parity::Space: dcb.Parity = SPACEPARITY; dcb.fParity = TRUE;  break;
    }

    switch (config.stop_bits) {
        case StopBits::One:          dcb.StopBits = ONESTOPBIT;   break;
        case StopBits::OnePointFive: dcb.StopBits = ONE5STOPBITS; break;
        case StopBits::Two:          dcb.StopBits = TWOSTOPBITS;  break;
    }

    switch (config.flow_control) {
        case FlowControl::None:
            dcb.fOutxCtsFlow = FALSE; dcb.fOutxDsrFlow = FALSE;
            dcb.fDtrControl = DTR_CONTROL_DISABLE;
            dcb.fOutX = FALSE; dcb.fInX = FALSE;
            dcb.fRtsControl = RTS_CONTROL_DISABLE;
            break;
        case FlowControl::Hardware:
            dcb.fOutxCtsFlow = TRUE; dcb.fOutxDsrFlow = FALSE;
            dcb.fDtrControl = DTR_CONTROL_HANDSHAKE;
            dcb.fOutX = FALSE; dcb.fInX = FALSE;
            dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
            break;
        case FlowControl::Software:
            dcb.fOutxCtsFlow = FALSE; dcb.fOutxDsrFlow = FALSE;
            dcb.fDtrControl = DTR_CONTROL_ENABLE;
            dcb.fOutX = TRUE; dcb.fInX = TRUE;
            dcb.fRtsControl = RTS_CONTROL_ENABLE;
            break;
    }

    if (!SetCommState(hSerial_, &dcb)) {
        last_error_ = gl_serial_detail::FormatWinError(::GetLastError());
        return false;
    }

    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout         = config.read_interval_ms;
    timeouts.ReadTotalTimeoutMultiplier  = 0;
    timeouts.ReadTotalTimeoutConstant    = config.read_timeout_ms;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant   = config.write_timeout_ms;
    if (!SetCommTimeouts(hSerial_, &timeouts)) {
        last_error_ = gl_serial_detail::FormatWinError(::GetLastError());
        return false;
    }

    SetupComm(hSerial_, 4096, 4096);   // 输入/输出驱动缓冲各 4KB

    config_ = config;
    last_error_.clear();
    return true;
#else
    (void)config;
    return false;
#endif
}

bool SerialDevice::SetConfig(const Config& config) { return ApplyConfig(config); }

// ---------------- 发送 ----------------
bool SerialDevice::Write(const uint8_t* data, size_t size) {
#ifdef _WIN32
    if (!IsOpen() || (size > 0 && data == nullptr)) {
        last_error_ = "Port not open or invalid argument";
        return false;
    }
    size_t total = 0;
    while (total < size) {
        DWORD written = 0;
        DWORD chunk   = static_cast<DWORD>((std::min)(size - total, static_cast<size_t>(0xFFFFFFFFu)));
        if (!WriteFile(hSerial_, data + total, chunk, &written, nullptr)) {
            last_error_ = gl_serial_detail::FormatWinError(::GetLastError());
            return false;
        }
        if (written == 0) {
            last_error_ = "Write timeout";
            return false;
        }
        total += written;
    }
    return true;
#else
    (void)data; (void)size;
    last_error_ = "SerialDevice::Write not implemented on this platform";
    return false;
#endif
}

bool SerialDevice::Write(const std::vector<uint8_t>& data) { return Write(data.data(), data.size()); }

bool SerialDevice::Write(const std::string& data) {
    return Write(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

// ---------------- 接收 ----------------
size_t SerialDevice::Read(uint8_t* buffer, size_t size) {
#ifdef _WIN32
    if (!IsOpen() || buffer == nullptr || size == 0) return 0;
    DWORD bytes_read = 0;
    DWORD to_read    = static_cast<DWORD>((std::min)(size, static_cast<size_t>(0xFFFFFFFFu)));
    if (!ReadFile(hSerial_, buffer, to_read, &bytes_read, nullptr)) {
        last_error_ = gl_serial_detail::FormatWinError(::GetLastError());
        return 0;
    }
    return static_cast<size_t>(bytes_read);
#else
    (void)buffer; (void)size;
    return 0;
#endif
}

size_t SerialDevice::Read(std::vector<uint8_t>& buffer) {
    if (buffer.empty()) return 0;
    return Read(buffer.data(), buffer.size());
}

size_t SerialDevice::Read(std::string& str, size_t max_bytes) {
    if (max_bytes == 0) { str.clear(); return 0; }
    std::string tmp(max_bytes, '\0');
    size_t n = Read(reinterpret_cast<uint8_t*>(tmp.data()), tmp.size());
    str.assign(tmp.data(), n);
    return n;
}

std::string SerialDevice::ReadString(size_t max_bytes) {
    std::string result;
    Read(result, max_bytes);
    return result;
}

size_t SerialDevice::ReadAvailable(std::vector<uint8_t>& buffer) {
#ifdef _WIN32
    if (!IsOpen() || buffer.empty()) return 0;
    COMSTAT cs   = {};
    DWORD   errs = 0;
    if (!ClearCommError(hSerial_, &errs, &cs)) {
        last_error_ = gl_serial_detail::FormatWinError(::GetLastError());
        return 0;
    }
    size_t to_read = (std::min)(buffer.size(), static_cast<size_t>(cs.cbInQue));
    if (to_read == 0) return 0;
    return Read(buffer.data(), to_read);
#else
    return 0;
#endif
}

size_t SerialDevice::BytesAvailable() const {
#ifdef _WIN32
    if (!IsOpen()) return 0;
    COMSTAT cs   = {};
    DWORD   errs = 0;
    if (!ClearCommError(hSerial_, &errs, &cs)) return 0;
    return static_cast<size_t>(cs.cbInQue);
#else
    return 0;
#endif
}

// ---------------- 其它操作 ----------------
void SerialDevice::Flush() {
#ifdef _WIN32
    if (IsOpen()) FlushFileBuffers(hSerial_);
#endif
}

void SerialDevice::ClearRx() {
#ifdef _WIN32
    if (IsOpen()) PurgeComm(hSerial_, PURGE_RXCLEAR | PURGE_RXABORT);
#endif
}

void SerialDevice::ClearTx() {
#ifdef _WIN32
    if (IsOpen()) PurgeComm(hSerial_, PURGE_TXCLEAR | PURGE_TXABORT);
#endif
}

bool SerialDevice::SetDTR(bool on) {
#ifdef _WIN32
    if (!IsOpen()) return false;
    if (!EscapeCommFunction(hSerial_, on ? SETDTR : CLRDTR)) {
        last_error_ = gl_serial_detail::FormatWinError(::GetLastError());
        return false;
    }
    return true;
#else
    (void)on;
    return false;
#endif
}

bool SerialDevice::SetRTS(bool on) {
#ifdef _WIN32
    if (!IsOpen()) return false;
    if (!EscapeCommFunction(hSerial_, on ? SETRTS : CLRRTS)) {
        last_error_ = gl_serial_detail::FormatWinError(::GetLastError());
        return false;
    }
    return true;
#else
    (void)on;
    return false;
#endif
}

bool SerialDevice::SendBreak(uint32_t duration_ms) {
#ifdef _WIN32
    if (!IsOpen()) return false;
    if (!SetCommBreak(hSerial_)) {
        last_error_ = gl_serial_detail::FormatWinError(::GetLastError());
        return false;
    }
    Sleep(duration_ms);        // 保持 break 电平（同步实现）
    ClearCommBreak(hSerial_);
    return true;
#else
    (void)duration_ms;
    return false;
#endif
}

// ---------------- 工具 ----------------
std::string SerialDevice::GetLastError() const { return last_error_; }

#ifdef _WIN32
HANDLE SerialDevice::GetNativeHandle() const { return hSerial_; }
#endif

std::vector<std::string> SerialDevice::ListAvailablePorts() {
    return gl_serial_detail::EnumerateComPorts();
}

// 全局函数：扫描系统中可用的 COM 串口设备
std::vector<std::string> ScanAvailableComPorts() {
    return gl_serial_detail::EnumerateComPorts();
}

#endif // GL_SERIAL_IMPLEMENTATION

#endif //__INC_GL_SERIAL_