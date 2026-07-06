/*---------------------------------------------------------*\
| LogManager.h                                              |
|                                                           |
|   Manages log file and output to the console              |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#ifndef LOGMANAGER_H
#define LOGMANAGER_H

#include <fstream>
#include <mutex>
#include <vector>
#include <queue>
#include <memory>
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

struct LogMessage
{
    std::string buffer;
    unsigned int level;
    const char* filename;
    int line;
    std::chrono::duration<double> counted_second;
    // int timestamp or float time_offset? TBD
};
typedef std::shared_ptr<LogMessage> PLogMessage;
typedef void(*LogDialogShowCallback)(void*, PLogMessage);

class LogManager
{
private:
    LogManager();
    LogManager(const LogManager&) = delete;
    LogManager(LogManager&&) = delete;
    ~LogManager();
    std::recursive_mutex entry_mutex;
    std::mutex section_mutex;
    std::ofstream log_stream;
    filesystem::path current_log_path = "";
    bool log_has_entries = false;

    std::vector<LogDialogShowCallback>  dialog_show_callbacks;
    std::vector<void*>                  dialog_show_callback_args;

    // A temporary log message storage to hold them until the stream opens
    std::vector<PLogMessage> temp_messages;

    // A log message storage used while suppression mode is active. Messages
    // produced during a device scan are buffered here and only flushed to the
    // file/stdout if the scan actually changed the device set; otherwise they
    // are discarded to keep the log file from growing on every rescan.
    std::vector<PLogMessage> suppressed_messages;
    bool                     suppress_mode = false;

    // A log message storage that will be displayed in the app
    std::vector<PLogMessage> all_messages;

    // A flag that marks if the message source file name and line number should be printed on screen
    bool print_source = false;

    // Logfile max level
    unsigned int loglevel = LL_INFO;
    int configured_log_limit = 0;

    // Verbosity (stdout) max level
    unsigned int verbosity = LL_WARNING;

    //Clock from LogManager creation
    std::chrono::time_point<std::chrono::steady_clock> base_clock;

    /*-------------------------------------------------*\
    | Per-day log rotation (service mode only)          |
    \*-------------------------------------------------*/
    bool                daily_rollover    = false;
    bool                service_log_mode  = false;
    std::string         daily_basename    = "";
    std::string         configured_log_template = "RGBServer_#.log";
    filesystem::path    log_base_dir      = "";
    filesystem::path    service_log_dir   = "";
    std::string         current_log_date  = "";
    int                 daily_log_limit   = 0;

    // A non-guarded append()
    void _append(const char* filename, int line, unsigned int level, const char* fmt, va_list va);

    // A non-guarded flush()
    void _flush();

    // Opens (or reopens, on day rollover) the log file used in service mode.
    // Service mode uses one file per day (RGBServer_YYYYMMDD.log), appended to
    // across same-day restarts, and rolled over at midnight while running.
    void _open_daily_log(const std::string& yyyymmdd);

    void rotate_logs(const filesystem::path& folder, const filesystem::path& templ, int max_count, const char* timestamp_regex);

public:
    static LogManager* get();
    void configure(json config, const filesystem::path & defaultDir);
    void setServiceLogDirectory(const filesystem::path& defaultDir);
    void reconfigure_daily_log(const filesystem::path& defaultDir);
    void flush();
    void append(const char* filename, int line, unsigned int level, const char* fmt, ...);

    /*-------------------------------------------------*\
    | Suppression mode for device-scan logging.         |
    | While active, INFO/VERBOSE/DEBUG/TRACE messages   |
    | are buffered in memory instead of written.        |
    | StartSuppressing() begins buffering;              |
    | StopSuppressing(true) flushes the buffer,         |
    | StopSuppressing(false) discards it.               |
    | FATAL/ERROR/DIALOG are never suppressed.          |
    \-------------------------------------------------*/
    void StartSuppressing();
    void StopSuppressing(bool flush);
    void setLoglevel(unsigned int);
    void setVerbosity(unsigned int);
    void setPrintSource(bool);
    void RegisterDialogShowCallback(LogDialogShowCallback callback, void* receiver);
    void UnregisterDialogShowCallback(LogDialogShowCallback callback, void* receiver);
    unsigned int getLoglevel();
    unsigned int getVerbosity() {return verbosity;}
    void clearMessages();
    std::vector<PLogMessage> messages();

    bool log_console_enabled;
    bool log_file_enabled;
    static const char* log_codes[];
};

#define LogAppend(level, ...)   LogManager::get()->append(__FILE__, __LINE__, level, __VA_ARGS__)
#define LOG_FATAL(...)          LogAppend(LL_FATAL,     __VA_ARGS__)
#define LOG_ERROR(...)          LogAppend(LL_ERROR,     __VA_ARGS__)
#define LOG_WARNING(...)        LogAppend(LL_WARNING,   __VA_ARGS__)
#define LOG_INFO(...)           LogAppend(LL_INFO,      __VA_ARGS__)
#define LOG_VERBOSE(...)        LogAppend(LL_VERBOSE,   __VA_ARGS__)
#define LOG_DEBUG(...)          LogAppend(LL_DEBUG,     __VA_ARGS__)
#define LOG_TRACE(...)          LogAppend(LL_TRACE,     __VA_ARGS__)
#define LOG_DIALOG(...)         LogAppend(LL_DIALOG,    __VA_ARGS__)

#endif // LOGMANAGER_H
