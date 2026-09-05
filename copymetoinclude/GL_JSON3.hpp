#ifndef __INC_GL_JSON3_
#define __INC_GL_JSON3_

#ifdef __INC_GL_JSON2_

#error "GL_JSON3.hpp与GL_JSON2.hpp不兼容，你只能包含一个版本"

#endif

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <istream>
#include <iterator>
#include <limits>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#ifdef _WIN32
#include <cwchar>
#include <stringapiset.h>
#endif

#include "ExceptionEx.hpp"

// ============================================================================
// GL JSON 解析库 版本3（现代化重构）
//
// 风格参考 nlohmann/json：
//   - 元素访问:  j["key"] / j.at("key") / j.value("key", 默认值) / j.contains("key")
//   - 类型判断:  is_null() / is_object() / is_array() / is_string() / is_number() ...
//   - 类型转换:  get<T>() / get_to(T&) / from_json / to_json（自定义类型走 ADL）
//   - 容器修改:  push_back / emplace_back / insert / erase / emplace / clear
//   - 迭代:      begin() / end() / rbegin() / rend() / items()
//   - 序列化:    dump(indent) / operator<< / operator>> / parse / load_file / save_file
//   - 数字区分整数(int64/uint64)与浮点(double)；对象保留插入顺序
//
// 破坏性变更（对比旧版 API）：
//   InObject(k)            -> j[k] / j.at(k) / j.value(k, 默认)
//   DoKeyExist(k)          -> j.contains(k)
//   Number()/Bool()/String()/Object()/Array() -> get<double>() / get<bool>() / get<std::string>() / ...
//   IsNull()               -> is_null()
//   GetNodeTypeName()      -> type_name()
//   Parse(s)               -> JSONNode::parse(s)
//   Stringify(d)           -> dump(d)
//   StringifyWithoutFormat()-> dump(-1)
//   SaveToFile/LoadFromFile-> save_file / JSONNode::load_file
//   JSONParseError         -> parse_error（含行/列）
//   JSONRuntimeError       -> type_error / out_of_range / other_error
// ============================================================================

namespace GL_JSON3 {

// ---- 解析标准 ----
enum class json_standard : int {
    rfc8259 = 0,  // 标准 JSON
    jsonc   = 1,  // JSONC：允许 // 与 /* */ 注释、允许尾随逗号
};

// ---- 源位置（行/列）----
struct parse_position {
    int line = 1;
    int column = 1;
};

// ============================================================================
// 异常体系（均继承 ExceptionEx，自带 source_location）
// ============================================================================

// 解析失败：携带出错位置（行/列）
class parse_error : public ExceptionEx {
    parse_position position_;
public:
    explicit parse_error(const std::string& msg,
                         std::source_location loc = std::source_location::current())
        : ExceptionEx(msg, loc) {}

    parse_error(const parse_position& pos, const std::string& msg,
                std::source_location loc = std::source_location::current())
        : ExceptionEx("[JSON解析错误]在行 " + std::to_string(pos.line)
                      + ",列 " + std::to_string(pos.column) + ":" + msg, loc),
          position_(pos) {}

    const parse_position& position_of() const noexcept { return position_; }
};

// 类型不匹配（如对数字调用 operator[] 期望对象）
class type_error : public ExceptionEx {
public:
    explicit type_error(const std::string& msg,
                        std::source_location loc = std::source_location::current())
        : ExceptionEx(msg, loc) {}
};

// 越界 / 键不存在
class out_of_range : public ExceptionEx {
public:
    explicit out_of_range(const std::string& msg,
                          std::source_location loc = std::source_location::current())
        : ExceptionEx(msg, loc) {}
};

// 其它运行时错误（文件 IO 等）
class other_error : public ExceptionEx {
public:
    explicit other_error(const std::string& msg,
                         std::source_location loc = std::source_location::current())
        : ExceptionEx(msg, loc) {}
};

// ---- 节点类型 ----
enum class value_t : std::uint8_t {
    null,
    boolean,
    number_integer,   // std::int64_t
    number_unsigned,  // std::uint64_t
    number_float,     // double
    string,
    array,
    object,
};

class JSONNode;

// ============================================================================
// 插入顺序保持的 map：查找 O(1)，迭代按插入顺序
// ============================================================================
template<typename Key, typename T>
class ordered_map {
public:
    using key_type        = Key;
    using mapped_type     = T;
    using value_type      = std::pair<const Key, T>;
    using size_type       = std::size_t;
    using difference_type = std::ptrdiff_t;
    using container       = std::vector<value_type>;
    using iterator        = typename container::iterator;
    using const_iterator  = typename container::const_iterator;

private:
    container values_;
    std::unordered_map<Key, size_type> index_;

public:
    ordered_map() = default;
    ordered_map(const ordered_map&) = default;
    ordered_map(ordered_map&&) = default;
    ordered_map(std::initializer_list<value_type> init) {
        for (const auto& kv : init) insert(kv);
    }

    // 元素含 const 键，无法逐元素赋值；拷贝赋值通过重建容器实现
    ordered_map& operator=(const ordered_map& other) {
        if (this == &other) return *this;
        container rebuilt;
        rebuilt.reserve(other.values_.size());
        for (const auto& kv : other.values_) rebuilt.push_back(kv);
        values_ = std::move(rebuilt);
        index_ = other.index_;
        return *this;
    }
    ordered_map& operator=(ordered_map&& other) noexcept = default;

    iterator begin() noexcept { return values_.begin(); }
    iterator end() noexcept { return values_.end(); }
    const_iterator begin() const noexcept { return values_.begin(); }
    const_iterator end() const noexcept { return values_.end(); }
    const_iterator cbegin() const noexcept { return values_.cbegin(); }
    const_iterator cend() const noexcept { return values_.cend(); }

    bool empty() const noexcept { return values_.empty(); }
    size_type size() const noexcept { return values_.size(); }
    size_type max_size() const noexcept { return values_.max_size(); }
    void clear() noexcept { values_.clear(); index_.clear(); }

    container& values() noexcept { return values_; }
    const container& values() const noexcept { return values_; }

    iterator find(const Key& key) {
        auto it = index_.find(key);
        if (it == index_.end()) return end();
        return values_.begin() + static_cast<difference_type>(it->second);
    }
    const_iterator find(const Key& key) const {
        auto it = index_.find(key);
        if (it == index_.end()) return end();
        return values_.begin() + static_cast<difference_type>(it->second);
    }
    bool contains(const Key& key) const { return index_.find(key) != index_.end(); }

    // 键已存在则覆盖值（保持原有顺序）；不存在则追加到末尾
    std::pair<iterator, bool> insert(const value_type& kv) {
        auto it = index_.find(kv.first);
        if (it != index_.end()) {
            values_[it->second].second = kv.second;
            return {values_.begin() + static_cast<difference_type>(it->second), false};
        }
        index_.emplace(kv.first, values_.size());
        values_.push_back(kv);
        return {values_.end() - 1, true};
    }
    std::pair<iterator, bool> insert(value_type&& kv) {
        auto it = index_.find(kv.first);
        if (it != index_.end()) {
            values_[it->second].second = std::move(kv.second);
            return {values_.begin() + static_cast<difference_type>(it->second), false};
        }
        index_.emplace(kv.first, values_.size());
        values_.push_back(std::move(kv));
        return {values_.end() - 1, true};
    }

    template<typename... Args>
    std::pair<iterator, bool> emplace(const Key& key, Args&&... args) {
        auto it = index_.find(key);
        if (it != index_.end()) {
            return {values_.begin() + static_cast<difference_type>(it->second), false};
        }
        index_.emplace(key, values_.size());
        values_.emplace_back(key, T(std::forward<Args>(args)...));
        return {values_.end() - 1, true};
    }

    // 键不存在则创建默认值并返回引用；存在则返回既有引用
    T& operator[](const Key& key) {
        auto it = index_.find(key);
        if (it != index_.end()) return values_[it->second].second;
        index_.emplace(key, values_.size());
        values_.emplace_back(key, T());
        return values_.back().second;
    }

    T& at(const Key& key) {
        auto it = index_.find(key);
        if (it == index_.end()) throw std::out_of_range("ordered_map: 键不存在");
        return values_[it->second].second;
    }
    const T& at(const Key& key) const {
        auto it = index_.find(key);
        if (it == index_.end()) throw std::out_of_range("ordered_map: 键不存在");
        return values_[it->second].second;
    }

