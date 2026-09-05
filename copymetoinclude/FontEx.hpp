#ifndef __INC_FONTEX_
#define __INC_FONTEX_

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <variant>
#include <cstdint>
#include "decode.hpp"

/**
 * @brief 转换原始多行UTF-8文本为Codepoints列表
 * 
 * @param text 原始多行UTF-8文本
 * @return std::vector<std::vector<uint32_t>> 每行的Codepoint列表
 */
std::vector<std::vector<uint32_t>> RawText2CodepointsLines(const std::string& text){
    std::vector<std::vector<uint32_t>> lines;
    //丢弃\r，只判断\n，可以兼容CRLF和LF
    std::vector<uint32_t> current_line;
    size_t i = 0;
    size_t len = text.size();
    while(i < len){
        unsigned char c = (unsigned char)text[i];
        if(c < 0x80){
            //单字节ASCII字符，直接处理换行与回车
            if(c == '\n'){
                lines.push_back(std::move(current_line));
                current_line.clear();
            }else if(c != '\r'){
                current_line.push_back(c);
            }
            i++;
        }else{
            //多字节UTF-8字符，手动解码出Codepoint
            uint32_t cp = 0;
            size_t extra = 0;
            if((c & 0xE0) == 0xC0){
                cp = c & 0x1F;
                extra = 1;
            }else if((c & 0xF0) == 0xE0){
                cp = c & 0x0F;
                extra = 2;
            }else if((c & 0xF8) == 0xF0){
                cp = c & 0x07;
                extra = 3;
            }else{
                //非法首字节，跳过
                i++;
                continue;
            }
            bool valid = (i + extra < len);
            for(size_t k = 1; valid && k <= extra; k++){
                unsigned char cc = (unsigned char)text[i + k];
                if((cc & 0xC0) != 0x80){
                    valid = false;
                }else{
                    cp = (cp << 6) | (cc & 0x3F);
                }
            }
            if(valid){
                current_line.push_back(cp);
            }
            i += extra + 1;
        }
    }
    lines.push_back(std::move(current_line));
    return lines;
}

void ttf_font_releaser(TTF_Font* font){
    if(TTF_WasInit()) TTF_CloseFont(font);
}

class FontContainer{
    std::shared_ptr<TTF_Font> normal;
    std::shared_ptr<TTF_Font> styled;
    public:

    FontContainer(const std::string& path,int ptsize){
        this->Open(path,ptsize);
    }

    void Open(const std::string& path,int ptsize){
        TTF_Font* fa = TTF_OpenFont(path.c_str(),ptsize);
        TTF_Font* fb = TTF_OpenFont(path.c_str(),ptsize);
        if(!(fa && fb)){
            if(fa) TTF_CloseFont(fa);
            if(fb) TTF_CloseFont(fb);
            throw std::runtime_error(cpp_sformat("无法打开字体:\"%s\"",path.c_str()));
        }
        normal.reset(fa,ttf_font_releaser);
        styled.reset(fb,ttf_font_releaser);
    }

    TTF_Font* GetStyled(int style) const{
        if(TTF_GetFontStyle(&*styled) == (TTF_FontStyleFlags)style){
            return &*styled;
        }
        TTF_SetFontStyle(&*styled,(TTF_FontStyleFlags)style);
        return &*styled;
    }

    TTF_Font* GetNormal() const{
        return &*normal;
    }
};

class FontSet{
    std::unordered_map<int,FontContainer> fonts;
    std::string font_path;

    public:

    FontSet(){

    }

    FontSet(const std::string& path){
        this->Open(path);
    }

    /*
        注意：这个函数不会检查字体是否存在或者字体是否有效。
        只有调用GetNormal函数或者GetStyled才会进行检查，
        这是因为只有调用那些函数时才会真正意义上的打开字体
    */
    bool Open(const std::string& path){
        font_path = path;
        return true;
    }

    const FontContainer& Get(int ptsize){
        if(ptsize < 1) throw std::runtime_error(cpp_sformat("字体大小应该大于或等于1(当前参数:\"%d\")",ptsize));
        auto fd = fonts.find(ptsize);
        if(fd == fonts.end()){
            fonts.insert({ptsize,FontContainer(font_path,ptsize)});
            fd = fonts.find(ptsize);
        }
        return fd->second;
    }

    const FontContainer& operator[](int ptsize){
        return Get(ptsize);
    }

