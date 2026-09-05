#ifndef __INC_GL_LOG_
#define __INC_GL_LOG_

#include<iostream>
#include<cstdio>
#include<thread>
#include<mutex>
#include<source_location>
#include"CVTS.hpp"
#include"GL_DateTime.hpp"

enum GL_LogLevel : int {
	GL_LOGLEVEL_DEBUG = 1,
	GL_LOGLEVEL_MESSAGE = 2,
	GL_LOGLEVEL_INFO = 3,
	GL_LOGLEVEL_WARNING = 4,
	GL_LOGLEVEL_ERROR = 5,
};

enum GL_LogLevelFilter : int {
	GL_LOGLEVEL_FILTER_ALL = 0,
	GL_LOGLEVEL_FILTER_ABOVE_DEBUG = 1,
	GL_LOGLEVEL_FILTER_ABOVE_MESSAGE = 2,
	GL_LOGLEVEL_FILTER_ABOVE_INFO = 3,
	GL_LOGLEVEL_FILTER_ONLY_ERROR = 4,
	GL_LOGLEVEL_FILTER_NONE = 5,
};

std::mutex __log_mtx;
FILE* __log_file = NULL;
bool __keep_cvts = false;
int __log_level_filter_in_console = 0;
int __log_level_filter_in_logfile = 0;

void GL_LogSetConsoleLogLevelFilter(GL_LogLevelFilter filter);

void GL_LogSetFileLogLevelFilter(GL_LogLevelFilter filter);

bool GL_LogSetFile(const char* path,bool keep_cvts = false,const char* mode = "w");

void GL_LogCloseFile();

int GL_Log_impl(std::source_location loc, GL_LogLevel logLevel, const char* format, ...);

bool GL_InitConsole();

bool GL_InitConsole(){
	SetConsoleOutputCP(65001);
	return CVTS::InitCurrentHandle();
}

void GL_LogSetConsoleLogLevelFilter(GL_LogLevelFilter filter){
	__log_level_filter_in_console = filter;
}

void GL_LogSetFileLogLevelFilter(GL_LogLevelFilter filter){
	__log_level_filter_in_logfile = filter;
}

bool GL_LogSetFile(const char* path,bool keep_cvts,const char* mode){
	FILE* file = fopen(path,mode);
	if(!file) return false;
	__log_file = file;
	__keep_cvts = keep_cvts;
	return true;
}

void GL_LogCloseFile(){
	fclose(__log_file);
	__log_file = NULL;
}

ULONGLONG __program_startup_time = GetTickCount64();

int GL_Log_impl(std::source_location loc, GL_LogLevel logLevel, const char* format, ...) {
	if(logLevel <= __log_level_filter_in_console && logLevel <= __log_level_filter_in_logfile){
		return 0;
	}
	std::string surfix;
	CVTS::CVTS_Color tcolor;
	surfix.push_back('[');
	//surfix.push_back('t');
	//surfix.push_back(':');
	//surfix += std::to_string(GetTickCount64() - __program_startup_time);
	surfix += DateTime::Now().ToString();
	surfix.push_back(',');
	surfix += "thread:";

	//获取线程ID
	int thread_id = -1;
	#ifdef _WIN32
	thread_id = GetCurrentThreadId();
	#elif __linux__
	thread_id = syscall(SYS_gettid);
	#elif __APPLE__
	thread_id = pthread_mach_thread_np(pthread_self());
	#elif __unix__
	thread_id = syscall(SYS_gettid);
	#else
	thread_id = std::hash<std::thread::id>{}(std::this_thread::get_id());
	#endif

	surfix += std::to_string(thread_id);
	surfix.push_back(',');
	surfix += "source:(";
	surfix += loc.file_name();
	surfix.push_back(':');
	surfix += std::to_string(loc.line());
	// surfix.push_back(',');
	// surfix += std::to_string(loc.column());
	surfix.push_back(')');
	surfix.push_back(']');
	switch (logLevel) {
		case GL_LOGLEVEL_DEBUG:
			tcolor = CVTS::CVTS_Color::CVTS_COLOR_FOREGROUND_CYAN;
			surfix += "[DEBUG]: ";
			break;
		case GL_LOGLEVEL_MESSAGE:
			tcolor = CVTS::CVTS_Color::CVTS_COLOR_FOREGROUND_WHITE;
			surfix += "[MESSAGE]: ";
			break;
		case GL_LOGLEVEL_INFO:
			tcolor = CVTS::CVTS_Color::CVTS_COLOR_FOREGROUND_GREEN;
			surfix += "[INFO]: ";
			break;
		case GL_LOGLEVEL_WARNING:
			tcolor = CVTS::CVTS_Color::CVTS_COLOR_FOREGROUND_YELLOW;
			surfix += "[WARNING]: ";
			break;
		case GL_LOGLEVEL_ERROR:
			tcolor = CVTS::CVTS_Color::CVTS_COLOR_FOREGROUND_RED;
			surfix += "[ERROR]: ";
			break;
		default:
			tcolor = CVTS::CVTS_Color::CVTS_COLOR_FOREGROUND_CYAN;
			surfix += "[UNKNOWN]: ";
			break;
	}
	va_list args;
	va_start(args, format);
	__log_mtx.lock();
	static char buffer[4096];
    int ret = vsnprintf(buffer, sizeof(buffer), format, args);
	if(logLevel > __log_level_filter_in_console){
		CVTS::SetColor(tcolor);
		fputs(surfix.c_str(), stdout);
		fputs(buffer, stdout);
		fputc('\n',stdout);
	}
	if(__log_file && logLevel > __log_level_filter_in_logfile){
		if(__keep_cvts){
			fprintf(__log_file,"\x1b[%dm", tcolor);
		}
		fputs(surfix.c_str(), __log_file);
		fputs(buffer, __log_file);
		fputc('\n',__log_file);
		fflush(__log_file);
	}
	__log_mtx.unlock();
	va_end(args);
	return ret;
}

#define GL_Log(log_level,fmt, ...) \
    GL_Log_impl(std::source_location::current(), log_level, fmt __VA_OPT__(,) __VA_ARGS__)

#define GL_LogError(format, ...) GL_Log(GL_LOGLEVEL_ERROR, format, ##__VA_ARGS__)
#define GL_LogMess(format, ...) GL_Log(GL_LOGLEVEL_MESSAGE, format, ##__VA_ARGS__)
#define GL_LogInfo(format, ...) GL_Log(GL_LOGLEVEL_INFO, format, ##__VA_ARGS__)
#define GL_LogWarn(format, ...) GL_Log(GL_LOGLEVEL_WARNING, format, ##__VA_ARGS__)
#define GL_LogDebug(format, ...) GL_Log(GL_LOGLEVEL_DEBUG, format, ##__VA_ARGS__)

#endif