    // 按迭代器删除（O(n)，通过重建保持插入顺序）
    iterator erase(const_iterator pos) {
        const size_type idx = static_cast<size_type>(pos - values_.cbegin());
        return erase_at(idx);
    }
    // 按键删除；返回删除数量（0 或 1）
    size_type erase(const Key& key) {
        auto it = index_.find(key);
        if (it == index_.end()) return 0;
        erase_at(it->second);
        return 1;
    }

private:
    iterator erase_at(size_type idx) {
        container rebuilt;
        rebuilt.reserve(values_.size() - 1);
        for (size_type i = 0; i < values_.size(); ++i) {
            if (i != idx) rebuilt.push_back(std::move(values_[i]));
        }
        index_.clear();
        for (size_type i = 0; i < rebuilt.size(); ++i) {
            index_.emplace(rebuilt[i].first, i);
        }
        values_ = std::move(rebuilt);
        return values_.begin() + static_cast<difference_type>(idx);
    }
};

// 公开的容器类型别名（供 get_ref<object_t&>() 等使用）
using array_t  = std::vector<JSONNode>;
using object_t = ordered_map<std::string, JSONNode>;

// ============================================================================
// 内部元编程辅助
// ============================================================================
namespace detail {

// 可迭代容器（有 value_type / iterator / begin / end / size）
template<typename T, typename = void>
struct is_iterable_container : std::false_type {};
template<typename T>
struct is_iterable_container<T, std::void_t<
    typename T::value_type,
    typename T::iterator,
    typename T::const_iterator,
    decltype(std::declval<T>().begin()),
    decltype(std::declval<T>().end()),
    decltype(std::declval<T>().size())>> : std::true_type {};

// 键值容器（有 key_type 与 mapped_type）
template<typename T, typename = void>
struct is_map_like : std::false_type {};
template<typename T>
struct is_map_like<T, std::void_t<typename T::key_type, typename T::mapped_type>> : std::true_type {};

// 字符串类（有 c_str()）
template<typename T, typename = void>
struct is_string_like : std::false_type {};
template<typename T>
struct is_string_like<T, std::void_t<
    typename T::value_type,
    decltype(std::declval<T>().c_str())>> : std::true_type {};

// 支持 push_back
template<typename T, typename = void>
struct has_push_back : std::false_type {};
template<typename T>
struct has_push_back<T, std::void_t<
    decltype(std::declval<T>().push_back(std::declval<typename T::value_type>()))>> : std::true_type {};

// 支持 insert(值)
template<typename T, typename = void>
struct has_insert_value : std::false_type {};
template<typename T>
struct has_insert_value<T, std::void_t<
    decltype(std::declval<T>().insert(std::declval<typename T::value_type>()))>> : std::true_type {};

// 是否为 JSONNode 已专门处理的类型（基础类型/字符串/容器别名等）
template<typename T, typename = void>
struct is_special_json_type : std::false_type {};
template<typename T>
struct is_special_json_type<T, typename std::enable_if<
    std::is_arithmetic<T>::value ||
    std::is_same<T, std::string>::value ||
    std::is_same<T, std::string_view>::value ||
    std::is_same<T, const char*>::value ||
    std::is_same<T, char*>::value ||
    std::is_same<T, std::nullptr_t>::value ||
    std::is_same<T, JSONNode>::value ||
    std::is_same<T, array_t>::value ||
    std::is_same<T, object_t>::value>::type> : std::true_type {};

// ============================================================================
// 内置字符串 / 编码工具（原 decode.hpp 内容内联）
// ============================================================================

// JSON 字符串转义（输出合法 JSON；控制字符 -> \u00XX）
inline std::string escape_string(const std::string& str) {
    std::string ret;
    ret.reserve(str.size());
    for (std::string::const_iterator i = str.begin(); i != str.end(); ++i) {
        unsigned char uc = static_cast<unsigned char>(*i);
        switch (*i) {
            case '\b': ret += "\\b"; break;
            case '\f': ret += "\\f"; break;
            case '\n': ret += "\\n"; break;
            case '\r': ret += "\\r"; break;
            case '\t': ret += "\\t"; break;
            case '\"': ret += "\\\""; break;
            case '\\': ret += "\\\\"; break;
            case '/': ret += "\\/"; break;
            default:
                if (uc < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", uc);
                    ret += buf;
                } else {
                    ret.push_back(*i);
                }
                break;
        }
    }
    return ret;
}

// Unicode 码点 -> UTF-8
inline std::string code_point_to_utf8(uint32_t code_point) {
    std::string utf8_char;
    if (code_point <= 0x7F) {
        utf8_char += static_cast<char>(code_point);
    } else if (code_point <= 0x07FF) {
        utf8_char += static_cast<char>(0xC0 | (code_point >> 6));
        utf8_char += static_cast<char>(0x80 | (code_point & 0x3F));
    } else if (code_point <= 0xFFFF) {
        utf8_char += static_cast<char>(0xE0 | (code_point >> 12));
        utf8_char += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
        utf8_char += static_cast<char>(0x80 | (code_point & 0x3F));
    } else {
        utf8_char += static_cast<char>(0xF0 | (code_point >> 18));
        utf8_char += static_cast<char>(0x80 | ((code_point >> 12) & 0x3F));
        utf8_char += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
        utf8_char += static_cast<char>(0x80 | (code_point & 0x3F));
    }
    return utf8_char;
}

// 打开二进制文件（跨平台）：
//   Windows：_wfopen + UTF-8->UTF-16 转换（支持中文/Unicode 路径）；
//   其它平台（Linux 等）：路径本身即 UTF-8 字节串，直接 fopen。
#ifdef _WIN32
inline std::wstring utf8_to_utf16(const std::string& mbcs) {
    int mbcsLen = static_cast<int>(mbcs.length());
    int wcharLen = MultiByteToWideChar(CP_UTF8, 0, mbcs.c_str(), mbcsLen, nullptr, 0);
    std::wstring wide(wcharLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, mbcs.c_str(), mbcsLen, &wide[0], wcharLen);
    return wide;
}
inline FILE* open_file_binary_read(const std::string& path) { return _wfopen(utf8_to_utf16(path).c_str(), L"rb"); }
inline FILE* open_file_binary_write(const std::string& path) { return _wfopen(utf8_to_utf16(path).c_str(), L"wb"); }
#else
inline FILE* open_file_binary_read(const std::string& path) { return std::fopen(path.c_str(), "rb"); }
inline FILE* open_file_binary_write(const std::string& path) { return std::fopen(path.c_str(), "wb"); }
#endif

}  // namespace detail

// 迭代代理模板前置声明（用于 items() 与结构化绑定）
template<typename NodeT> class iteration_proxy_value_t;
template<typename NodeT> class iteration_proxy_iterator_t;
template<typename NodeT> class iteration_proxy_t;
class JSONNode {
public:
    using value_t            = ::GL_JSON3::value_t;
    using size_type          = std::size_t;
    using difference_type    = std::ptrdiff_t;
    using array_t            = ::GL_JSON3::array_t;
    using object_t           = ::GL_JSON3::object_t;
    using string_t           = std::string;
    using boolean_t          = bool;
    using number_integer_t   = std::int64_t;
    using number_unsigned_t  = std::uint64_t;
    using number_float_t     = double;

private:
    using storage_t = std::variant<std::nullptr_t, bool, std::int64_t, std::uint64_t,
                                   double, std::string, array_t, object_t>;
    storage_t data{};

    // ---- 内部辅助（先定义，供嵌套迭代器使用）----
    JSONNode& element_at(size_type pos) {
        if (is_array()) return std::get<array_t>(data)[pos];
        if (is_object()) return std::get<object_t>(data).values()[pos].second;
        throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 array/object");
    }
    const JSONNode& element_at(size_type pos) const {
        if (is_array()) return std::get<array_t>(data)[pos];
        if (is_object()) return std::get<object_t>(data).values()[pos].second;
        throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 array/object");
    }
    const std::string& key_at(size_type pos) const {
        if (!is_object()) throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 object");
        return std::get<object_t>(data).values()[pos].first;
    }
    std::string key_or_index_string(size_type pos) const {
        if (is_object()) return key_at(pos);
        return std::to_string(pos);
    }

    void newline_indent(std::string& out, int depth, int indent, char ic) const {
        out.push_back('\n');
        for (int i = 0; i < depth * indent; ++i) out.push_back(ic);
    }
    void dump_impl(std::string& out, int depth, int indent, char ic) const {
        switch (type()) {
            case value_t::null: out += "null"; break;
            case value_t::boolean: out += (std::get<bool>(data) ? "true" : "false"); break;
            case value_t::number_integer: out += std::to_string(std::get<std::int64_t>(data)); break;
            case value_t::number_unsigned: out += std::to_string(std::get<std::uint64_t>(data)); break;
            case value_t::number_float: {
                double d = std::get<double>(data);
                char buf[64];
                auto res = std::to_chars(buf, buf + sizeof(buf), d, std::chars_format::general);
                if (res.ec == std::errc()) {
                    std::string_view sv(buf, static_cast<std::size_t>(res.ptr - buf));
                    if (sv.find_first_of(".eE") == std::string_view::npos) { out += sv; out += ".0"; }
                    else out += sv;
                } else {
                    out += std::to_string(d);
                }
                break;
            }
            case value_t::string: {
                out.push_back('"');
                out += detail::escape_string(std::get<std::string>(data));
                out.push_back('"');
                break;
            }
            case value_t::array: {
                out.push_back('[');
                const auto& arr = std::get<array_t>(data);
                bool first = true;
                for (const auto& el : arr) {
                    if (!first) out.push_back(',');
                    first = false;
                    if (indent >= 0) newline_indent(out, depth + 1, indent, ic);
                    el.dump_impl(out, depth + 1, indent, ic);
                }
                if (!arr.empty() && indent >= 0) newline_indent(out, depth, indent, ic);
                out.push_back(']');
                break;
            }
            case value_t::object: {
                out.push_back('{');
                const auto& obj = std::get<object_t>(data);
                bool first = true;
                for (const auto& kv : obj) {
                    if (!first) out.push_back(',');
                    first = false;
                    if (indent >= 0) newline_indent(out, depth + 1, indent, ic);
                    out.push_back('"');
                    out += detail::escape_string(kv.first);
                    out.push_back('"');
                    out.push_back(':');
                    if (indent >= 0) out.push_back(' ');
                    kv.second.dump_impl(out, depth + 1, indent, ic);
                }
                if (!obj.empty() && indent >= 0) newline_indent(out, depth, indent, ic);
                out.push_back('}');
                break;
            }
        }
    }

public:
    // ---- 迭代器（嵌套类，须先于使用它的成员定义）----
    class iterator {
        friend class JSONNode;
        JSONNode* m_node = nullptr;
        size_type m_pos = 0;
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = JSONNode;
        using difference_type = std::ptrdiff_t;
        using pointer = JSONNode*;
        using reference = JSONNode&;

