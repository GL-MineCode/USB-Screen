#include <cstdio>
#include <thread>
#include <filesystem>
#include <mutex>
#include <cctype>
#include <atomic>
#include <string>
#include <exception>
#include <memory>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include "decode.hpp"
#include "GL_Log.hpp"
#include "Times.hpp"
#include "GL_DateTime.hpp"
#include "Fgui/Fgui.hpp"
#define SDL_EVENT_DISPATCHER_IMPLEMENTATION
#include "SDL_EventDispatcher.hpp"
#define SDL_WINDOWIM_IMPLEMENTATION
#include "SDL_WindowIM.hpp"

#include "BuildVersion.hpp"

#include "USBScreen.hpp"

#include "WindowsDCtoSDLSurface.hpp"

#include "PerformanceQuery.hpp"

#include "GL_Commdlg.hpp"
#include "GL_JSON3.hpp"
#include <shellapi.h>

namespace{
    // 取路径中的文件名部分, 用于界面简短展示背景文件
    std::string _PathBaseName(const std::string& path){
        size_t p = path.find_last_of("/\\");
        return (p == std::string::npos) ? path : path.substr(p + 1);
    }

    // 用系统默认浏览器打开链接(供"设置"页作者主页等使用)
    void _OpenUrl(const std::string& url){
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    // 配置文件(与字体/图标一样放在工作目录 runtime_workdir), 使用 GL_JSON3 读写
    const std::string kConfigPath = "config.json";

    // 从 config.json 读取两页背景路径(文件缺失/损坏时保持原值)
    void _LoadBgPathsFromConfig(std::string& clock_bg, std::string& media_bg){
        try{
            GL_JSON3::JSONNode root = GL_JSON3::JSONNode::load_file(kConfigPath);
            if(!root.is_object() || !root.contains("background")) return;
            GL_JSON3::JSONNode& bg = root["background"];
            if(!bg.is_object()) return;

            // 逐键读取, 仅接受字符串类型, 其它情况忽略避免拖垮整个加载
            auto readStr = [](const GL_JSON3::JSONNode& obj, const char* key) -> std::string{
                try{
                    if(obj.contains(key) && obj[key].is_string())
                        return obj[key].get<std::string>();
                }
                catch(...){}
                return {};
            };
            clock_bg = readStr(bg, "clock");
            media_bg = readStr(bg, "media");
        }
        catch(const std::exception& e){
            printf("读取配置(config.json)失败: %s\n", e.what());
        }
    }

    // 把两页背景路径写入 config.json
    void _SaveBgPathsToConfig(const std::string& clock_bg, const std::string& media_bg){
        try{
            GL_JSON3::JSONNode root{
                {"background", GL_JSON3::JSONNode{{"clock", clock_bg}, {"media", media_bg}}}
            };
            root.save_file(kConfigPath, 4);
        }
        catch(const std::exception& e){
            printf("保存配置(config.json)失败: %s\n", e.what());
        }
    }
}

WindowCapturer wc(GetDesktopWindow());
PerformanceQuery performance_monitor;
FPSCounter push_fps;
FPSLimiter update_fps_limiter(60,true);

// 双线程乒乓缓冲推屏 (对齐原脚本 Screen_Date_get + show_PC_Screen):
//   capture_thread 截图线程   : 持续截图→压缩→填入空闲缓冲, 与发送并行
//   thread 发送/连接线程      : 有帧立刻发送(快速推屏, 区域未变跳过重设), 串口不空闲
class DeviceLoop{
    bool is_running;
    std::unique_ptr<std::thread> thread;          // 发送/连接线程
    std::unique_ptr<std::thread> capture_thread;  // 截图线程

    // 乒乓双缓冲 (对应原脚本 G_screnn0/G_screnn1)
    std::mutex buf_mtx;
    std::vector<uint8_t> bufs[2];   // 两帧压缩好的 RGB565 数据
    bool ready[2] = {false, false}; // 缓冲是否已填好待发送
    int  send_idx = 0;              // 发送线程当前消费的缓冲

    // ---- 背景图片(时钟页 page==2 / 媒体页 page==3) ----
    std::mutex bg_mtx;              // 保护路径字段: GUI线程写 / 截图线程读
    std::string clock_bg_path;      // 时钟页背景图片路径, 空表示不设置
    std::string media_bg_path;      // 媒体页背景图片路径, 空表示不设置

