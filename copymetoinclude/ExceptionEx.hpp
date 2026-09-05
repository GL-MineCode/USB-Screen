#ifndef __INC_EXCEPTIONEX_
#define __INC_EXCEPTIONEX_

#include <cstdarg>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <source_location>

namespace exceptionex_internal {
    inline std::string cpp_sformat_valist(const char* format, va_list args) {
        va_list args_copy;
        va_copy(args_copy, args);
        int len = vsnprintf(nullptr, 0, format, args_copy);
        va_end(args_copy);
        if (len < 0) return std::string();
        std::string result(len, '\0');
        vsnprintf(result.data(), len + 1, format, args);
        return result;
    }
}

class ExceptionEx : public std::exception {

    protected:
    std::source_location location;
    std::string message;

    mutable std::string what_cache;

    public:
    ExceptionEx(const std::string& msg, const std::source_location& loc = std::source_location::current())
        : location(loc), message(msg) {}

    const char* what() const noexcept override {
        // 缓存格式化结果，避免返回临时 string 的悬垂指针
        if (what_cache.empty()) {
            what_cache = '[' + std::string(location.file_name()) + ':' + std::to_string(location.line()) + ']' + message;
        }
        return what_cache.c_str();
    }
};

class RuntimeErrorEx_impl : public ExceptionEx {

    public:
    RuntimeErrorEx_impl(const std::source_location& loc,const std::string& msg,...)
        : ExceptionEx(std::string(), loc) {
        va_list args;
        va_start(args, msg);
        message = exceptionex_internal::cpp_sformat_valist(msg.c_str(), args);
        va_end(args);
    }
};

#define RuntimeErrorEx(msg, ...) RuntimeErrorEx_impl(std::source_location::current(), msg __VA_OPT__(,) __VA_ARGS__)

#endif