        iterator() = default;
        iterator(JSONNode* node, size_type pos) : m_node(node), m_pos(pos) {}

        reference operator*() const { return m_node->element_at(m_pos); }
        pointer operator->() const { return &**this; }
        reference operator[](difference_type n) const { return m_node->element_at(m_pos + static_cast<size_type>(n)); }

        const std::string& key() const { return m_node->key_at(m_pos); }
        reference value() const { return **this; }
        size_type index() const noexcept { return m_pos; }

        iterator& operator++() { ++m_pos; return *this; }
        iterator operator++(int) { auto t = *this; ++m_pos; return t; }
        iterator& operator--() { --m_pos; return *this; }
        iterator operator--(int) { auto t = *this; --m_pos; return t; }
        iterator& operator+=(difference_type n) { m_pos += static_cast<size_type>(n); return *this; }
        iterator& operator-=(difference_type n) { m_pos -= static_cast<size_type>(n); return *this; }
        iterator operator+(difference_type n) const { auto t = *this; t += n; return t; }
        iterator operator-(difference_type n) const { auto t = *this; t -= n; return t; }
        friend iterator operator+(difference_type n, const iterator& it) { return it + n; }
        difference_type operator-(const iterator& o) const {
            return static_cast<difference_type>(m_pos) - static_cast<difference_type>(o.m_pos);
        }

        bool operator==(const iterator& o) const { return m_node == o.m_node && m_pos == o.m_pos; }
        bool operator!=(const iterator& o) const { return !(*this == o); }
        bool operator<(const iterator& o) const { return m_pos < o.m_pos; }
        bool operator<=(const iterator& o) const { return m_pos <= o.m_pos; }
        bool operator>(const iterator& o) const { return m_pos > o.m_pos; }
        bool operator>=(const iterator& o) const { return m_pos >= o.m_pos; }
    };

    class const_iterator {
        friend class JSONNode;
        const JSONNode* m_node = nullptr;
        size_type m_pos = 0;
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = JSONNode;
        using difference_type = std::ptrdiff_t;
        using pointer = const JSONNode*;
        using reference = const JSONNode&;

        const_iterator() = default;
        const_iterator(const JSONNode* node, size_type pos) : m_node(node), m_pos(pos) {}
        const_iterator(const iterator& it) : m_node(&*it), m_pos(it.index()) {}

        reference operator*() const { return m_node->element_at(m_pos); }
        pointer operator->() const { return &**this; }
        reference operator[](difference_type n) const { return m_node->element_at(m_pos + static_cast<size_type>(n)); }

        const std::string& key() const { return m_node->key_at(m_pos); }
        reference value() const { return **this; }
        size_type index() const noexcept { return m_pos; }

        const_iterator& operator++() { ++m_pos; return *this; }
        const_iterator operator++(int) { auto t = *this; ++m_pos; return t; }
        const_iterator& operator--() { --m_pos; return *this; }
        const_iterator operator--(int) { auto t = *this; --m_pos; return t; }
        const_iterator& operator+=(difference_type n) { m_pos += static_cast<size_type>(n); return *this; }
        const_iterator& operator-=(difference_type n) { m_pos -= static_cast<size_type>(n); return *this; }
        const_iterator operator+(difference_type n) const { auto t = *this; t += n; return t; }
        const_iterator operator-(difference_type n) const { auto t = *this; t -= n; return t; }
        friend const_iterator operator+(difference_type n, const const_iterator& it) { return it + n; }
        difference_type operator-(const const_iterator& o) const {
            return static_cast<difference_type>(m_pos) - static_cast<difference_type>(o.m_pos);
        }

        bool operator==(const const_iterator& o) const { return m_node == o.m_node && m_pos == o.m_pos; }
        bool operator!=(const const_iterator& o) const { return !(*this == o); }
        bool operator<(const const_iterator& o) const { return m_pos < o.m_pos; }
        bool operator<=(const const_iterator& o) const { return m_pos <= o.m_pos; }
        bool operator>(const const_iterator& o) const { return m_pos > o.m_pos; }
        bool operator>=(const const_iterator& o) const { return m_pos >= o.m_pos; }
    };

    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    template<typename> friend class iteration_proxy_value_t;

    // ==========================================================================
    // 构造
    // ==========================================================================
    JSONNode() = default;          // null
    JSONNode(std::nullptr_t) {}    // null
    JSONNode(bool b) : data(b) {}
    JSONNode(double d) : data(d) {}
    JSONNode(float f) : data(static_cast<double>(f)) {}
    // 有符号整数 -> int64
    template<typename T, typename std::enable_if<
        std::is_integral<T>::value && std::is_signed<T>::value && !std::is_same<T, bool>::value, int>::type = 0>
    JSONNode(T v) : data(static_cast<std::int64_t>(v)) {}
    // 无符号整数 -> uint64
    template<typename T, typename std::enable_if<
        std::is_integral<T>::value && std::is_unsigned<T>::value, int>::type = 0>
    JSONNode(T v) : data(static_cast<std::uint64_t>(v)) {}
    JSONNode(const std::string& s) : data(s) {}
    JSONNode(std::string&& s) : data(std::move(s)) {}
    JSONNode(std::string_view s) : data(std::string(s)) {}
    JSONNode(const char* s) : data(std::string(s)) {}
    JSONNode(const array_t& a) : data(a) {}
    JSONNode(array_t&& a) : data(std::move(a)) {}
    JSONNode(const object_t& o) : data(o) {}
    JSONNode(object_t&& o) : data(std::move(o)) {}
    JSONNode(std::initializer_list<JSONNode> init) {
        // 对象判定：全部元素都是「两个元素、首元素为字符串的数组」
        bool is_object = true;
        for (const auto& el : init) {
            if (!(el.is_array() && el.size() == 2 && el[0].is_string())) { is_object = false; break; }
        }
        if (is_object) {
            object_t obj;
            for (const auto& el : init) {
                obj.emplace(el[0].get_ref<const std::string&>(), el[1]);
            }
            data = std::move(obj);
        } else {
            data = array_t(init.begin(), init.end());
        }
    }
    // 容器 / 自定义类型（走 to_json，ADL）
    template<typename T, typename std::enable_if<!detail::is_special_json_type<T>::value, int>::type = 0>
    JSONNode(const T& v) { to_json(*this, v); }

    JSONNode(const JSONNode&) = default;
    JSONNode(JSONNode&&) = default;
    JSONNode& operator=(const JSONNode&) = default;
    JSONNode& operator=(JSONNode&&) = default;

    // ==========================================================================
    // 赋值
    // ==========================================================================
    JSONNode& operator=(std::nullptr_t) { data = nullptr; return *this; }
    JSONNode& operator=(bool b) { data = b; return *this; }
    JSONNode& operator=(double d) { data = d; return *this; }
    JSONNode& operator=(float f) { data = static_cast<double>(f); return *this; }
    template<typename T, typename std::enable_if<
        std::is_integral<T>::value && std::is_signed<T>::value && !std::is_same<T, bool>::value, int>::type = 0>
    JSONNode& operator=(T v) { data = static_cast<std::int64_t>(v); return *this; }
    template<typename T, typename std::enable_if<
        std::is_integral<T>::value && std::is_unsigned<T>::value, int>::type = 0>
    JSONNode& operator=(T v) { data = static_cast<std::uint64_t>(v); return *this; }
    JSONNode& operator=(const std::string& s) { data = s; return *this; }
    JSONNode& operator=(std::string&& s) { data = std::move(s); return *this; }
    JSONNode& operator=(std::string_view s) { data = std::string(s); return *this; }
    JSONNode& operator=(const char* s) { data = std::string(s); return *this; }
    JSONNode& operator=(const array_t& a) { data = a; return *this; }
    JSONNode& operator=(array_t&& a) { data = std::move(a); return *this; }
    JSONNode& operator=(const object_t& o) { data = o; return *this; }
    JSONNode& operator=(object_t&& o) { data = std::move(o); return *this; }
    JSONNode& operator=(std::initializer_list<JSONNode> init) { *this = JSONNode(init); return *this; }
    template<typename T, typename std::enable_if<!detail::is_special_json_type<T>::value, int>::type = 0>
    JSONNode& operator=(const T& v) { to_json(*this, v); return *this; }

    // ==========================================================================
    // 类型判断
    // ==========================================================================
    value_t type() const noexcept {
        if (std::holds_alternative<std::nullptr_t>(data)) return value_t::null;
        if (std::holds_alternative<bool>(data)) return value_t::boolean;
        if (std::holds_alternative<std::int64_t>(data)) return value_t::number_integer;
        if (std::holds_alternative<std::uint64_t>(data)) return value_t::number_unsigned;
        if (std::holds_alternative<double>(data)) return value_t::number_float;
        if (std::holds_alternative<std::string>(data)) return value_t::string;
        if (std::holds_alternative<array_t>(data)) return value_t::array;
        return value_t::object;
    }
    const char* type_name() const noexcept {
        switch (type()) {
            case value_t::null: return "null";
            case value_t::boolean: return "boolean";
            case value_t::number_integer: return "integer";
            case value_t::number_unsigned: return "unsigned";
            case value_t::number_float: return "float";
            case value_t::string: return "string";
            case value_t::array: return "array";
            case value_t::object: return "object";
        }
        return "unknown";
    }