    // 已加载的背景表面(160x80, RGBA32), 仅在截图线程内访问
    SDL_Surface* clock_bg_surf   = nullptr;
    std::string  clock_bg_loaded; // 与 clock_bg_surf 对应的路径
    SDL_Surface* media_bg_surf   = nullptr;
    std::string  media_bg_loaded; // 与 media_bg_surf 对应的路径

    // 截图线程专用: 按需加载/刷新某页(page==2/3)的背景, 返回可直接铺到页面表面的表面
    SDL_Surface* LoadPageBackground(int page){
        const bool is_media = (page == 3);
        std::string path;
        {
            std::lock_guard<std::mutex> lk(bg_mtx);
            path = is_media ? media_bg_path : clock_bg_path;
        }
        SDL_Surface*& cached = is_media ? media_bg_surf : clock_bg_surf;
        std::string&  loaded = is_media ? media_bg_loaded : clock_bg_loaded;
        if(cached && loaded == path) return cached;   // 路径未变, 复用缓存
        if(cached){ SDL_DestroySurface(cached); cached = nullptr; }
        loaded = path;
        if(path.empty()) return nullptr;              // 未设置背景
        SDL_Surface* img = IMG_Load(path.c_str());
        if(!img){
            printf("背景图片加载失败: %s\n", path.c_str());
            return nullptr;
        }
        cached = MSNDevice::ResizeTo(img, MSNDevice::kLcdWidth, MSNDevice::kLcdHeight,
                                     MSNDevice::ImageFitMode::Fill, 0x000000FF);
        SDL_DestroySurface(img);
        return cached;
    }

    FontEx font;

public:

    MSNDevice device;

    // 背景图片路径访问(线程安全); 修改后立即写回 config.json 持久化
    void SetClockBackground(const std::string& path){
        { std::lock_guard<std::mutex> lk(bg_mtx); clock_bg_path = path; }
        SaveBgConfigToFile();
    }
    void SetMediaBackground(const std::string& path){
        { std::lock_guard<std::mutex> lk(bg_mtx); media_bg_path = path; }
        SaveBgConfigToFile();
    }
    std::string ClockBackgroundPath(){ std::lock_guard<std::mutex> lk(bg_mtx); return clock_bg_path; }
    std::string MediaBackgroundPath(){ std::lock_guard<std::mutex> lk(bg_mtx); return media_bg_path; }

    // 启动时从 config.json 恢复两页背景路径
    void LoadBgConfigFromFile(){
        std::string c, m;
        _LoadBgPathsFromConfig(c, m);
        { std::lock_guard<std::mutex> lk(bg_mtx); clock_bg_path = c; media_bg_path = m; }
    }
    // 把两页背景路径写入 config.json
    void SaveBgConfigToFile(){
        std::string c, m;
        { std::lock_guard<std::mutex> lk(bg_mtx); c = clock_bg_path; m = media_bg_path; }
        _SaveBgPathsToConfig(c, m);
    }

    int page = 0;
    const int PAGE_MIN = 0;
    const int PAGE_MAX = 4;

    std::mutex preview_buffer_mtx;
    SDL_Surface* preview_buffer = nullptr;