    void ClearCache(){
        fonts.clear();
    }
};

class FontEx{
    std::vector<FontSet> fonts;
    public:
    FontEx(){

    }

    FontEx(const std::vector<std::string>& font_paths){
        this->OpenFonts(font_paths);
    }

    void OpenFonts(const std::vector<std::string>& font_paths){
        for(auto& i : font_paths){
            fonts.push_back(FontSet(i));
        }
    }

    FontSet& GetAt(int pos){
        return fonts[pos];
    }

    class Glyph{
        public:
        // 该字符在行中占用的矩形区域
        SDL_Rect rect;
        uint32_t codepoint = 0;
        // 该字符的Surface，不一定存在；空格、TAB等无字形字符surface为NULL，但rect仍有效。
        // 该Surface的生命周期与Glyph对象相同
        SDL_Surface* surface = nullptr;

        Glyph() = default;
        Glyph(uint32_t cp, const SDL_Rect& r, SDL_Surface* s) : rect(r), codepoint(cp), surface(s){
        }
        ~Glyph(){
            if(surface) SDL_DestroySurface(surface);
        }

        // Glyph持有SDL_Surface*资源，禁止拷贝、支持移动
        Glyph(const Glyph&) = delete;
        Glyph& operator=(const Glyph&) = delete;
        Glyph(Glyph&& other) noexcept : rect(other.rect), codepoint(other.codepoint), surface(other.surface){
            other.surface = nullptr;
        }
        Glyph& operator=(Glyph&& other) noexcept{
            if(this != &other){
                if(surface) SDL_DestroySurface(surface);
                rect = other.rect;
                codepoint = other.codepoint;
                surface = other.surface;
                other.surface = nullptr;
            }
            return *this;
        }
    };

    class LineInfo
    {
    public:
        // 该行占用的矩形区域
        SDL_Rect rect;
        std::vector<Glyph> glyphs;
    };

    class LayoutInfo{
        public:
        int total_width;
        int total_height;
        std::vector<LineInfo> line_infos;
    };

    /**
     * @brief 输出单行文本布局
     * 
     * @param ptsize 字体大小
     * @param chars 字符列表
     * @param info 输出的布局信息
     * @param create_glyphs 是否创建GlyphSurface
     * @param style 字体样式
     */
    void LineLayout(int ptsize, const std::vector<uint32_t>& chars,LineInfo& info,bool create_glyphs = true,int style = 0,SDL_Color color = {255,255,255,255}){
        info.rect = {0,0,0,0};
        info.glyphs.clear();
        if(fonts.empty()){
            return;
        }

        // 根据样式获取对应字体的辅助函数
        auto get_font = [&](int font_index) -> TTF_Font* {
            if(style == 0){
                return fonts[font_index].Get(ptsize).GetNormal();
            }
            return fonts[font_index].Get(ptsize).GetStyled(style);
        };

        TTF_Font* default_font = get_font(0);
        int default_line_height = TTF_GetFontHeight(default_font);
        int default_space_width = 0;
        int __temp;
        TTF_GetStringSize(default_font, " ", 1, &default_space_width, &__temp);
        if(default_space_width <= 0){
            default_space_width = default_line_height / 2;
        }

        // 第一遍：统计每个字符的宽度、高度与所用字体
        struct CharMetric{
            uint32_t codepoint;
            int font_index;
            int w;
            int h;
        };
        std::vector<CharMetric> metrics;
        metrics.reserve(chars.size());

        int total_width = 0;
        int max_height = default_line_height;

        for(auto& cp : chars){
            // 选择第一个提供该字形的字体
            int font_index = 0;
            for(; font_index < (int)fonts.size(); font_index++){
                if(TTF_FontHasGlyph(get_font(font_index), cp)){
                    break;
                }
            }
            if(font_index >= (int)fonts.size()){
                font_index = 0;
            }

            CharMetric m;
            m.codepoint = cp;
            m.font_index = font_index;

            if(cp == '\t'){
                // TAB宽度 = 空格宽度 * 4
                m.w = default_space_width * 4;
                m.h = default_line_height;
            }
            else{
                int minx, maxx, miny, maxy, advance;
                bool provided = TTF_FontHasGlyph(get_font(font_index), cp);
                if(!provided){
                    // 无字形，用 U+FFFD 替代
                    m.codepoint = 0xFFFD;
                    if(!TTF_FontHasGlyph(get_font(font_index), m.codepoint)){
                        // 退回
                        m.codepoint = cp;
                    }
                    else{
                        provided = true;
                    }
                }
                if(provided && TTF_GetGlyphMetrics(get_font(font_index), cp, &minx, &maxx, &miny, &maxy, &advance)){
                    m.w = std::max(advance, maxx - minx);
                    m.h = maxy - miny;
                    if(m.w <= 0) m.w = default_space_width;
                    if(m.h <= 0) m.h = default_line_height;
                }
                else{
                    // 无字形（字体不提供该字符），用空格宽度占位
                    m.w = default_space_width;
                    m.h = default_line_height;
                }
            }

            metrics.push_back(m);
            max_height = std::max(max_height, m.h);
            total_width += m.w;
        }

        // 第二遍：生成Glyph。
        // SDL_ttf的单字形surface已按基线排版（字形在surface内自带ascent偏移，baseline位置一致），
        // 因此rect.y取0让surface顶部对齐行顶即可得到标准基线对齐；rect仅用于占位与定位。
        // 空格、TAB等无字形字符surface为NULL，仅靠rect占位。
        int cur_x = 0;
        info.glyphs.reserve(metrics.size());
        for(auto& m : metrics){
            int y = 0;
            SDL_Surface* sur = nullptr;
            if(create_glyphs){
                bool is_blank = (m.codepoint == ' ' || m.codepoint == '\t');
                if(!is_blank && TTF_FontHasGlyph(get_font(m.font_index), m.codepoint)){
                    sur = TTF_RenderGlyph_Blended(get_font(m.font_index), m.codepoint, color);
                }
            }
            Glyph g(m.codepoint, {cur_x, y, m.w, m.h}, sur);
            info.glyphs.emplace_back(std::move(g));
            cur_x += m.w;
        }

        info.rect = {0, 0, total_width, max_height};
    }

