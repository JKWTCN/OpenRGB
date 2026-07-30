/*---------------------------------------------------------*\
| LogManager.h                                              |
|                                                           |
|   Manages log file and output to the console              |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <fstream>
#include <mutex>
#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <nlohmann/json.hpp>
#include "filesystem.h"

/*-------------------------------------------------*\
| Common LOG strings                                |
|   This may need to be in it's own .h file         |
\*-------------------------------------------------*/
#define SMBUS_CHECK_DEVICE_MESSAGE_EN   "[%s] Bus %02d is a motherboard and the subvendor matches the one for %s, looking for a device at 0x%02X"
#define SMBUS_CHECK_DEVICE_FAILURE_EN   "[%s] Bus %02d is not a motherboard or the subvendor does not match the one for %s, skipping detection"

#define GPU_DETECT_MESSAGE              "[%s] Found a device match at Bus %02d for Device 0x%04X and SubDevice 0x%04X: %s"

using json = nlohmann::json;

enum
{
    LL_FATAL,       // Critical unrecoverable errors that cause a generalized crash of a module or of the entire app
    LL_ERROR,       // Local errors that abort an operation
    LL_WARNING,     // Local errors that may cause an operation to have an undefined behavior or may have dangerous/unforeseen consequences
    LL_INFO,        // Initialization messages, significant actions and follow-up information
    LL_VERBOSE,     // Tracing of commands and performed actions, usually for debug purposes, comments on the higher priority messages
    LL_DEBUG,       // Deep tracing, "printf-style debugging" alternative, for debug purposes. Such messages should be put all over the code instead of comments
    LL_TRACE,
    LL_DIALOG       // Log messages to be shown in a GUI dialog box
};

typedef struct
{
    unsigned int                    level;
    unsigned int                    line;
    unsigned int                    timestamp;
    std::string                     filename;
    std::string                     text;
} LogMessage;

typedef std::shared_ptr<LogMessage> PLogMessage;

/*---------------------------------------------------------*\
| Callback Types                                            |
\*---------------------------------------------------------*/
typedef void(*LogManagerCallback)(void*, unsigned int, PLogMessage);

/*---------------------------------------------------------*\
| LogManager Update Reason Codes                            |
\*---------------------------------------------------------*/
enum
{
    LOGMANAGER_UPDATE_REASON_LOG_ENTRY,             /* Log entry                        */
    LOGMANAGER_UPDATE_REASON_SHOW_DIALOG,           /* Show dialog                      */
};

class LogManager
{
public:
    LogManager();
    ~LogManager();

    /*-----------------------------------------------------*\
    | LogManager Global Instance Accessor                   |
    \*-----------------------------------------------------*/
    static LogManager*                  get();

    /*-----------------------------------------------------*\
    | Callback Registration                                 |
    \*-----------------------------------------------------*/
    void                                RegisterLogManagerCallback(LogManagerCallback callback, void* receiver);
    void                                UnregisterLogManagerCallback(LogManagerCallback callback, void* receiver);

    /*-----------------------------------------------------*\
    | Configuration                                         |
    \*-----------------------------------------------------*/
    void                                Configure(json config, const filesystem::path& config_dir);
    void                                SetServiceLogDirectory(const filesystem::path& config_dir);
    void                                ReconfigureDailyLog(const filesystem::path& config_dir);

    /*-----------------------------------------------------*\
    | Device-scan log suppression                           |
    \*-----------------------------------------------------*/
    void                                StartSuppressing();
    void                                StopSuppressing(bool flush);

    /*-----------------------------------------------------*\
    | Log Buffer Functions                                  |
    \*-----------------------------------------------------*/
    void                                ClearLogBuffer();
    std::vector<PLogMessage>            GetLogBuffer();

    /*-----------------------------------------------------*\
    | Log Level Functions                                   |
    \*-----------------------------------------------------*/
    unsigned int                        GetLogLevel();
    unsigned int                        GetVerbosity();

    void                                SetLogLevel(unsigned int, bool local_only = false);
    void                                SetVerbosity(unsigned int);

    /*-----------------------------------------------------*\
    | Log Format Functions                                  |
    \*-----------------------------------------------------*/
    void                                SetPrintSource(bool print);

    /*-----------------------------------------------------*\
    | Log Entry Functions                                   |
    \*-----------------------------------------------------*/
    void                                LogEntry(const char* filename, int line, unsigned int level, const char* fmt, ...);
    void                                LogEntryUnsuppressed(const char* filename, int line, unsigned int level, const char* fmt, ...);
    void                                LogEntry_message(PLogMessage message);
    void                                LogEntry_va(const char* filename, int line, unsigned int level, const char* fmt, va_list va);