    void CaptureLoop(){
        while(is_running){
            if(!device.IsConnected()){
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            int slot = -1;
            {
                std::lock_guard<std::mutex> lk(buf_mtx);
                for(int i = 0; i < 2; ++i)
                    if(!ready[i]) { slot = i; break; }
            }
            if(slot < 0){                            // 两个缓冲都满, 等发送线程消费
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            SDL_Surface *surface = ActionOnUpdate();
            if (surface)
            {
                {
                    std::lock_guard<std::mutex> lk(buf_mtx);
                    bufs[slot] = device.ConvertImageToLCD565(surface, MSNDevice::ImageFitMode::Fit, 0x00000000);
                    ready[slot] = true;
                    {
                        std::lock_guard<std::mutex> lk(preview_buffer_mtx);
                        if(preview_buffer){
                            SDL_DestroySurface(preview_buffer);
                        }
                        preview_buffer = MSNDevice::RGB565ToSurface(bufs[slot],MSNDevice::kLcdWidth,MSNDevice::kLcdHeight);
                    }
                }
                SDL_DestroySurface(surface);
            }
        }
    }

    struct PerformanceItem{
        std::string label;
        float value;
    };

    void DrawLayout(SDL_Surface* surface, const std::vector<PerformanceItem>& items){
        if(!surface) return;
        int line_count = static_cast<int>(items.size() / 2);
        int line_height = surface->h / line_count;
        // 性能页配色(黑底, 柔和莫兰迪风): 翡翠绿/天蓝/琥珀橙/藕紫
        SDL_Color color_table[] = {
            { 74, 222, 128, 255},  // CPU   — #4ADE80 翡翠绿
            { 56, 189, 248, 255},  // MEM   — #38BDF8 天蓝
            {251, 146,  60, 255},  // GPU0  — #FB923C 琥珀橙
            {232, 121, 249, 255},  // GPU1  — #E879F9 藕紫粉
        };
        size_t index = 0;
        for(auto& item : items){
            int line_index = index / 2;
            int col_index = index % 2;
            int ptsize = font.PtsizeFromHeight(surface->h / line_count);
            std::string num = std::format("{:3.0f}", item.value);
            int w,h;
            font.SizeText(ptsize,num,&w,&h);
            int x = (col_index+1) * (surface->w / 2) - w;
            int y = line_index * line_height;
            int rw = (surface->w / 2) - w;
            font.drawTextOnSurface(surface, x, y, ptsize, num, color_table[index % std::size(color_table)],{0.0f,0.0f});
            std::string label = item.label;
            font.drawTextOnSurface(surface, col_index * (surface->w / 2), y + h, font.PtsizeFromWidth(rw,label), label, color_table[index % std::size(color_table)],{0.0f,1.0f});
            index++;
        }
    }

    SDL_Surface* ActionOnUpdate(){
        if(page == 0){
            wc.ReadPixel();
            return wc.GetSurface();
        }
        else if(page == 1){
            SDL_Surface* sur = SDL_CreateSurface(MSNDevice::kLcdWidth,MSNDevice::kLcdHeight,SDL_PIXELFORMAT_XRGB8888);
            if(!sur){
                return nullptr;
            }
            SDL_FillSurfaceRect(sur, nullptr, SDL_MapSurfaceRGBA(sur, 0, 0, 0, 255));

            PerformanceQuery::CpuUsage cpu_usage = performance_monitor.GetCpuUsage();
            PerformanceQuery::MemUsage mem_usage;
            performance_monitor.GetSystemMemoryUsage(mem_usage);
            std::vector<PerformanceQuery::GpuUsage> gpu_usage;
            gpu_usage = performance_monitor.GetGpuUsage();

            std::vector<PerformanceItem> items;
            items.push_back({"CPU", static_cast<float>(cpu_usage.Usage*100.0f)});
            items.push_back({"MEM", static_cast<float>(mem_usage.Usage*100.0f)});
            size_t gpu_index = 0;
            for(const auto& gpu : gpu_usage){
                float v = static_cast<float>(gpu.Usage*100.0f);
                if(std::isnan(v)) v = 0;
                items.push_back({std::format("GPU{}", gpu_index),v});
                gpu_index++;
            }
            DrawLayout(sur, items);
            return sur;
        }
        else if(page == 2){
            SDL_Surface* sur = SDL_CreateSurface(MSNDevice::kLcdWidth,MSNDevice::kLcdHeight,SDL_PIXELFORMAT_XRGB8888);
            if(!sur){
                return nullptr;
            }
            SDL_FillSurfaceRect(sur, nullptr, SDL_MapSurfaceRGBA(sur, 0, 0, 0, 255));
            SDL_Surface* bg = LoadPageBackground(page);   // 可选: 时钟页背景
            if(bg){
                SDL_BlitSurface(bg, nullptr, sur, nullptr);
            }
            std::string text = DateTime::Now().ToString("hh:mm:ss");
            int ps = font.PtsizeFromWidth(sur->w,text);
            int cx = sur->w / 2, cy = sur->h / 2;
            if(bg){
                // 有背景时给文字加一圈黑描边保证可读性
                for(int dx = -1; dx <= 1; ++dx){
                    for(int dy = -1; dy <= 1; ++dy){
                        if(dx == 0 && dy == 0) continue;
                        font.drawTextOnSurface(sur, cx + dx, cy + dy, ps, text, {0,0,0,255},{0.5f,0.5f});
                    }
                }
            }
            font.drawTextOnSurface(sur, cx, cy, ps, text, {255,255,255,255},{0.5f,0.5f});
            return sur;
        }
        else if(page == 3){
            SDL_Surface* sur = SDL_CreateSurface(MSNDevice::kLcdWidth,MSNDevice::kLcdHeight,SDL_PIXELFORMAT_XRGB8888);
            if(!sur){
                return nullptr;
            }
            SDL_FillSurfaceRect(sur, nullptr, SDL_MapSurfaceRGBA(sur, 0, 0, 0, 255));
            SDL_Surface* bg = LoadPageBackground(page);   // 可选: 媒体页背景
            if(bg){
                SDL_BlitSurface(bg, nullptr, sur, nullptr);
            }
            return sur;
        }
        return nullptr;
    }

    static void ThreadEntry(DeviceLoop* self){
        while(self->is_running){
            if(self->device.IsConnected()){
                std::vector<uint8_t> frame;
                {
                    std::lock_guard<std::mutex> lk(self->buf_mtx);
                    if(self->ready[self->send_idx]){
                        frame = std::move(self->bufs[self->send_idx]);
                        self->ready[self->send_idx] = false;
                        self->send_idx = 1 - self->send_idx;   // 交替消费双缓冲
                    }
                }
                if(!frame.empty()){
                    // 快速推屏: 显示区域未变时跳过重设+ACK, 串口持续被占用
                    self->device.LcdShowImageFast(0, 0, 160, 80, frame, true);
                    push_fps.Update();
                }
                else{
                    std::this_thread::sleep_for(std::chrono::milliseconds(1)); // 无帧才等
                }
                update_fps_limiter.Delay();
            }
            else{
                if(self->device.ConnectAny(1000)){
                    printf(std::format("连接[{}]成功!\n设备版本:{}",self->device.Port(),self->device.Version()).c_str());
                }
                else{
                    //printf("无设备...\n");
                    SDL_Delay(100);
                }
            }
        }
    }

    DeviceLoop(){
        is_running = true;
        font.OpenFonts({
            //"SarasaMonoSC-SemiBold.ttf",
            "JetBrainsMono-Medium.ttf",
            "SourceHanSansCN-Medium.ttf"
        });
    }

    void Startup(){
        if(thread) return;
        LoadBgConfigFromFile();   // 启动时恢复 config.json 中的背景配置
        is_running = true;
        thread = std::make_unique<std::thread>(DeviceLoop::ThreadEntry, this);
        capture_thread = std::make_unique<std::thread>(&DeviceLoop::CaptureLoop, this);
    }
    void Shutdown(){
        is_running = false;
        if(thread) thread->join();
        if(capture_thread) capture_thread->join();
        thread.reset();
        capture_thread.reset();
        // 释放截图线程缓存的背景表面(线程已 join, 无并发访问)
        if(clock_bg_surf){ SDL_DestroySurface(clock_bg_surf); clock_bg_surf = nullptr; }
        clock_bg_loaded.clear();
        if(media_bg_surf){ SDL_DestroySurface(media_bg_surf); media_bg_surf = nullptr; }
        media_bg_loaded.clear();
        device.Disconnect();
        {
            std::lock_guard<std::mutex> lk(preview_buffer_mtx);
            if(preview_buffer){
                SDL_DestroySurface(preview_buffer);
            }
            preview_buffer = nullptr;
        }
    }
    ~DeviceLoop(){
        Shutdown();
    }
} device_loop;

SDL_EventDispatcher eventDispatcher;

bool g_running = true;

// 利用C++的RAII机制自动初始化和销毁SDL和TTF
class SDLEnv
{
public:
    SDLEnv()
    {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        {
            throw std::runtime_error("SDL_Init failed: " + std::string(SDL_GetError()));
        }
        if (!TTF_Init())
        {
            // 因为析构函数不会被调用，所以这里需要手动调用Quit函数
            SDL_Quit();
            throw std::runtime_error("TTF_Init failed: " + std::string(SDL_GetError()));
        }
    }
    ~SDLEnv()
    {
        TTF_Quit();
        SDL_Quit();
    }
};

const int GeneralPadding = 5;

class BufferPreview : public Fgui_Control{
public:
    BufferPreview(const SDL_Rect& rect){
        default_rect = rect;
        this->InvalidateRect();
    };
    void ActionOnPaint(SDL_Renderer* renderer,const SDL_Rect& dirty_region,const SDL_Rect& relative_rect) override{
        SDL_Rect draw_area = this->default_rect;
        draw_area.x = relative_rect.x;
        draw_area.y = relative_rect.y;
        SDL_Texture* texture = NULL;
        {
            std::lock_guard<std::mutex> lk(device_loop.preview_buffer_mtx);
            if(device_loop.preview_buffer){
                texture = SDL_CreateTextureFromSurface(renderer,device_loop.preview_buffer);
            }
        }
        if(!texture) return;
        SDL_FRect dst_rect = {draw_area.x,draw_area.y,draw_area.w,draw_area.h};
        SDL_SetTextureScaleMode(texture,SDL_SCALEMODE_NEAREST);
        SDL_RenderTexture(renderer,texture,NULL,&dst_rect);
        SDL_DestroyTexture(texture);
    }

	void ActionOnEvent(const SDL_Event* event,const SDL_Rect& relative_rect) override{
        
    }

	void ActionOnTimer(const SDL_Rect& relative_rect) override{
        this->InvalidateRect();
    }

    std::string ActionOnGetTypeName() const override{
        return "BufferPreview";
    }
};

class MainWindowTopMenuInterface : public ControlBox{
    FontEx* font;
    SDL_Window* window;

    std::shared_ptr<TabControl> tab_control;

    SDL_Rect client_rect;

    std::vector<std::shared_ptr<ControlBox>> tab_items;

    Uint64 last_data_update_time = 0;

    // 背景图片选择分组(仅"时钟/媒体"两页显示)
    std::shared_ptr<StaticText> bg_title;      // "时钟页背景:" / "媒体页背景:"
    std::shared_ptr<StaticText> bg_path_text;  // 当前已选背景文件名
    std::shared_ptr<PushButton> bg_pick_btn;   // "选择背景图片..."
    std::shared_ptr<PushButton> bg_clear_btn;  // "清除背景"
public:

    MainWindowTopMenuInterface(FontEx* font, SDL_Rect window_rect, SDL_Window* window)
        : ControlBox(window_rect,window_rect.w,window_rect.h), font(font), window(window) {
        this->SetShowScrollBars(false);
        SDL_Rect temp = {0,0,window_rect.w,window_rect.h};
        client_rect = SetRectPadding(temp,GeneralPadding*2);
        tab_control = std::make_shared<TabControl>(TAB_FLAG_POS_TOP,font,24,client_rect);
        tab_control->SetDrawControlBoxBackground(false);
        PushbackControl("tab", tab_control);

        tab_items.push_back(tab_control->PushbackTab(TabControl::TabItem{"主页","home",
        std::make_shared<ControlBox>(SDL_Rect{0,0,client_rect.w,client_rect.h},-1,-1)
        }).content);
        //临时设计，FIX ME LATER!
        {
            auto& tab_item = *(*(tab_items.end()-1));
            static const std::string page_name[] = {
                "录屏推送",
                "性能监测",
                "时钟",
                "媒体",
                "关闭推送"
            };
            auto mode_text = tab_item.PushbackControl("mode_text", std::make_shared<StaticText>("模式",font,24,SDL_Rect{20,190}));
            mode_text->SetText(std::format("模式[{}]: {}",device_loop.page+1,page_name[device_loop.page]));

            auto btn1 = tab_item.PushbackControl("pageup", std::make_shared<PushButton>("↑上翻页",font,24,SDL_Rect{10,10}));
            btn1->actions.OnClick = [this,mode_text](PushButton*){
                if(device_loop.page <= device_loop.PAGE_MIN){
                    device_loop.page = device_loop.PAGE_MAX;
                }
                else{
                    device_loop.page--;
                }
                mode_text->SetText(std::format("模式[{}]: {}",device_loop.page+1,page_name[device_loop.page]));
            };
            auto btn2 = tab_item.PushbackControl("pagedown",std::make_shared<PushButton>("↓下翻页",font,24,SDL_Rect{10,70}));
            btn2->actions.OnClick = [this,mode_text](PushButton*){
                if(device_loop.page >= device_loop.PAGE_MAX){
                    device_loop.page = device_loop.PAGE_MIN;
                }
                else{
                    device_loop.page++;
                }
                mode_text->SetText(std::format("模式[{}]: {}",device_loop.page+1,page_name[device_loop.page]));
            };

            auto port_select = tab_item.PushbackControl("port_select", std::make_shared<ComboBox>("未连接",std::vector<ComboBox::ComboItem>{
                
            },-1,font,24,SDL_Rect{10,130}));
            port_select->actions.OnChange = [this](ComboBox* cb,const ComboBox::ComboItem& item){
                device_loop.device.Connect(item.ui_string,1000);
            };

            auto preview = tab_item.PushbackControl("preview", std::make_shared<BufferPreview>(SDL_Rect{120,10,MSNDevice::kLcdWidth*2,MSNDevice::kLcdHeight*2}));

            auto fps_text = tab_item.PushbackControl("fps_text", std::make_shared<StaticText>("帧数",font,16,SDL_Rect{320,180}));

            auto fps_select = tab_item.PushbackControl("fps_select", std::make_shared<ComboBox>("-",std::vector<ComboBox::ComboItem>{
                {"1",1,0},
                {"5",5,0},
                {"10",10,0},
                {"30",30,0},
                {"60",60,0},
                {"90",90,0},
                {"120",120,0},
                {"无限制",999,0}
            },999,font,16,SDL_Rect{320,210}));
            fps_select->actions.OnChange = [this](ComboBox* cb,const ComboBox::ComboItem& item){
                if(item.id == 999){
                    update_fps_limiter.Disable();
                }
                else if(item.id == 0xFFFFFFFF){
                    return;
                }
                else{
                    update_fps_limiter.Enable();
                    update_fps_limiter.Set(item.id);
                }
            };

            // ---- 背景图片选择(仅在"时钟/媒体"两页显示) ----
            bg_title = tab_item.PushbackControl("bg_title", std::make_shared<StaticText>("背景",font,16,SDL_Rect{20,250}));
            bg_pick_btn = tab_item.PushbackControl("bg_pick_btn", std::make_shared<PushButton>("选择背景图片...",font,16,SDL_Rect{20,280}));
            bg_clear_btn = tab_item.PushbackControl("bg_clear_btn", std::make_shared<PushButton>("清除背景",font,16,SDL_Rect{210,280}));
            bg_path_text = tab_item.PushbackControl("bg_path_text", std::make_shared<StaticText>("未设置",font,16,SDL_Rect{20,315}));
            bg_pick_btn->actions.OnClick = [this](PushButton*){
                int page = device_loop.page;
                const char* page_label = (page == 2) ? "时钟" : (page == 3) ? "媒体" : nullptr;
                if(!page_label) return;

                const std::vector<std::string> filters = {
                    "图片文件(*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp)|*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp",
                    "所有文件(*.*)|*.*"
                };
                std::string path;
                try{
                    // GL_Commdlg 的文件打开对话框: 选中返回 UTF-8 路径, 取消返回空串
                    path = GLDLG::getOpenFileName(filters, std::format("选择{}页背景图片", page_label));
                }
                catch(const std::exception& e){
                    printf("打开文件对话框失败: %s\n", e.what());
                    return;
                }
                if(path.empty()) return;   // 用户取消

                if(page == 2)      device_loop.SetClockBackground(path);
                else if(page == 3) device_loop.SetMediaBackground(path);
                SyncBackgroundUI();        // 立即刷新已选文件名显示
            };
            bg_clear_btn->actions.OnClick = [this](PushButton*){
                int page = device_loop.page;
                if(page == 2)      device_loop.SetClockBackground("");
                else if(page == 3) device_loop.SetMediaBackground("");
                SyncBackgroundUI();
            };

        }

        tab_items.push_back(tab_control->PushbackTab(TabControl::TabItem{"关于","about",
        std::make_shared<ControlBox>(SDL_Rect{0,0,client_rect.w,client_rect.h},-1,-1)
        }).content);

        // ---- 设置页内容: 软件信息 / 作者信息 ----
        {
            auto& settings_item = *(*(tab_items.end()-1));

            // 软件信息(版本号来自 BuildVersion.hpp 的 BUILD_VERSION)
            settings_item.PushbackControl("about_name", std::make_shared<StaticText>("USB 屏幕上位机程序",font,26,SDL_Rect{20,30}));
            settings_item.PushbackControl("about_version", std::make_shared<StaticText>(std::format("版本: {}",BUILD_VERSION),font,16,SDL_Rect{20,85}));
            settings_item.PushbackControl("about_framework", std::make_shared<StaticText>("基于 SDL3 与 Fgui 开发",font,16,SDL_Rect{20,125}));

            // 作者信息(GitHub 链接可点击, 用系统浏览器打开)
            settings_item.PushbackControl("author_text", std::make_shared<StaticText>("作者: GL",font,16,SDL_Rect{20,190}));
            auto author_link = settings_item.PushbackControl("author_link", std::make_shared<LinkText>("https://github.com/GL-MineCode",font,16,SDL_Rect{20,225}));
            author_link->actions.OnClick = [](LinkText*){
                _OpenUrl("https://github.com/GL-MineCode");
            };
        }


        tab_control->ActivateTab("home");
        SyncBackgroundUI();
    }

    // 依据当前设备页(2=时钟,3=媒体)显示/隐藏背景选择分组, 并刷新其文案
    void SyncBackgroundUI(){
        bool relevant = (device_loop.page == 2 || device_loop.page == 3);

        // 显隐切换: Fgui 隐藏的子控件会被跳过(不聚合脏区/不渲染), 所以对它们
        // 单独 Invalidate 无效。必须让"仍可见的父容器(home 内容区)"整体失效,
        // 主循环才会把整块区域清成背景色并重绘其余可见控件, 残留画面才会消失。
        if(bg_title->IsVisibility() != relevant){
            bg_title->SetVisibility(relevant);
            bg_path_text->SetVisibility(relevant);
            bg_pick_btn->SetVisibility(relevant);
            bg_clear_btn->SetVisibility(relevant);
            tab_control->GetTab("home").content->InvalidateRect();
        }
        if(!relevant) return;

        std::string title = (device_loop.page == 2) ? "时钟页背景:" : "媒体页背景:";
        if(bg_title->GetText() != title) bg_title->SetText(title);

        std::string path = (device_loop.page == 2) ? device_loop.ClockBackgroundPath()
                                                   : device_loop.MediaBackgroundPath();
        std::string shown = path.empty() ? "未设置背景图片" : std::format("当前: {}", _PathBaseName(path));
        if(bg_path_text->GetText() != shown) bg_path_text->SetText(shown);
    }

    void ActionOnPaint(SDL_Renderer* renderer,const SDL_Rect& dirty_region,const SDL_Rect& relative_rect) override{
        SDL_Rect draw_area = this->default_rect;
        draw_area.x = relative_rect.x;
        draw_area.y = relative_rect.y;

        ControlBox::ActionOnPaint(renderer,dirty_region,relative_rect);
    }

    void ActionOnTimer(const SDL_Rect& relative_rect) override{
        SyncBackgroundUI();
        if(SDL_GetTicks() - last_data_update_time > 500){ 

            std::string text_fps_status;
            if(!device_loop.device.IsConnected()){
                text_fps_status = "未连接";
            }
            else if(device_loop.page == device_loop.PAGE_MAX){
                text_fps_status = "推送暂停";
            }
            else if(update_fps_limiter.Get() > 0){
                text_fps_status = std::format("帧数: {:.1f}/{:.1f}",push_fps.GetFPS(),update_fps_limiter.Get());
            }
            else{
                text_fps_status = std::format("帧数: {:.1f}/无限制",push_fps.GetFPS());
            }
            this->FindControl<TabControl>("tab")->GetTab("home").content->FindControl<StaticText>("fps_text")->SetText(text_fps_status);
            last_data_update_time = SDL_GetTicks();

            std::vector<ComboBox::ComboItem> port_items;
            auto ports = MSNDevice::ListPorts();
            int n = 0;
            for(auto& i : ports){
                port_items.push_back({i,n++,0});
            }
            int port_select = -1;
            std::string port_name = device_loop.device.Port();
            for(auto& i : port_items){
                if(i.ui_string == port_name){
                    port_select = i.id;
                    break;
                }
            }
            this->FindControl<TabControl>("tab")->GetTab("home").content->FindControl<ComboBox>("port_select")->SetItems(port_items,port_select);
        }
        ControlBox::ActionOnTimer(relative_rect);
    }

    std::string ActionOnGetTypeName() const override{
        return "MainWindowTopMenuInterface";
    }

};

class MainWindow{
public:

    SDL_Rect window_rect;

    std::unique_ptr<std::thread> mw_thread;
    SDL_WindowIM::WindowIM window;

    bool mw_running;

    MainWindow(){
        mw_running = true;
        window_rect = {0, 0, 500, 500};
    }

    ~MainWindow(){
        Dispose();
    }

    static int ThreadEntry(MainWindow& mw){
        SDL_Rect& window_rect = mw.window_rect;
        // 值得说的是这里的renderer在throw时会被SDL_Quit回收，所以不用担心
        SDL_Renderer *renderer = SDL_CreateRenderer(mw.window.Get(), NULL);
        if (!renderer)
        {
            throw std::runtime_error("SDL_CreateRenderer failed: " + std::string(SDL_GetError()));
        }

        Timer fps_limit(20);
        SDL_Event event;
        eventDispatcher.InitCurrentThread({mw.window.GetID()});

        SDL_Texture *frame_buf = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, window_rect.w, window_rect.h);

        // 准备GUI
        FontEx font({"./SourceHanSansCN-Medium.ttf"});
        // 创建根容器
        std::shared_ptr<ControlBox> cb = std::make_shared<ControlBox>(window_rect);

        cb->PushbackControl("top", std::make_shared<MainWindowTopMenuInterface>(&font, window_rect, mw.window.Get()));

        cb->InvalidateRect();

        mw.window.Show();
        while (g_running && mw.mw_running)
        {
            // 处理事件
            while (eventDispatcher.PollEvent(&event))
            {
                if (event.type == SDL_EVENT_QUIT)
                {
                    g_running = false;
                }
                else if (event.window.windowID == mw.window.GetID())
                {
                    if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
                    {
                        cb->InvalidateRect();
                    }
                    else if (event.type == SDL_EVENT_WINDOW_MOVED)
                    {
                        cb->InvalidateRect();
                    }
                    else if (event.type == SDL_EVENT_WINDOW_EXPOSED)
                    {
                        cb->InvalidateRect();
                    }
                    else if( event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED){
                       mw.window.Hide();
                    }
                    // else if(event.type == SDL_EVENT_KEY_DOWN){
                    //     if(event.key.scancode == SDL_SCANCODE_UP){
                    //         device_loop.page++;
                    //     }
                    //     else if(event.key.scancode == SDL_SCANCODE_DOWN){
                    //         device_loop.page--;
                    //     }
                    // }
                    cb->MaintainEvent(&event, window_rect);
                }
            }

            // 调用Tick回调
            cb->MaintainTimer(window_rect);
            // 读取脏区域
            SDL_Rect dirty_rect = cb->GetInvaildRect(window_rect);
            // 判断是否有脏区域
            if (dirty_rect.w != 0 && dirty_rect.h != 0)
            {
                // GL_Log(GL_LOGLEVEL_DEBUG,"脏区域:%d,%d,%d,%d",dirty_rect.x,dirty_rect.y,dirty_rect.w,dirty_rect.h);
                // 在持久帧缓冲纹理上局部重绘脏区
                SDL_FRect fr_dirty = toFRect(dirty_rect);
                SDL_SetRenderTarget(renderer, frame_buf);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                SDL_RenderFillRect(renderer, &fr_dirty);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                cb->MaintainRender(renderer, window_rect);
                SDL_SetRenderTarget(renderer, NULL);
                // 整体拷贝持久缓冲到 backbuffer 并呈现
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
                SDL_RenderClear(renderer);
                SDL_RenderTexture(renderer, frame_buf, NULL, NULL);
                SDL_RenderPresent(renderer);
            }

            fps_limit.Delay();
        }
        //In some of the platform, destroying window in non-main thread will cause crash. So we hide it instead.
        mw.window.Hide();
        return 0;
    }

    void Show(){
        if(!mw_thread) return;
        window.Show();
    }

    void Startup(){
        if(mw_thread) return;
        //Windows platform limitation, or the window will not be responsive.
        window.Create(std::format("USB屏幕上位机程序 {}",BUILD_VERSION), window_rect.w, window_rect.h, SDL_WINDOW_HIDDEN);
        window.SetIMState(false);
        mw_thread = std::make_unique<std::thread>(ThreadEntry,std::ref(*this));
    }

    void Dispose(){
        mw_running = false;
        if(mw_thread) mw_thread->join();
        window.Destroy();
        mw_thread.reset();
    }
};

MainWindow mainWindow;

int _main(int argc, char **argv)
{
    int fail = performance_monitor.Init(PERFORMANCE_QUERY_INIT_CPU | PERFORMANCE_QUERY_INIT_GPU | PERFORMANCE_QUERY_INIT_MEM);
    CoInitialize(NULL);
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "1");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
    SDLEnv sdl;
    {
        mainWindow.Startup();
        mainWindow.Show();
        SDL_Event event;
        device_loop.Startup();
        SDL_Tray* tray = SDL_CreateTray(SDL_LoadPNG("icon.png"),"USB屏幕上位机程序");
        SDL_TrayMenu* menu = SDL_CreateTrayMenu(tray);
        SDL_TrayEntry* entryShow = SDL_InsertTrayEntryAt(menu, -1, "显示控制窗口", SDL_TRAYENTRY_BUTTON);
        SDL_SetTrayEntryCallback(entryShow, [](void*, SDL_TrayEntry*){
            mainWindow.Show();
            mainWindow.window.ForceTop();
        }, nullptr);
        SDL_TrayEntry* entryQuit = SDL_InsertTrayEntryAt(menu, -1, "退出", SDL_TRAYENTRY_BUTTON);
        SDL_SetTrayEntryCallback(entryQuit, [](void*, SDL_TrayEntry*){
            g_running = false;
        }, nullptr);

        while(g_running){
            while (SDL_WaitEventTimeout(&event, 50))
            {
                // if(event.type == SDL_EVENT_QUIT){
                //     g_running = false;
                // }
                eventDispatcher.DispatchEvents(&event);
            }
        }
        mainWindow.Dispose();
        device_loop.Shutdown();
    }
    return 0;
}

int SDL_main(int argc, char **argv)
{
    int ret = _main(argc, argv);

    return ret;
}