    /**
     * @brief 输出多行文本布局
     * 
     * @param ptsize 字体大小
     * @param chars 每行的Codepoint列表
     * @param info 输出的布局信息
     * @param create_glyphs 是否创建GlyphSurface
     * @param style 字体样式
     */
    void TextLayout(int ptsize, const std::vector<std::vector<uint32_t>>& chars,LayoutInfo& info,bool create_glyphs = true,int style = 0,SDL_Color color = {255,255,255,255}){
        info.total_width = 0;
        info.total_height = 0;
        info.line_infos.clear();
        for(auto& i : chars){
            LineInfo line_info;
            LineLayout(ptsize, i, line_info, create_glyphs, style, color);
            info.line_infos.emplace_back(std::move(line_info));
            info.total_width = std::max(info.total_width, line_info.rect.w);
            info.total_height += line_info.rect.h;
        }
    }

    void SizeText(int ptsize, const std::string& text,int* w,int* h){
        LayoutInfo info;
        TextLayout(ptsize, RawText2CodepointsLines(text), info,false);
        *w = info.total_width;
        *h = info.total_height;
    }

    SDL_Surface* RenderText_Blended(int ptsize, const std::string& text, SDL_Color color) {
        LayoutInfo info;
        TextLayout(ptsize, RawText2CodepointsLines(text), info, true, 0, color);

        if(info.total_width <= 0 || info.total_height <= 0){
            return nullptr;
        }

        SDL_Surface* final_surface = SDL_CreateSurface(info.total_width, info.total_height, SDL_PIXELFORMAT_RGBA8888);
        if(!final_surface){
            return nullptr;
        }
        SDL_FillSurfaceRect(final_surface, NULL, SDL_MapSurfaceRGBA(final_surface, 0, 0, 0, 0));
        SDL_SetSurfaceBlendMode(final_surface,SDL_BLENDMODE_NONE);

        // 空格、TAB等无字形字符的surface为NULL，仅靠rect占位，不参与绘制。
        // 注意：blit目标尺寸使用surface实际宽高，避免因advance与位图宽不一致导致字符拉伸错位。
        int cur_y = 0;
        for(auto& line : info.line_infos){
            for(auto& glyph : line.glyphs){
                if(glyph.surface){
                    SDL_Rect dst = { glyph.rect.x, cur_y + glyph.rect.y, glyph.surface->w, glyph.surface->h };
                    SDL_SetSurfaceBlendMode(glyph.surface,SDL_BLENDMODE_NONE);
                    SDL_BlitSurface(glyph.surface, NULL, final_surface, &dst);
                }
            }
            cur_y += line.rect.h;
        }
        return final_surface;
    }