    /*-----------------------------------------------------*\
    | Log Code String Constants                             |
    \*-----------------------------------------------------*/
    static const char*                  LOG_CODES[];

private:
    /*-----------------------------------------------------*\
    | Static pointer to shared instance of LogManager       |
    \*-----------------------------------------------------*/
    static LogManager*                  instance;

    /*-----------------------------------------------------*\
    | Log Levels                                            |
    \*-----------------------------------------------------*/
    unsigned int                        loglevel;
    unsigned int                        verbosity;

    /*-----------------------------------------------------*\
    | Log Formatting                                        |
    \*-----------------------------------------------------*/
    std::chrono::time_point<std::chrono::steady_clock>
                                        base_clock;
    bool                                log_console_enabled;
    bool                                print_source;

    /*-----------------------------------------------------*\
    | Log Mutexes                                           |
    \*-----------------------------------------------------*/
    std::recursive_mutex                entry_mutex;
    std::mutex                          section_mutex;

    /*-----------------------------------------------------*\
    | Log File Output Stream                                |
    \*-----------------------------------------------------*/
    std::ofstream                       log_stream;

    /*-----------------------------------------------------*\
    | LogManager Callbacks                                  |
    \*-----------------------------------------------------*/
    std::vector<LogManagerCallback>     LogManagerCallbacks;
    std::vector<void *>                 LogManagerCallbackArgs;
    std::mutex                          LogManagerCallbackMutex;

    /*-----------------------------------------------------*\
    | Log Buffers                                           |
    \*-----------------------------------------------------*/
    std::vector<PLogMessage>            temp_messages;
    std::vector<PLogMessage>            all_messages;
    std::vector<PLogMessage>            suppressed_messages;
    bool                                suppress_mode;
    std::thread::id                     suppressing_thread;

    /*-----------------------------------------------------*\
    | Service-mode daily log rotation                       |
    \*-----------------------------------------------------*/
    bool                                log_file_enabled;
    bool                                daily_rollover;
    bool                                service_log_mode;
    bool                                log_has_entries;
    int                                 configured_log_limit;
    int                                 daily_log_limit;
    std::string                         configured_log_template;
    std::string                         daily_basename;
    std::string                         current_log_date;
    filesystem::path                    current_log_path;
    filesystem::path                    log_base_dir;
    filesystem::path                    service_log_dir;

    /*-----------------------------------------------------*\
    | Private Functions                                     |
    \*-----------------------------------------------------*/
    void                                LogFlush();
    void                                OpenDailyLog(const std::string& yyyymmdd);
    void                                RemoveExpiredDailyLogs(const filesystem::path& folder, const std::string& basename, unsigned int retention_days);
    void                                LogEntry_va_internal(const char* filename, int line, unsigned int level, const char* fmt, va_list va, bool bypass_suppression);
    void                                LogEntry_message_internal(PLogMessage message, bool bypass_suppression);
    void                                LogRotate(const filesystem::path& folder, const filesystem::path& templ, std::size_t max_count, const char* timestamp_regex);
};

/*---------------------------------------------------------*\
| Log Macros                                                |
\*---------------------------------------------------------*/
#define LogAppend(level, ...)   LogManager::get()->LogEntry(__FILE__, __LINE__, level, __VA_ARGS__)
#define LOG_FATAL(...)          LogAppend(LL_FATAL,     __VA_ARGS__)
#define LOG_ERROR(...)          LogAppend(LL_ERROR,     __VA_ARGS__)
#define LOG_WARNING(...)        LogAppend(LL_WARNING,   __VA_ARGS__)
#define LOG_INFO(...)           LogAppend(LL_INFO,      __VA_ARGS__)
#define LOG_VERBOSE(...)        LogAppend(LL_VERBOSE,   __VA_ARGS__)
#define LOG_DEBUG(...)          LogAppend(LL_DEBUG,     __VA_ARGS__)
#define LOG_TRACE(...)          LogAppend(LL_TRACE,     __VA_ARGS__)
#define LOG_DIALOG(...)         LogAppend(LL_DIALOG,    __VA_ARGS__)
#define LOG_ERROR_UNSUPPRESSED(...) LogManager::get()->LogEntryUnsuppressed(__FILE__, __LINE__, LL_ERROR, __VA_ARGS__)
#define LOG_INFO_UNSUPPRESSED(...) LogManager::get()->LogEntryUnsuppressed(__FILE__, __LINE__, LL_INFO, __VA_ARGS__)