    bool is_null() const noexcept { return std::holds_alternative<std::nullptr_t>(data); }
    bool is_boolean() const noexcept { return std::holds_alternative<bool>(data); }
    bool is_number_integer() const noexcept { return std::holds_alternative<std::int64_t>(data); }
    bool is_number_unsigned() const noexcept { return std::holds_alternative<std::uint64_t>(data); }
    bool is_number_float() const noexcept { return std::holds_alternative<double>(data); }
    bool is_number() const noexcept { return is_number_integer() || is_number_unsigned() || is_number_float(); }
    bool is_string() const noexcept { return std::holds_alternative<std::string>(data); }
    bool is_array() const noexcept { return std::holds_alternative<array_t>(data); }
    bool is_object() const noexcept { return std::holds_alternative<object_t>(data); }
    bool is_primitive() const noexcept { return is_null() || is_boolean() || is_number() || is_string(); }
    bool is_structured() const noexcept { return is_array() || is_object(); }

    // ==========================================================================
    // 底层取值（get<T>() / from_json 的基石；类型不符抛 type_error）
    // ==========================================================================
    bool get_boolean() const {
        if (!is_boolean()) throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 boolean");
        return std::get<bool>(data);
    }
    std::int64_t get_integer_signed() const {
        if (is_number_integer()) return std::get<std::int64_t>(data);
        if (is_number_unsigned()) {
            std::uint64_t u = std::get<std::uint64_t>(data);
            if (u > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                throw out_of_range("[JSON运行时错误]:无符号整数超出 int64 范围");
            return static_cast<std::int64_t>(u);
        }
        throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是整数");
    }
    std::uint64_t get_integer_unsigned() const {
        if (is_number_unsigned()) return std::get<std::uint64_t>(data);
        if (is_number_integer()) {
            std::int64_t s = std::get<std::int64_t>(data);
            if (s < 0) throw out_of_range("[JSON运行时错误]:负数不能转为无符号整数");
            return static_cast<std::uint64_t>(s);
        }
        throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是整数");
    }
    double get_double() const {
        if (is_number_integer()) return static_cast<double>(std::get<std::int64_t>(data));
        if (is_number_unsigned()) return static_cast<double>(std::get<std::uint64_t>(data));
        if (is_number_float()) return std::get<double>(data);
        throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是数字");
    }
    const std::string& get_string_ref() const {
        if (!is_string()) throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是字符串");
        return std::get<std::string>(data);
    }

    // get_ref<T>()：T 为 std::string / array_t / object_t
    template<typename T>
    T& get_ref() {
        using U = typename std::remove_cv<typename std::remove_reference<T>::type>::type;
        if constexpr (std::is_same<U, std::string>::value) {
            if (!is_string()) throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 string");
            return std::get<std::string>(data);
        } else if constexpr (std::is_same<U, array_t>::value) {
            if (!is_array()) throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 array");
            return std::get<array_t>(data);
        } else if constexpr (std::is_same<U, object_t>::value) {
            if (!is_object()) throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 object");
            return std::get<object_t>(data);
        } else {
            static_assert(std::is_same<U, std::string>::value || std::is_same<U, array_t>::value || std::is_same<U, object_t>::value,
                          "get_ref 仅支持 std::string / array_t / object_t");
        }
    }
    template<typename T>
    const T& get_ref() const {
        using U = typename std::remove_cv<typename std::remove_reference<T>::type>::type;
        if constexpr (std::is_same<U, std::string>::value) {
            if (!is_string()) throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 string");
            return std::get<std::string>(data);
        } else if constexpr (std::is_same<U, array_t>::value) {
            if (!is_array()) throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 array");
            return std::get<array_t>(data);
        } else if constexpr (std::is_same<U, object_t>::value) {
            if (!is_object()) throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 object");
            return std::get<object_t>(data);
        } else {
            static_assert(std::is_same<U, std::string>::value || std::is_same<U, array_t>::value || std::is_same<U, object_t>::value,
                          "get_ref 仅支持 std::string / array_t / object_t");
        }
    }

    // ==========================================================================
    // 元素访问
    // ==========================================================================
    // 对象：非 const 缺键自动创建；const 缺键抛 out_of_range
    JSONNode& operator[](const std::string& key) {
        if (is_null()) data = object_t{};
        if (!is_object()) throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 object");
        return std::get<object_t>(data)[key];
    }
    const JSONNode& operator[](const std::string& key) const {
        if (!is_object()) throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 object");
        auto& obj = std::get<object_t>(data);
        auto it = obj.find(key);
        if (it == obj.end()) throw out_of_range("[JSON运行时错误]:不存在键值: \"" + key + "\"");
        return it->second;
    }
    // 数组：越界抛 out_of_range
    JSONNode& operator[](size_type idx) {
        if (!is_array()) throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 array");
        auto& arr = std::get<array_t>(data);
        if (idx >= arr.size()) throw out_of_range("[JSON运行时错误]:数组越界: 下标 " + std::to_string(idx) + " 超出长度 " + std::to_string(arr.size()));
        return arr[idx];
    }
    const JSONNode& operator[](size_type idx) const {
        if (!is_array()) throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 array");
        auto& arr = std::get<array_t>(data);
        if (idx >= arr.size()) throw out_of_range("[JSON运行时错误]:数组越界: 下标 " + std::to_string(idx) + " 超出长度 " + std::to_string(arr.size()));
        return arr[idx];
    }

    // at()：键不存在 / 越界抛 out_of_range
    JSONNode& at(const std::string& key) {
        if (!is_object()) throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 object");
        auto& obj = std::get<object_t>(data);
        auto it = obj.find(key);
        if (it == obj.end()) throw out_of_range("[JSON运行时错误]:不存在键值: \"" + key + "\"");
        return it->second;
    }
    const JSONNode& at(const std::string& key) const {
        if (!is_object()) throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 object");
        auto& obj = std::get<object_t>(data);
        auto it = obj.find(key);
        if (it == obj.end()) throw out_of_range("[JSON运行时错误]:不存在键值: \"" + key + "\"");
        return it->second;
    }
    JSONNode& at(size_type idx) {
        if (!is_array()) throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 array");
        auto& arr = std::get<array_t>(data);
        if (idx >= arr.size()) throw out_of_range("[JSON运行时错误]:数组越界: 下标 " + std::to_string(idx) + " 超出长度 " + std::to_string(arr.size()));
        return arr[idx];
    }
    const JSONNode& at(size_type idx) const {
        if (!is_array()) throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 array");
        auto& arr = std::get<array_t>(data);
        if (idx >= arr.size()) throw out_of_range("[JSON运行时错误]:数组越界: 下标 " + std::to_string(idx) + " 超出长度 " + std::to_string(arr.size()));
        return arr[idx];
    }

    // value(key, 默认值)：键不存在时返回默认值；键存在但类型不符抛 type_error
    template<typename T>
    T value(const std::string& key, const T& default_value) const {
        if (is_object()) {
            auto& obj = std::get<object_t>(data);
            auto it = obj.find(key);
            if (it != obj.end()) return it->second.template get<T>();
        }
        return default_value;
    }
    template<typename T>
    T value(const std::string& key, T&& default_value) const {
        if (is_object()) {
            auto& obj = std::get<object_t>(data);
            auto it = obj.find(key);
            if (it != obj.end()) return it->second.template get<T>();
        }
        return std::move(default_value);
    }
    template<typename T>
    T value(size_type idx, const T& default_value) const {
        if (is_array()) {
            auto& arr = std::get<array_t>(data);
            if (idx < arr.size()) return arr[idx].template get<T>();
        }
        return default_value;
    }
    template<typename T>
    T value(size_type idx, T&& default_value) const {
        if (is_array()) {
            auto& arr = std::get<array_t>(data);
            if (idx < arr.size()) return arr[idx].template get<T>();
        }
        return std::move(default_value);
    }

    // 查询
    bool contains(const std::string& key) const { return is_object() && std::get<object_t>(data).contains(key); }
    bool contains(size_type idx) const { return is_array() && idx < std::get<array_t>(data).size(); }

    iterator find(const std::string& key) {
        if (!is_object()) return end();
        auto& obj = std::get<object_t>(data);
        auto it = obj.find(key);
        if (it == obj.end()) return end();
        return iterator(this, static_cast<size_type>(it - obj.begin()));
    }
    const_iterator find(const std::string& key) const {
        if (!is_object()) return end();
        auto& obj = std::get<object_t>(data);
        auto it = obj.find(key);
        if (it == obj.end()) return end();
        return const_iterator(this, static_cast<size_type>(it - obj.begin()));
    }

    // ==========================================================================
    // 容器修改
    // ==========================================================================
    void push_back(const JSONNode& val) {
        if (is_null()) data = array_t{};
        if (!is_array()) throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 array（push_back）");
        std::get<array_t>(data).push_back(val);
    }
    void push_back(JSONNode&& val) {
        if (is_null()) data = array_t{};
        if (!is_array()) throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 array（push_back）");
        std::get<array_t>(data).push_back(std::move(val));
    }
    void push_back(std::initializer_list<JSONNode> init) {
        // 对象 + {key, value} 对 → 添加键值对；否则作为单个数组元素追加
        if (is_object()) {
            if (init.size() == 2) {
                auto it = init.begin();
                if (it->is_string()) {
                    std::string key = it->get_ref<const std::string&>();
                    emplace(std::move(key), *(it + 1));
                    return;
                }
            }
            throw type_error("[JSON运行时错误]:对象 push_back 需要 {key, value} 形式的 initializer_list");
        }
        if (is_null()) data = array_t{};
        if (!is_array()) throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 array（push_back）");
        std::get<array_t>(data).push_back(JSONNode(init));
    }

    template<typename... Args>
    JSONNode& emplace_back(Args&&... args) {
        if (is_null()) data = array_t{};
        if (!is_array()) throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 array（emplace_back）");
        auto& arr = std::get<array_t>(data);
        arr.emplace_back(std::forward<Args>(args)...);
        return arr.back();
    }

    iterator insert(const_iterator pos, const JSONNode& val) {
        if (!is_array()) throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 array（insert）");
        auto& arr = std::get<array_t>(data);
        size_type idx = pos.index();
        arr.insert(arr.begin() + static_cast<difference_type>(idx), val);
        return iterator(this, idx);
    }
    iterator insert(const_iterator pos, JSONNode&& val) {
        if (!is_array()) throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 array（insert）");
        auto& arr = std::get<array_t>(data);
        size_type idx = pos.index();
        arr.insert(arr.begin() + static_cast<difference_type>(idx), std::move(val));
        return iterator(this, idx);
    }

    iterator erase(const_iterator pos) {
        if (is_array()) {
            auto& arr = std::get<array_t>(data);
            size_type idx = pos.index();
            if (idx >= arr.size()) throw out_of_range("[JSON运行时错误]:erase 越界");
            arr.erase(arr.begin() + static_cast<difference_type>(idx));
            return iterator(this, idx);
        }
        if (is_object()) {
            auto& obj = std::get<object_t>(data);
            size_type idx = pos.index();
            if (idx >= obj.size()) throw out_of_range("[JSON运行时错误]:erase 越界");
            obj.erase(obj.values().cbegin() + static_cast<difference_type>(idx));
            return iterator(this, idx);
        }
        throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 array/object（erase）");
    }
    size_type erase(const std::string& key) {
        if (!is_object()) throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 object（erase）");
        return std::get<object_t>(data).erase(key);
    }

    template<typename... Args>
    JSONNode& emplace(const std::string& key, Args&&... args) {
        if (is_null()) data = object_t{};
        if (!is_object()) throw type_error("[JSON运行时错误]:该节点内储存的数据为 " + std::string(type_name()) + " 而不是 object（emplace）");
        auto& obj = std::get<object_t>(data);
        auto result = obj.emplace(key, std::forward<Args>(args)...);
        return result.first->second;
    }

    void clear() {
        if (is_array()) std::get<array_t>(data).clear();
        else if (is_object()) std::get<object_t>(data).clear();
    }

    // 容量：null=0，标量=1，容器=元素数
    size_type size() const {
        switch (type()) {
            case value_t::null: return 0;
            case value_t::array: return std::get<array_t>(data).size();
            case value_t::object: return std::get<object_t>(data).size();
            default: return 1;
        }
    }
    bool empty() const { return size() == 0; }
    size_type max_size() const {
        if (is_array()) return std::get<array_t>(data).max_size();
        if (is_object()) return std::get<object_t>(data).max_size();
        return 1;
    }

    JSONNode& front() {
        if (is_array()) {
            auto& arr = std::get<array_t>(data);
            if (arr.empty()) throw out_of_range("[JSON运行时错误]:front() 作用于空数组");
            return arr.front();
        }
        if (is_object()) {
            auto& obj = std::get<object_t>(data);
            if (obj.empty()) throw out_of_range("[JSON运行时错误]:front() 作用于空对象");
            return obj.values().front().second;
        }
        throw type_error("[JSON运行时错误]:front() 需要 array/object");
    }
    const JSONNode& front() const {
        if (is_array()) {
            auto& arr = std::get<array_t>(data);
            if (arr.empty()) throw out_of_range("[JSON运行时错误]:front() 作用于空数组");
            return arr.front();
        }
        if (is_object()) {
            auto& obj = std::get<object_t>(data);
            if (obj.empty()) throw out_of_range("[JSON运行时错误]:front() 作用于空对象");
            return obj.values().front().second;
        }
        throw type_error("[JSON运行时错误]:front() 需要 array/object");
    }
    JSONNode& back() {
        if (is_array()) {
            auto& arr = std::get<array_t>(data);
            if (arr.empty()) throw out_of_range("[JSON运行时错误]:back() 作用于空数组");
            return arr.back();
        }
        if (is_object()) {
            auto& obj = std::get<object_t>(data);
            if (obj.empty()) throw out_of_range("[JSON运行时错误]:back() 作用于空对象");
            return obj.values().back().second;
        }
        throw type_error("[JSON运行时错误]:back() 需要 array/object");
    }
    const JSONNode& back() const {
        if (is_array()) {
            auto& arr = std::get<array_t>(data);
            if (arr.empty()) throw out_of_range("[JSON运行时错误]:back() 作用于空数组");
            return arr.back();
        }
        if (is_object()) {
            auto& obj = std::get<object_t>(data);
            if (obj.empty()) throw out_of_range("[JSON运行时错误]:back() 作用于空对象");
            return obj.values().back().second;
        }
        throw type_error("[JSON运行时错误]:back() 需要 array/object");
    }

    // ==========================================================================
    // 迭代
    // ==========================================================================
    iterator begin() {
        if (!is_array() && !is_object()) throw type_error("[JSON运行时错误]:begin() 需要 array/object");
        return iterator(this, 0);
    }
    const_iterator begin() const {
        if (!is_array() && !is_object()) throw type_error("[JSON运行时错误]:begin() 需要 array/object");
        return const_iterator(this, 0);
    }
    const_iterator cbegin() const { return begin(); }
    iterator end() {
        if (!is_array() && !is_object()) throw type_error("[JSON运行时错误]:end() 需要 array/object");
        return iterator(this, size());
    }
    const_iterator end() const {
        if (!is_array() && !is_object()) throw type_error("[JSON运行时错误]:end() 需要 array/object");
        return const_iterator(this, size());
    }
    const_iterator cend() const { return end(); }
    reverse_iterator rbegin() { return reverse_iterator(end()); }
    const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
    const_reverse_iterator crbegin() const { return const_reverse_iterator(cend()); }
    reverse_iterator rend() { return reverse_iterator(begin()); }
    const_reverse_iterator rend() const { return const_reverse_iterator(begin()); }
    const_reverse_iterator crend() const { return const_reverse_iterator(cbegin()); }

    iteration_proxy_t<JSONNode> items();
    iteration_proxy_t<const JSONNode> items() const;

    // ==========================================================================
    // 类型转换
    // ==========================================================================
    template<typename T>
    T get() const {
        T result{};
        from_json(*this, result);
        return result;
    }
    template<typename T>
    void get_to(T& v) const {
        from_json(*this, v);
    }

    // ==========================================================================
    // 比较（深层相等 / 全序）
    // ==========================================================================
    static bool integer_equal(std::int64_t a, std::uint64_t b) {
        return a >= 0 && static_cast<std::uint64_t>(a) == b;
    }
    long double to_long_double() const {
        if (is_number_integer()) return static_cast<long double>(std::get<std::int64_t>(data));
        if (is_number_unsigned()) return static_cast<long double>(std::get<std::uint64_t>(data));
        return static_cast<long double>(std::get<double>(data));
    }
    bool number_equal(const JSONNode& r) const {
        if (is_number_integer() && r.is_number_integer())
            return std::get<std::int64_t>(data) == std::get<std::int64_t>(r.data);
        if (is_number_unsigned() && r.is_number_unsigned())
            return std::get<std::uint64_t>(data) == std::get<std::uint64_t>(r.data);
        if (is_number_integer() && r.is_number_unsigned())
            return integer_equal(std::get<std::int64_t>(data), std::get<std::uint64_t>(r.data));
        if (is_number_unsigned() && r.is_number_integer())
            return r.number_equal(*this);
        return to_long_double() == r.to_long_double();
    }
    bool operator==(const JSONNode& r) const {
        if (is_null() && r.is_null()) return true;
        if (is_boolean() && r.is_boolean()) return std::get<bool>(data) == std::get<bool>(r.data);
        if (is_number() && r.is_number()) return number_equal(r);
        if (is_string() && r.is_string()) return std::get<std::string>(data) == std::get<std::string>(r.data);
        if (is_array() && r.is_array()) {
            const auto& a = std::get<array_t>(data);
            const auto& b = std::get<array_t>(r.data);
            if (a.size() != b.size()) return false;
            for (size_type i = 0; i < a.size(); ++i) if (!(a[i] == b[i])) return false;
            return true;
        }
        if (is_object() && r.is_object()) {
            const auto& a = std::get<object_t>(data);
            const auto& b = std::get<object_t>(r.data);
            if (a.size() != b.size()) return false;
            for (const auto& kv : a) {
                auto it = b.find(kv.first);
                if (it == b.end() || !(kv.second == it->second)) return false;
            }
            return true;
        }
        return false;
    }
    bool operator!=(const JSONNode& r) const { return !(*this == r); }

    int type_rank() const noexcept {
        switch (type()) {
            case value_t::null: return 0;
            case value_t::boolean: return 1;
            case value_t::number_integer:
            case value_t::number_unsigned:
            case value_t::number_float: return 2;
            case value_t::string: return 3;
            case value_t::array: return 4;
            case value_t::object: return 5;
        }
        return 6;
    }
    bool number_less(const JSONNode& r) const {
        if (is_number_integer() && r.is_number_integer())
            return std::get<std::int64_t>(data) < std::get<std::int64_t>(r.data);
        if (is_number_unsigned() && r.is_number_unsigned())
            return std::get<std::uint64_t>(data) < std::get<std::uint64_t>(r.data);
        if (is_number_integer() && r.is_number_unsigned()) {
            std::int64_t a = std::get<std::int64_t>(data);
            std::uint64_t b = std::get<std::uint64_t>(r.data);
            if (a < 0) return true;
            return static_cast<std::uint64_t>(a) < b;
        }
        if (is_number_unsigned() && r.is_number_integer()) {
            std::uint64_t a = std::get<std::uint64_t>(data);
            std::int64_t b = std::get<std::int64_t>(r.data);
            if (b < 0) return false;
            return a < static_cast<std::uint64_t>(b);
        }
        return to_long_double() < r.to_long_double();
    }
    bool operator<(const JSONNode& r) const {
        if (is_number() && r.is_number()) return number_less(r);
        if (type() != r.type()) return type_rank() < r.type_rank();
        switch (type()) {
            case value_t::null: return false;
            case value_t::boolean: return !std::get<bool>(data) && std::get<bool>(r.data);
            case value_t::number_integer:
                return std::get<std::int64_t>(data) < std::get<std::int64_t>(r.data);
            case value_t::number_unsigned:
                return std::get<std::uint64_t>(data) < std::get<std::uint64_t>(r.data);
            case value_t::number_float:
                return std::get<double>(data) < std::get<double>(r.data);
            case value_t::string:
                return std::get<std::string>(data) < std::get<std::string>(r.data);
            case value_t::array: {
                const auto& a = std::get<array_t>(data);
                const auto& b = std::get<array_t>(r.data);
                for (size_type i = 0; i < a.size() && i < b.size(); ++i) {
                    if (a[i] < b[i]) return true;
                    if (b[i] < a[i]) return false;
                }
                return a.size() < b.size();
            }
            case value_t::object: {
                const auto& a = std::get<object_t>(data);
                const auto& b = std::get<object_t>(r.data);
                auto ia = a.values().begin();
                auto ib = b.values().begin();
                for (; ia != a.values().end() && ib != b.values().end(); ++ia, ++ib) {
                    if (ia->first != ib->first) return ia->first < ib->first;
                    if (ia->second < ib->second) return true;
                    if (ib->second < ia->second) return false;
                }
                return a.size() < b.size();
            }
        }
        return false;
    }
    bool operator>(const JSONNode& r) const { return r < *this; }
    bool operator<=(const JSONNode& r) const { return !(r < *this); }
    bool operator>=(const JSONNode& r) const { return !(*this < r); }

    // ==========================================================================
    // 序列化与解析
    // ==========================================================================
    std::string dump(int indent = -1, char indent_char = ' ') const {
        std::string out;
        dump_impl(out, 0, indent, indent_char);
        return out;
    }

    //等效于 dump(-1)，满足不同编码习惯
    std::string stringify(){
        return dump(-1);
    }

    //等效于 dump(4)，满足不同编码习惯
    std::string stringify_with_format() {
        return dump(4);
    }

    static JSONNode parse(std::string_view str, json_standard standard = json_standard::rfc8259);
    static JSONNode parse(std::string_view str, bool allow_jsonc);
    static JSONNode parse(std::istream& is, json_standard standard = json_standard::rfc8259);
    static JSONNode load_file(const std::string& path, json_standard standard = json_standard::rfc8259);
    void save_file(const std::string& path, int indent = 4) const;

    // ==========================================================================
    // 工厂
    // ==========================================================================
    static JSONNode object() { return JSONNode(object_t{}); }
    static JSONNode object(std::initializer_list<JSONNode> init) {
        JSONNode j(object_t{});
        auto& obj = j.get_ref<object_t&>();
        for (const auto& el : init) {
            if (!(el.is_array() && el.size() == 2 && el[0].is_string()))
                throw type_error("[JSON运行时错误]:无法从 initializer_list 创建对象");
            obj.emplace(el[0].get_ref<const std::string&>(), el[1]);
        }
        return j;
    }
    static JSONNode array(std::initializer_list<JSONNode> init = {}) {
        JSONNode j(array_t{});
        auto& arr = j.get_ref<array_t&>();
        for (const auto& el : init) arr.push_back(el);
        return j;
    }

    // ==========================================================================
    // 其它
    // ==========================================================================
    void swap(JSONNode& other) noexcept { data.swap(other.data); }

    JSONNode& operator+=(const JSONNode& v) { push_back(v); return *this; }
    JSONNode& operator+=(JSONNode&& v) { push_back(std::move(v)); return *this; }
    JSONNode& operator+=(std::initializer_list<JSONNode> init) { push_back(init); return *this; }
};

// ============================================================================
// 内部解析器（递归下降；在 JSONNode 定义之后实现，因为需要其完整 API）
// ============================================================================
namespace detail {

struct parser_state {
    const char* cur;
    const char* end;
    parse_position pos{};
    bool allow_jsonc;
};

inline void advance(parser_state& s, const char* what) {
    if (s.cur >= s.end) throw parse_error(s.pos, std::string(what));
    if (*s.cur == '\n') { s.pos.line++; s.pos.column = 1; }
    else s.pos.column++;
    ++s.cur;
}
inline bool at_end(const parser_state& s) { return s.cur >= s.end; }
inline char peek(const parser_state& s) { return at_end(s) ? '\0' : *s.cur; }

inline void skip_ws(parser_state& s) {
    for (;;) {
        if (at_end(s)) return;
        char c = *s.cur;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(s, "意外的输入结束");
        } else if (s.allow_jsonc && c == '/') {
            if (s.cur + 1 < s.end && s.cur[1] == '/') {
                // 行注释
                while (!at_end(s) && *s.cur != '\n') advance(s, "意外的输入结束");
            } else if (s.cur + 1 < s.end && s.cur[1] == '*') {
                // 块注释
                advance(s, "意外的输入结束");
                advance(s, "意外的输入结束");
                bool closed = false;
                while (!at_end(s)) {
                    if (*s.cur == '*' && s.cur + 1 < s.end && s.cur[1] == '/') {
                        advance(s, "意外的输入结束");
                        advance(s, "意外的输入结束");
                        closed = true;
                        break;
                    }
                    advance(s, "意外的输入结束");
                }
                if (!closed) throw parse_error(s.pos, "未闭合的块注释");
            } else {
                throw parse_error(s.pos, "JSONC 中不允许孤立的 '/'");
            }
        } else {
            return;
        }
    }
}

inline uint32_t parse_hex4(parser_state& s) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        if (at_end(s)) throw parse_error(s.pos, "\\u 转义不完整");
        char c = *s.cur;
        v <<= 4;
        if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
        else throw parse_error(s.pos, "\\u 转义中出现非法十六进制字符");
        advance(s, "\\u 转义不完整");
    }
    return v;
}