    SDL_Surface* RenderStyledText_Blended(int ptsize, const std::string& text, SDL_Color color,int style) {
        LayoutInfo info;
        TextLayout(ptsize, RawText2CodepointsLines(text), info, true, style, color);

        if(info.total_width <= 0 || info.total_height <= 0){
            return nullptr;
        }

        SDL_Surface* final_surface = SDL_CreateSurface(info.total_width, info.total_height, SDL_PIXELFORMAT_RGBA8888);
        if(!final_surface){
            return nullptr;
        }
        SDL_FillSurfaceRect(final_surface, NULL, SDL_MapSurfaceRGBA(final_surface, 0, 0, 0, 0));
        SDL_SetSurfaceBlendMode(final_surface,SDL_BLENDMODE_NONE);

        // 空格、TAB等无字形字符的surface为NULL，仅靠rect占位，不参与绘制。
        // 注意：blit目标尺寸使用surface实际宽高，避免因advance与位图宽不一致导致字符拉伸错位。
        int cur_y = 0;
        for(auto& line : info.line_infos){
            for(auto& glyph : line.glyphs){
                if(glyph.surface){
                    SDL_Rect dst = { glyph.rect.x, cur_y + glyph.rect.y, glyph.surface->w, glyph.surface->h };
                    SDL_SetSurfaceBlendMode(glyph.surface,SDL_BLENDMODE_NONE);
                    SDL_BlitSurface(glyph.surface, NULL, final_surface, &dst);
                }
            }
            cur_y += line.rect.h;
        }
        return final_surface;
    }

    SDL_Rect drawTextOnSurface(SDL_Surface* surface,int x,int y,int ptsize, const std::string& text, SDL_Color color,const SDL_FPoint& center = {0.0f,0.0f}){
        SDL_Surface* sur = this->RenderText_Blended(ptsize,text,color);
        if(!sur) return {-1,-1,-1,-1};
        SDL_Rect dst = {x,y,sur->w,sur->h};
        dst.x -= dst.w * center.x;
        dst.y -= dst.h * center.y;
        SDL_SetSurfaceBlendMode(sur,SDL_BLENDMODE_BLEND);
        SDL_BlitSurface(sur,NULL,surface,&dst);
        SDL_DestroySurface(sur);
        return dst;
    }

