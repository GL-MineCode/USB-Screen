# USB Screen — MSU2-MINI USB 屏幕上位机

基于 **C++20 / SDL3 / Fgui** 编写的 MSU2-MINI（MSN）USB 小屏幕上位机程序，通过串口把画面实时推送到 160×80 的小屏幕上。

> 本工程由逆向恢复的 `MSU2_MINI_DemoV1.6`（Python 3.11 + PyQt6）移植/重构而来，通信协议与原始 Demo 保持一致。原逆向文件 `MSU2_MINI_DemoV1.6_Output_recovered.py` 保留在仓库中作为协议参考。

---

## 功能特性

- **录屏推送**：实时抓取桌面，双线程“乒乓双缓冲”截图 → 压缩 → 串口推屏，让 19200 波特率的串口尽量保持满载，提高刷新率。
- **性能监测**：实时显示 CPU / 内存 / 各 GPU 使用率（PDH + DXGI + NVML + WMI 底层查询，无需管理员权限）。
- **时钟**：显示当前时间（`hh:mm:ss`），可设置背景图片。
- **媒体**：可设置背景图片的显示页（预留）。
- **关闭推送**：停止推屏。
- **背景图片选择**：在“时钟 / 媒体”页通过系统文件对话框（`GL_Commdlg`）选择背景图，路径持久化到 `runtime_workdir/config.json`（`GL_JSON3`）。
- 系统托盘图标 + 控制窗口（SDL Tray）。

| 页面序号 | 名称     | 说明                                   |
| :------: | :------- | :------------------------------------- |
| 1        | 录屏推送 | 桌面镜像推屏                           |
| 2        | 性能监测 | CPU / 内存 / GPU 占用监控              |
| 3        | 时钟     | 时间显示（可设背景）                   |
| 4        | 媒体     | 背景图页（可设背景）                   |
| 5        | 关闭推送 | 停止推屏                               |

---

## 目录结构

```
.
├─ CMakeLists.txt / CMakePresets.json   # 构建脚本 (MinGW + Ninja, Debug/Release)
├─ include/                             # 本工程头文件
│   ├─ USBScreen.hpp                    # MSNDevice: 串口协议封装 + SDL 图片工具 (核心)
│   └─ PerformanceQuery.hpp             # CPU/内存/GPU 性能查询接口
├─ src/
│   ├─ main.cpp                         # 入口 + GUI(Fgui) + 推屏逻辑
│   └─ PerformanceQuery.cpp             # 性能查询实现
├─ tools/
│   ├─ perf_test.cpp                    # 性能查询独立验证工具
│   └─ cpu_verify.cpp                   # CPU 占用率验证工具
├─ resource/
│   └─ icon.rc / icon.ico               # 程序图标
├─ runtime_workdir/                     # 运行目录 (cwd)
│   ├─ main.exe / SDL3*.dll             # 运行所需 DLL
│   ├─ *.ttf                            # 字体
│   ├─ icon.png                         # 托盘图标
│   └─ config.json                      # 背景配置(自动读写)
├─ MSU2_MINI_DemoV1.6_Output_recovered.py  # 逆向参考(Python)
└─ bin/{debug,release}/main.exe         # 构建产物
```

---

## 环境与依赖

- 编译器：MinGW-w64 **g++**（C++20），生成器 **Ninja**。
- 依赖库（SDK 目录统一放在 `D:/AppInstallers/RecentlyC++Programs`，如需调整请改 `CMakeLists.txt` 里的 `SDL_KIT_DIR`）：
  - **SDL3** `3.4.14`、**SDL3_image** `3.4.4`、**SDL3_ttf** `3.2.2`
  - **Fgui**（GUI 控件库）及 `GL_*` 系列头文件：`GL_Serial`（串口）、`GL_Commdlg`（系统文件对话框）、`GL_JSON3`（JSON）、`GL_DateTime`、`FontEx` 等
  - Windows 系统库：`comdlg32`、`shell32`、`comctl32`、`ole32`、`pdh`、`psapi`、`wbemuuid`、`dxgi` 等

---

## 构建与运行

```bash
# 配置并构建 (Debug)
cmake --preset debug
cmake --build build/debug

# 或 Release
cmake --preset release
cmake --build build/release
```

产物输出到 `bin/debug/main.exe` / `bin/release/main.exe`。

**运行**：程序把工作目录当作资源目录，请把 `runtime_workdir` 设为当前目录后再启动，或在其中放置 `main.exe` 及配套 DLL：

```bash
cd runtime_workdir
../bin/debug/main.exe
```

> 使用 VS Code 时可直接按 F5（`launch.json` 已设置 `cwd=runtime_workdir`，自动先执行 CMake 构建）。

首次连接：插入屏幕后，在上位机“主页”的串口下拉框选择对应 COM 口，或等待程序自动扫描连接（`ConnectAny`）。

---

## 配置说明

背景图片等设置自动保存到运行目录下的 `config.json`（使用 `GL_JSON3` 读写）：

```json
{
    "background": {
        "clock": "C:\\path\\to\\clock.png",
        "media": "C:\\path\\to\\media.jpg"
    }
}
```

- 值为图片**完整路径**，空字符串表示“未设置”（默认）。
- 在 GUI“时钟/媒体”页点击“选择背景图片...”即更新并写回该文件；点击“清除背景”清空。
- 程序启动时自动读取恢复。图片按 **cover（等比铺满居中裁剪）** 适配 160×80。

---

## 通信协议（简要）

串口参数：**19200-8N1**。所有命令均为 6 字节帧 `[头, 命令/子命令, D2, D3, D4, D5]`：

| 帧头 | 用途 |
| :--: | :--- |
| `0x00` | 寄存器（SFR）读写 / 数据表 |
| `0x02` | LCD 显示指令（图片/字库/区域/填充等） |
| `0x03` | Flash 操作 |
| `0x04` | 数据写入（每次 4 字节，Flash/LCD 像素数据） |
| `0x08` | ADC 读取 |

完整协议与调用示例见 `include/USBScreen.hpp` 顶部注释。