// 解析 JSON 字符串（含 \uXXXX 与代理对），返回解码后的内容
inline std::string parse_string(parser_state& s) {
    if (peek(s) != '"') throw parse_error(s.pos, "应出现字符串");
    advance(s, "字符串未闭合");  // 吃掉 "
    std::string out;
    for (;;) {
        if (at_end(s)) throw parse_error(s.pos, "字符串未闭合");
        char c = *s.cur;
        if (c == '"') { advance(s, "字符串未闭合"); return out; }
        if (c == '\\') {
            advance(s, "字符串未闭合");
            if (at_end(s)) throw parse_error(s.pos, "字符串未闭合");
            char e = *s.cur;
            switch (e) {
                case '"': out.push_back('"'); advance(s, "字符串未闭合"); break;
                case '\\': out.push_back('\\'); advance(s, "字符串未闭合"); break;
                case '/': out.push_back('/'); advance(s, "字符串未闭合"); break;
                case 'b': out.push_back('\b'); advance(s, "字符串未闭合"); break;
                case 'f': out.push_back('\f'); advance(s, "字符串未闭合"); break;
                case 'n': out.push_back('\n'); advance(s, "字符串未闭合"); break;
                case 'r': out.push_back('\r'); advance(s, "字符串未闭合"); break;
                case 't': out.push_back('\t'); advance(s, "字符串未闭合"); break;
                case 'u': {
                    advance(s, "\\u 转义不完整");
                    uint32_t cp = parse_hex4(s);
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        // 高位代理：期待紧随的低位代理
                        if (at_end(s) || *s.cur != '\\' || s.cur + 1 >= s.end || s.cur[1] != 'u')
                            throw parse_error(s.pos, "缺少代理对低位");
                        advance(s, "缺少代理对低位");
                        advance(s, "缺少代理对低位");
                        uint32_t low = parse_hex4(s);
                        if (low < 0xDC00 || low > 0xDFFF) throw parse_error(s.pos, "无效的代理对低位");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        throw parse_error(s.pos, "孤立的低位代理");
                    }
                    out += code_point_to_utf8(cp);
                    break;
                }
                default:
                    throw parse_error(s.pos, std::string("非法转义字符: \\") + e);
            }
        } else {
            unsigned char uc = static_cast<unsigned char>(c);
            if (uc < 0x20) throw parse_error(s.pos, "字符串中包含非法控制字符");
            out.push_back(c);
            advance(s, "字符串未闭合");
        }
    }
}