    SDL_Rect paintText_Blended(SDL_Renderer* renderer,int x,int y,int ptsize,const std::string& text, SDL_Color color,const SDL_FPoint& center = {0.0f,0.0f}){
        SDL_Surface* sur = this->RenderText_Blended(ptsize,text,color);
        if(!sur) return {-1,-1,-1,-1};
        SDL_Rect dst = {x,y,sur->w,sur->h};
        dst.x -= dst.w * center.x;
        dst.y -= dst.h * center.y;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer,sur);
        SDL_SetTextureBlendMode(tex,SDL_BLENDMODE_BLEND);
        SDL_FRect fdst = {(float)dst.x,(float)dst.y,(float)dst.w,(float)dst.h};
        SDL_RenderTexture(renderer,tex,NULL,&fdst);
        SDL_DestroySurface(sur);
        SDL_DestroyTexture(tex);
        return dst;
    }

    SDL_Rect paintStyledText_Blended(SDL_Renderer* renderer,int x,int y,int ptsize,const std::string& text, SDL_Color color,int style,const SDL_FPoint& center = {0.0f,0.0f}){
        SDL_Surface* sur = this->RenderStyledText_Blended(ptsize,text,color,style);
        if(!sur) return {-1,-1,-1,-1};
        SDL_Rect dst = {x,y,sur->w,sur->h};
        dst.x -= dst.w * center.x;
        dst.y -= dst.h * center.y;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer,sur);
        SDL_SetTextureBlendMode(tex,SDL_BLENDMODE_BLEND);
        SDL_FRect fdst = {(float)dst.x,(float)dst.y,(float)dst.w,(float)dst.h};
        SDL_RenderTexture(renderer,tex,NULL,&fdst);
        SDL_DestroySurface(sur);
        SDL_DestroyTexture(tex);
        return dst;
    }

    int GetPrimaryFontHeight(int ptsize){
        if(fonts.empty()) return 0;
        return TTF_GetFontHeight(fonts[0].Get(ptsize).GetNormal());
    }

    // 返回主字体(ptsize)的 ascent(基线距行顶距离), 用于不同字号文本按基线(底边)对齐
    int GetPrimaryFontAscent(int ptsize){
        if(fonts.empty()) return 0;
        return TTF_GetFontAscent(fonts[0].Get(ptsize).GetNormal());
    }

    SDL_Surface *GetGlyph(uint32_t codepoint, int ptsize,SDL_Color color)
    {
        int font_index = 0;
        for (auto &j : fonts)
        {
            if (TTF_FontHasGlyph(j.Get(ptsize).GetNormal(), codepoint))
            {
                break;
            }
            font_index++;
        }
        if (font_index >= fonts.size())
        {
            font_index = 0;
        }
        SDL_Surface *sur = TTF_RenderGlyph_Blended(fonts[font_index].Get(ptsize).GetNormal(), codepoint, color);
        return sur;
    }

    // 把给定的像素高度(height)换算成合适的点大小(ptsize)。
    // SDL_ttf 的字体高度与点大小近似线性(字高 ≈ ptsize × 系数)，但该系数随字体不同而变化，
    // 无法用固定公式精确换算。因此这里用主字体(fonts[0])实测校准：
    // 先打开一个估算字号量出真实高度，再按 目标/实测 比例缩放，重复几次直至收敛。
    int PtsizeFromHeight(int height){
        if(fonts.empty()) return height;   // 无字体时按像素值原样返回
        if(height < 1) height = 1;

        int estimate = height;   // 初始把像素高度当作点大小
        for(int pass = 0; pass < 4; pass++){
            int actual = TTF_GetFontHeight(fonts[0].Get(estimate).GetNormal());
            if(actual == height) break;

            // 按 目标高度/实测高度 的比例缩放点大小
            long long next = (long long)estimate * height / actual;
            if(next < 1) next = 1;
            if(next > 65535) next = 65535;
            if(next == estimate) break;    // 取整后已不再变化，视为收敛
            estimate = (int)next;
        }
        return estimate;
    }

    // 把给定的像素宽度(width)换算成合适的点大小(ptsize)，使 text 以主字体渲染时总宽度接近 width。
    // 文本宽度与点大小近似线性，但比例系数同时取决于字体与具体文本内容，无法用固定公式换算。
    // 因此这里实测校准：先按估算字号量出文本实际宽度，再按 目标/实测 比例缩放迭代逼近；
    // 由于字符 advance 取整会使宽度随字号呈"台阶"变化，最后在结果邻域扫描取最接近目标者。
    int PtsizeFromWidth(int width, const std::string& text){
        if(width < 1) return 1;
        if(fonts.empty()) return 1;

        auto lines = RawText2CodepointsLines(text);
        size_t ch_count = 0;
        for(auto& ln : lines) ch_count += ln.size();
        if(ch_count == 0) return 1;   // 空文本宽度恒为0，无可换算

        // 返回 text 在指定点大小下渲染出的总宽
        auto measure = [&](int pt) -> int{
            LayoutInfo info;
            TextLayout(pt, lines, info, false);
            return info.total_width;
        };

        // 初始猜测：平均每字符宽度约为点大小的0.8倍(中西文混合的粗略平均)
        int estimate = (int)((double)width / (ch_count * 0.8) + 0.5);
        estimate = std::clamp(estimate, 1, 4096);

        // 按 目标宽度/实测宽度 的比例缩放，迭代逼近
        for(int pass = 0; pass < 6; pass++){
            int w = measure(estimate);
            if(w <= 0) break;
            if(w == width) return estimate;

            long long next = (long long)estimate * width / w;
            if(next < 1) next = 1;
            if(next > 4096) next = 4096;
            if((int)next == estimate) break;   // 取整后已不再变化，视为收敛
            estimate = (int)next;
        }

        // 在结果邻域做线性扫描，选实际宽度最接近目标值者(覆盖整型台阶/振荡)
        auto iabs = [](int v){ return v < 0 ? -v : v; };
        int best = estimate;
        int best_err = iabs(measure(estimate) - width);
        for(int d = estimate - 2; d <= estimate + 2; d++){
            if(d < 1) continue;
            int err = iabs(measure(d) - width);
            if(err < best_err){
                best_err = err;
                best = d;
            }
        }
        return best;
    }
};

#endif