inline JSONNode parse_number(parser_state& s) {
    const char* start = s.cur;
    bool is_float = false;
    if (peek(s) == '-') advance(s, "数字格式非法");
    if (at_end(s)) throw parse_error(s.pos, "数字格式非法");
    if (peek(s) == '0') {
        advance(s, "数字格式非法");
    } else if (peek(s) >= '1' && peek(s) <= '9') {
        while (!at_end(s) && peek(s) >= '0' && peek(s) <= '9') advance(s, "数字格式非法");
    } else {
        throw parse_error(s.pos, "数字格式非法");
    }
    if (!at_end(s) && peek(s) == '.') {
        is_float = true;
        advance(s, "数字格式非法");
        if (at_end(s) || !(peek(s) >= '0' && peek(s) <= '9')) throw parse_error(s.pos, "数字格式非法");
        while (!at_end(s) && peek(s) >= '0' && peek(s) <= '9') advance(s, "数字格式非法");
    }
    if (!at_end(s) && (peek(s) == 'e' || peek(s) == 'E')) {
        is_float = true;
        advance(s, "数字格式非法");
        if (!at_end(s) && (peek(s) == '+' || peek(s) == '-')) advance(s, "数字格式非法");
        if (at_end(s) || !(peek(s) >= '0' && peek(s) <= '9')) throw parse_error(s.pos, "数字格式非法");
        while (!at_end(s) && peek(s) >= '0' && peek(s) <= '9') advance(s, "数字格式非法");
    }
    std::string token(start, s.cur);
    if (is_float) {
        double d = 0.0;
        auto res = std::from_chars(start, s.cur, d, std::chars_format::general);
        if (res.ec != std::errc()) throw parse_error(s.pos, "数字格式非法: " + token);
        return JSONNode(d);
    }
    std::int64_t i64 = 0;
    auto r1 = std::from_chars(start, s.cur, i64);
    if (r1.ec == std::errc() && r1.ptr == s.cur) return JSONNode(i64);
    std::uint64_t u64 = 0;
    auto r2 = std::from_chars(start, s.cur, u64);
    if (r2.ec == std::errc() && r2.ptr == s.cur) return JSONNode(u64);
    // 整数溢出 -> 浮点
    double d = 0.0;
    auto r3 = std::from_chars(start, s.cur, d, std::chars_format::general);
    if (r3.ec != std::errc()) throw parse_error(s.pos, "数字格式非法: " + token);
    return JSONNode(d);
}

inline JSONNode parse_literal(parser_state& s) {
    auto match = [&](const char* word) -> bool {
        std::size_t n = std::strlen(word);
        if (static_cast<std::size_t>(s.end - s.cur) >= n && std::memcmp(s.cur, word, n) == 0) {
            for (std::size_t i = 0; i < n; ++i) advance(s, "意外的输入结束");
            return true;
        }
        return false;
    };
    if (match("true")) return JSONNode(true);
    if (match("false")) return JSONNode(false);
    if (match("null")) return JSONNode(nullptr);
    throw parse_error(s.pos, "无法识别的字面量");
}

inline JSONNode parse_value(parser_state& s);

inline JSONNode parse_array(parser_state& s) {
    advance(s, "缺少右方括号");  // [
    JSONNode arr = array_t{};
    auto& vec = arr.get_ref<array_t&>();
    skip_ws(s);
    if (peek(s) == ']') { advance(s, "缺少右方括号"); return arr; }
    for (;;) {
        skip_ws(s);
        vec.push_back(parse_value(s));
        skip_ws(s);
        if (peek(s) == ',') {
            advance(s, "缺少右方括号");
            skip_ws(s);
            if (s.allow_jsonc && peek(s) == ']') { advance(s, "缺少右方括号"); return arr; }  // 尾逗号
            continue;
        }
        if (peek(s) == ']') { advance(s, "缺少右方括号"); return arr; }
        throw parse_error(s.pos, "应出现 ',' 或 ']'");
    }
}

inline JSONNode parse_object(parser_state& s) {
    advance(s, "缺少右大括号");  // {
    JSONNode obj = object_t{};
    auto& o = obj.get_ref<object_t&>();
    skip_ws(s);
    if (peek(s) == '}') { advance(s, "缺少右大括号"); return obj; }
    for (;;) {
        skip_ws(s);
        if (peek(s) != '"') throw parse_error(s.pos, "对象的键应为字符串");
        std::string key = parse_string(s);
        skip_ws(s);
        if (peek(s) != ':') throw parse_error(s.pos, "应出现 ':'");
        advance(s, "缺少数据结构");
        skip_ws(s);
        JSONNode value = parse_value(s);
        // 重复键：后值覆盖（保持原有顺序）
        auto it = o.find(key);
        if (it != o.end()) it->second = std::move(value);
        else o.emplace(key, std::move(value));
        skip_ws(s);
        if (peek(s) == ',') {
            advance(s, "缺少右大括号");
            skip_ws(s);
            if (s.allow_jsonc && peek(s) == '}') { advance(s, "缺少右大括号"); return obj; }  // 尾逗号
            continue;
        }
        if (peek(s) == '}') { advance(s, "缺少右大括号"); return obj; }
        throw parse_error(s.pos, "应出现 ',' 或 '}'");
    }
}

inline JSONNode parse_value(parser_state& s) {
    skip_ws(s);
    if (at_end(s)) throw parse_error(s.pos, "缺少数据结构");
    char c = peek(s);
    if (c == '{') return parse_object(s);
    if (c == '[') return parse_array(s);
    if (c == '"') return JSONNode(parse_string(s));
    if (c == 't' || c == 'f' || c == 'n') return parse_literal(s);
    if (c == '-' || (c >= '0' && c <= '9')) return parse_number(s);
    throw parse_error(s.pos, std::string("意外的字符: ") + c);
}

}  // namespace detail

// ============================================================================
// JSONNode::parse / load_file / save_file（类外定义）
// ============================================================================
inline JSONNode JSONNode::parse(std::string_view str, json_standard standard) {
    if (str.empty()) throw parse_error("传入的字符串是空的");
    detail::parser_state st{ str.data(), str.data() + str.size(), parse_position{}, standard == json_standard::jsonc };
    JSONNode result = detail::parse_value(st);
    detail::skip_ws(st);
    if (!detail::at_end(st)) throw parse_error(st.pos, "解析结束后仍有剩余内容");
    return result;
}
inline JSONNode JSONNode::parse(std::string_view str, bool allow_jsonc) {
    return parse(str, allow_jsonc ? json_standard::jsonc : json_standard::rfc8259);
}
inline JSONNode JSONNode::parse(std::istream& is, json_standard standard) {
    std::stringstream ss;
    ss << is.rdbuf();
    return parse(ss.str(), standard);
}
inline JSONNode JSONNode::load_file(const std::string& path, json_standard standard) {
    FILE* file = detail::open_file_binary_read(path);
    if (!file) throw other_error("[JSONNode::load_file]:无法打开文件: " + path);
    fseek(file, 0, SEEK_END);
    long len = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (len < 0) { fclose(file); throw other_error("[JSONNode::load_file]:读取文件失败: " + path); }
    if (len > 32 * 1024 * 1024) { fclose(file); throw other_error("[JSONNode::load_file]:文件过大: " + path); }
    std::string str;
    str.resize(static_cast<std::size_t>(len));
    fread(&str[0], 1, static_cast<std::size_t>(len), file);
    fclose(file);
    return parse(str, standard);
}
inline void JSONNode::save_file(const std::string& path, int indent) const {
    std::string str = dump(indent);
    FILE* file = detail::open_file_binary_write(path);
    if (!file) throw other_error("[JSONNode::save_file]:无法创建文件: " + path);
    fwrite(str.data(), 1, str.size(), file);
    fclose(file);
}

// ============================================================================
// 迭代代理（items()，支持 for (auto& [k, v] : j.items())）
// ============================================================================
template<typename NodeT>
class iteration_proxy_value_t {
    std::string m_key;
    NodeT* m_value = nullptr;
public:
    iteration_proxy_value_t() = default;
    iteration_proxy_value_t(NodeT* node, std::size_t pos)
        : m_key(node->key_or_index_string(pos)), m_value(&node->element_at(pos)) {}

    const std::string& key() const noexcept { return m_key; }
    NodeT& value() const { return *m_value; }
    operator NodeT&() const { return *m_value; }

    template<std::size_t I>
    decltype(auto) get() const& {
        if constexpr (I == 0) return m_key;
        else return *m_value;
    }
};

template<typename NodeT>
class iteration_proxy_iterator_t {
    NodeT* m_node = nullptr;
    std::size_t m_pos = 0;
    iteration_proxy_value_t<NodeT> m_proxy;

    void rebuild() {
        if (m_node && m_pos < m_node->size()) {
            m_proxy = iteration_proxy_value_t<NodeT>(m_node, m_pos);
        }
    }

public:
    iteration_proxy_iterator_t() = default;
    iteration_proxy_iterator_t(NodeT* node, std::size_t pos) : m_node(node), m_pos(pos) {
        rebuild();
    }
    iteration_proxy_value_t<NodeT>& operator*() { return m_proxy; }
    const iteration_proxy_value_t<NodeT>& operator*() const { return m_proxy; }
    iteration_proxy_value_t<NodeT>* operator->() { return &m_proxy; }
    iteration_proxy_iterator_t& operator++() {
        ++m_pos;
        rebuild();
        return *this;
    }
    iteration_proxy_iterator_t operator++(int) { auto t = *this; ++(*this); return t; }
    bool operator==(const iteration_proxy_iterator_t& o) const { return m_node == o.m_node && m_pos == o.m_pos; }
    bool operator!=(const iteration_proxy_iterator_t& o) const { return !(*this == o); }
};

template<typename NodeT>
class iteration_proxy_t {
    NodeT& m_node;
public:
    explicit iteration_proxy_t(NodeT& n) : m_node(n) {}
    iteration_proxy_iterator_t<NodeT> begin() const { return iteration_proxy_iterator_t<NodeT>(&m_node, 0); }
    iteration_proxy_iterator_t<NodeT> end() const { return iteration_proxy_iterator_t<NodeT>(&m_node, m_node.size()); }
};

using iteration_proxy = iteration_proxy_t<JSONNode>;
using const_iteration_proxy = iteration_proxy_t<const JSONNode>;

inline iteration_proxy_t<JSONNode> JSONNode::items() {
    return iteration_proxy_t<JSONNode>(*this);
}
inline iteration_proxy_t<const JSONNode> JSONNode::items() const {
    return iteration_proxy_t<const JSONNode>(*this);
}

// 结构化绑定辅助（同时提供成员 get 与 ADL 自由函数 get，保证 C++17/20 兼容）
template<std::size_t I, typename NodeT>
decltype(auto) get(iteration_proxy_value_t<NodeT>& v) { return v.template get<I>(); }
template<std::size_t I, typename NodeT>
decltype(auto) get(const iteration_proxy_value_t<NodeT>& v) { return v.template get<I>(); }

// ============================================================================
// from_json：JSONNode -> C++ 值
// ============================================================================
inline void from_json(const JSONNode& j, bool& v) { v = j.get_boolean(); }
inline void from_json(const JSONNode& j, double& v) { v = j.get_double(); }
inline void from_json(const JSONNode& j, float& v) { v = static_cast<float>(j.get_double()); }
inline void from_json(const JSONNode& j, long double& v) { v = static_cast<long double>(j.get_double()); }
inline void from_json(const JSONNode& j, std::string& v) { v = j.get_string_ref(); }
inline void from_json(const JSONNode& j, std::string_view& v) { v = j.get_string_ref(); }
inline void from_json(const JSONNode& j, std::nullptr_t&) {
    if (!j.is_null()) throw type_error("[JSON运行时错误]:该节点不是 null");
}
inline void from_json(const JSONNode& j, JSONNode& v) { v = j; }

template<typename T, typename std::enable_if<
    std::is_integral<T>::value && std::is_signed<T>::value && !std::is_same<T, bool>::value, int>::type = 0>
inline void from_json(const JSONNode& j, T& v) {
    std::int64_t s = j.get_integer_signed();
    if (s < static_cast<std::int64_t>(std::numeric_limits<T>::min()) ||
        s > static_cast<std::int64_t>(std::numeric_limits<T>::max())) {
        throw out_of_range("[JSON运行时错误]:整数超出目标类型范围");
    }
    v = static_cast<T>(s);
}
template<typename T, typename std::enable_if<
    std::is_integral<T>::value && std::is_unsigned<T>::value, int>::type = 0>
inline void from_json(const JSONNode& j, T& v) {
    std::uint64_t u = j.get_integer_unsigned();
    if (u > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
        throw out_of_range("[JSON运行时错误]:整数超出目标类型范围");
    }
    v = static_cast<T>(u);
}

template<typename T>
inline void from_json(const JSONNode& j, std::optional<T>& v) {
    if (j.is_null()) v = std::nullopt;
    else v = j.get<T>();
}

template<typename T1, typename T2>
inline void from_json(const JSONNode& j, std::pair<T1, T2>& v) {
    if (!j.is_array() || j.size() != 2) throw type_error("[JSON运行时错误]:pair 需要两个元素的数组");
    v = std::make_pair(j[0].get<T1>(), j[1].get<T2>());
}

namespace detail {
template<typename Tuple, std::size_t... I>
inline void from_json_tuple(const JSONNode& j, Tuple& v, std::index_sequence<I...>) {
    ((std::get<I>(v) = j[I].get<typename std::tuple_element<I, Tuple>::type>()), ...);
}
template<typename Tuple, std::size_t... I>
inline void to_json_tuple(JSONNode& j, const Tuple& v, std::index_sequence<I...>) {
    (j.emplace_back(std::get<I>(v)), ...);
}
}  // namespace detail

template<typename... Ts>
inline void from_json(const JSONNode& j, std::tuple<Ts...>& v) {
    if (!j.is_array() || j.size() != sizeof...(Ts)) throw type_error("[JSON运行时错误]:tuple 长度与数组不符");
    detail::from_json_tuple(j, v, std::index_sequence_for<Ts...>{});
}

// 序列容器（push_back）
template<typename T, typename std::enable_if<
    detail::is_iterable_container<T>::value && !detail::is_string_like<T>::value &&
    !detail::is_map_like<T>::value && !std::is_same<T, JSONNode>::value &&
    detail::has_push_back<T>::value, int>::type = 0>
inline void from_json(const JSONNode& j, T& v) {
    if (!j.is_array()) throw type_error("[JSON运行时错误]:该节点不是数组");
    v.clear();
    for (const auto& el : j) v.push_back(el.get<typename T::value_type>());
}
// 集合类（insert）
template<typename T, typename std::enable_if<
    detail::is_iterable_container<T>::value && !detail::is_string_like<T>::value &&
    !detail::is_map_like<T>::value && !std::is_same<T, JSONNode>::value &&
    !detail::has_push_back<T>::value && detail::has_insert_value<T>::value, int>::type = 0>
inline void from_json(const JSONNode& j, T& v) {
    if (!j.is_array()) throw type_error("[JSON运行时错误]:该节点不是数组");
    v.clear();
    for (const auto& el : j) v.insert(el.get<typename T::value_type>());
}
// map 类容器
template<typename T, typename std::enable_if<
    detail::is_map_like<T>::value, int>::type = 0>
inline void from_json(const JSONNode& j, T& v) {
    if (!j.is_object()) throw type_error("[JSON运行时错误]:该节点不是对象");
    v.clear();
    for (auto it = j.begin(); it != j.end(); ++it) {
        v.emplace(it.key(), it.value().get<typename T::mapped_type>());
    }
}

// ============================================================================
// to_json：C++ 值 -> JSONNode
// ============================================================================
inline void to_json(JSONNode& j, std::nullptr_t) { j = nullptr; }
inline void to_json(JSONNode& j, bool b) { j = b; }
inline void to_json(JSONNode& j, double d) { j = d; }
inline void to_json(JSONNode& j, float f) { j = static_cast<double>(f); }
inline void to_json(JSONNode& j, long double d) { j = static_cast<double>(d); }
inline void to_json(JSONNode& j, const std::string& s) { j = s; }
inline void to_json(JSONNode& j, std::string&& s) { j = std::move(s); }
inline void to_json(JSONNode& j, std::string_view s) { j = s; }
inline void to_json(JSONNode& j, const char* s) { j = s; }
inline void to_json(JSONNode& j, const JSONNode& n) { j = n; }
inline void to_json(JSONNode& j, std::initializer_list<JSONNode> init) { j = JSONNode(init); }

template<typename T, typename std::enable_if<
    std::is_integral<T>::value && std::is_signed<T>::value && !std::is_same<T, bool>::value, int>::type = 0>
inline void to_json(JSONNode& j, T v) { j = static_cast<std::int64_t>(v); }
template<typename T, typename std::enable_if<
    std::is_integral<T>::value && std::is_unsigned<T>::value, int>::type = 0>
inline void to_json(JSONNode& j, T v) { j = static_cast<std::uint64_t>(v); }

template<typename T>
inline void to_json(JSONNode& j, const std::optional<T>& v) {
    if (v.has_value()) to_json(j, *v);
    else j = nullptr;
}
template<typename T1, typename T2>
inline void to_json(JSONNode& j, const std::pair<T1, T2>& v) {
    j = array_t{};
    auto& arr = j.get_ref<array_t&>();
    arr.emplace_back(v.first);
    arr.emplace_back(v.second);
}
template<typename... Ts>
inline void to_json(JSONNode& j, const std::tuple<Ts...>& v) {
    j = array_t{};
    detail::to_json_tuple(j, v, std::index_sequence_for<Ts...>{});
}
// 键值容器 -> 对象
template<typename T, typename std::enable_if<
    detail::is_map_like<T>::value, int>::type = 0>
inline void to_json(JSONNode& j, const T& m) {
    j = object_t{};
    for (const auto& kv : m) {
        j.emplace(static_cast<std::string>(kv.first), kv.second);
    }
}
// 其它可迭代容器 -> 数组
template<typename T, typename std::enable_if<
    detail::is_iterable_container<T>::value && !detail::is_string_like<T>::value &&
    !detail::is_map_like<T>::value && !std::is_same<T, JSONNode>::value, int>::type = 0>
inline void to_json(JSONNode& j, const T& v) {
    j = array_t{};
    for (const auto& el : v) j.emplace_back(el);
}

// ============================================================================
// 流操作符与 swap
// ============================================================================
inline std::ostream& operator<<(std::ostream& os, const JSONNode& j) {
    os << j.dump(-1);
    return os;
}
inline std::istream& operator>>(std::istream& is, JSONNode& j) {
    j = JSONNode::parse(is);
    return is;
}
inline void swap(JSONNode& a, JSONNode& b) noexcept { a.swap(b); }

}  // namespace GL_JSON3

// 结构化绑定支持（for (auto& [k, v] : j.items())）
namespace std {
template<typename NodeT>
struct tuple_size<GL_JSON3::iteration_proxy_value_t<NodeT>> : std::integral_constant<std::size_t, 2> {};

template<std::size_t I, typename NodeT>
struct tuple_element<I, GL_JSON3::iteration_proxy_value_t<NodeT>> {
    using type = std::conditional_t<I == 0, const std::string, NodeT&>;
};
}  // namespace std

#endif
