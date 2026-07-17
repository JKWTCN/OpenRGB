/*---------------------------------------------------------*\
| LogManager.cpp                                            |
|                                                           |
|   Manages log file and output to the console              |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <chrono>
#include <iomanip>
#include <iostream>
#include <regex>
#include <stdarg.h>
#include "filesystem.h"
#include "JsonUtils.h"
#include "LogManager.h"
#include "NetworkClient.h"
#include "NetworkServer.h"
#include "ResourceManager.h"

const char* LogManager::LOG_CODES[] = {"Fatal", "Error", "Warning", "Info", "Verbose", "Debug", "Trace", "Dialog"};

const char* TimestampPattern = "%04d%02d%02d_%02d%02d%02d";

/*---------------------------------------------------------*\
| Relies on the structure of the template above             |
\*---------------------------------------------------------*/
const char* TimestampRegex = "[0-9]{8}_[0-9]{6}";
const char* DailyDateRegex = "[0-9]{8}";

static std::string GetDailyLogBasename(const std::string& log_template)
{
    filesystem::path log_path = filesystem::u8path(log_template);
    std::string basename = log_path.stem().generic_u8string();
    std::size_t marker = basename.find("#");

    if(marker != basename.npos)
    {
        basename.erase(marker);
    }

    while(!basename.empty() && (basename.back() == '_' || basename.back() == '-' || basename.back() == '.' || basename.back() == ' '))
    {
        basename.pop_back();
    }

    return(basename.empty() ? "OpenRGB" : basename);
}

/*---------------------------------------------------------*\
| LogManager name for log entries                           |
\*---------------------------------------------------------*/
const char* LOGMANAGER = "LogManager";

/*---------------------------------------------------------*\
| LogManager Global Instance Pointer                        |
\*---------------------------------------------------------*/
LogManager* LogManager::instance;

LogManager::LogManager()
{
    /*-----------------------------------------------------*\
    | Initialize global instance pointer the when created   |
    | There should only ever be one instance of             |
    | LogManager                                            |
    \*-----------------------------------------------------*/
    if(!instance)
    {
        instance = this;
    }
    /*-----------------------------------------------------*\
    | If, for whatever reason, LogManager already exists,   |
    | delete this instance as only one should exist         |
    \*-----------------------------------------------------*/
    else
    {
        delete this;
        return;
    }

    base_clock          = std::chrono::steady_clock::now();
    log_console_enabled = false;
    loglevel            = LL_INFO;
    print_source        = false;
    suppress_mode       = false;
    log_file_enabled    = true;
    daily_rollover      = false;
    service_log_mode    = false;
    log_has_entries     = false;
    configured_log_limit = 10;
    daily_log_limit     = 10;
    configured_log_template = "OpenRGB_#.log";
    verbosity           = LL_WARNING;
}

LogManager::~LogManager()
{

}

/*---------------------------------------------------------*\
| LogManager Global Instance Accessor                       |
\*---------------------------------------------------------*/
LogManager* LogManager::get()
{
    /*-----------------------------------------------------*\
    | If LogManager does not exist yet, create it           |
    \*-----------------------------------------------------*/
    if(!instance)
    {
        instance = new LogManager();
    }

    return instance;
}

/*---------------------------------------------------------*\
| Callback Registration                                     |
\*---------------------------------------------------------*/
void LogManager::RegisterLogManagerCallback(LogManagerCallback new_callback, void * new_callback_arg)
{
    LogManagerCallbackMutex.lock();

    for(std::size_t idx = 0; idx < LogManagerCallbacks.size(); idx++)
    {
        if(LogManagerCallbacks[idx] == new_callback && LogManagerCallbackArgs[idx] == new_callback_arg)
        {
            LogManagerCallbackMutex.unlock();

            LOG_TRACE("[%s] Tried to register an already registered LogManager callback, skipping.  Total callbacks registered: %d", LOGMANAGER, LogManagerCallbacks.size());

            return;
        }
    }

    LogManagerCallbacks.push_back(new_callback);
    LogManagerCallbackArgs.push_back(new_callback_arg);

    LogManagerCallbackMutex.unlock();

    LOG_TRACE("[%s] Registered LogManager callback.  Total callbacks registered: %d", LOGMANAGER, LogManagerCallbacks.size());
}

void LogManager::UnregisterLogManagerCallback(LogManagerCallback callback, void * callback_arg)
{
    LogManagerCallbackMutex.lock();

    for(std::size_t idx = 0; idx < LogManagerCallbacks.size(); idx++)
    {
        if(LogManagerCallbacks[idx] == callback && LogManagerCallbackArgs[idx] == callback_arg)
        {
            LogManagerCallbacks.erase(LogManagerCallbacks.begin() + idx);
            LogManagerCallbackArgs.erase(LogManagerCallbackArgs.begin() + idx);
        }
    }

    LogManagerCallbackMutex.unlock();

    LOG_TRACE("[%s] Unregistered LogManager callback.  Total callbacks registered: %d", LOGMANAGER, LogManagerCallbackArgs.size());
}

/*---------------------------------------------------------*\
| Configuration                                             |
\*---------------------------------------------------------*/
void LogManager::Configure(json config, const filesystem::path& config_dir)
{
    /*-----------------------------------------------------*\
    | Lock the entry mitex while configuring                |
    \*-----------------------------------------------------*/
    std::lock_guard<std::recursive_mutex> guard(entry_mutex);

    /*-----------------------------------------------------*\
    | Close the log file if it is already open              |
    \*-----------------------------------------------------*/
    if(log_stream.is_open())
    {
        log_stream.close();
    }

    /*-----------------------------------------------------*\
    | If the log is not open, create a new log file         |
    \*-----------------------------------------------------*/
    if(!log_stream.is_open())
    {
        /*-------------------------------------------------*\
        | If a limit is declared in the config for the      |
        | maximum number of log files, respect the limit    |
        | Log rotation will remove the files matching the   |
        | current "logfile", starting with the oldest ones  |
        | (according to the timestamp in their filename)    |
        | i.e. with the lexicographically smallest filename |
        | 0 or less equals no limit                         |
        \*-------------------------------------------------*/
        int     loglimit            = JsonUtils::JsonGetInt(config, "file_count_limit", 10);
        log_file_enabled            = JsonUtils::JsonGetBool(config, "log_file", true);
        configured_log_limit        = loglimit;

        /*-------------------------------------------------*\
        | Default template for the logfile name             |
        | The # symbol is replaced with a timestamp         |
        \*-------------------------------------------------*/
        std::string logtemp = JsonUtils::JsonGetString(config, "logfile", "OpenRGB_#.log", false);
        configured_log_template = logtemp;

        if(log_file_enabled)
        {
            /*---------------------------------------------*\
            | If the # symbol is found in the log file      |
            | name, replace it with a timestamp             |
            \*---------------------------------------------*/
            time_t t = time(0);
            struct tm* tmp = localtime(&t);
            char time_string[64];
            snprintf(time_string, 64, TimestampPattern, 1900 + tmp->tm_year, tmp->tm_mon + 1, tmp->tm_mday, tmp->tm_hour, tmp->tm_min, tmp->tm_sec);

            if(service_log_mode)
            {
                daily_rollover  = true;
                daily_log_limit = loglimit;
                daily_basename  = GetDailyLogBasename(logtemp);

                filesystem::path configured_path = filesystem::u8path(logtemp);
                if(configured_path.is_absolute() && configured_path.has_parent_path())
                {
                    log_base_dir = configured_path.parent_path();
                }
                else
                {
                    log_base_dir = service_log_dir / "logs";
                }

                char date_string[16];
                snprintf(date_string, sizeof(date_string), "%04d%02d%02d", 1900 + tmp->tm_year, tmp->tm_mon + 1, tmp->tm_mday);
                OpenDailyLog(date_string);
            }
            else
            {
            std::string logname = logtemp;
            std::size_t oct = logname.find("#");
            if(oct != logname.npos)
            {
                logname.replace(oct, 1, time_string);
            }

            /*---------------------------------------------*\
            | If the path is relative, use logs dir         |
            \*---------------------------------------------*/
            filesystem::path p = filesystem::u8path(logname);
            if(p.is_relative())
            {
                p = config_dir / "logs" / logname;
            }
            filesystem::create_directories(p.parent_path());

            /*---------------------------------------------*\
            | "Log rotation": remove old log files          |
            | exceeding the current configured limit        |
            \*---------------------------------------------*/
            LogRotate(p.parent_path(), filesystem::u8path(logtemp).filename(), loglimit, TimestampRegex);

            /*---------------------------------------------*\
            | Open the logfile                              |
            \*---------------------------------------------*/
            current_log_path = p;
            log_has_entries  = false;
            log_stream.open(p);

            /*---------------------------------------------*\
            | Print Git Commit info, version, etc.          |
            \*---------------------------------------------*/
            log_stream << "    OpenRGB v" << VERSION_STRING << std::endl;
            log_stream << "    Commit: " << GIT_COMMIT_ID << " from " << GIT_COMMIT_DATE << std::endl;
            log_stream << "    Launched: " << time_string << std::endl;
            log_stream << "====================================================================================================" << std::endl;
            log_stream << std::endl;
            }
        }
    }

    /*-----------------------------------------------------*\
    | Check loglevel configuration                          |
    \*-----------------------------------------------------*/
    loglevel = JsonUtils::JsonGetInt(config, "loglevel", LL_INFO);

    /*-----------------------------------------------------*\
    | Check log console configuration                       |
    \*-----------------------------------------------------*/
    log_console_enabled = JsonUtils::JsonGetBool(config, "log_console");

    /*-----------------------------------------------------*\
    | Flush the log                                         |
    \*-----------------------------------------------------*/
    LogFlush();
}

void LogManager::OpenDailyLog(const std::string& yyyymmdd)
{
    std::string basename = daily_basename.empty() ? "OpenRGB" : daily_basename;
    filesystem::path path = log_base_dir / (basename + "_" + yyyymmdd + ".log");
    filesystem::create_directories(path.parent_path());

    LogRotate(path.parent_path(), filesystem::u8path(basename + "_#.log"), daily_log_limit, DailyDateRegex);

    bool file_existed = filesystem::exists(path) && filesystem::file_size(path) > 0;
    current_log_path = path;
    log_has_entries  = file_existed;
    log_stream.open(path, std::ios::app);

    if(!file_existed && log_stream.is_open())
    {
        time_t t = time(0);
        struct tm* tmp = localtime(&t);
        char time_string[64];
        snprintf(time_string, sizeof(time_string), TimestampPattern, 1900 + tmp->tm_year, tmp->tm_mon + 1, tmp->tm_mday, tmp->tm_hour, tmp->tm_min, tmp->tm_sec);
        log_stream << "    OpenRGB v" << VERSION_STRING << std::endl;
        log_stream << "    Commit: " << GIT_COMMIT_ID << " from " << GIT_COMMIT_DATE << std::endl;
        log_stream << "    Launched: " << time_string << std::endl;
        log_stream << "====================================================================================================" << std::endl << std::endl;
    }

    current_log_date = yyyymmdd;
}

void LogManager::SetServiceLogDirectory(const filesystem::path& config_dir)
{
    std::lock_guard<std::recursive_mutex> guard(entry_mutex);
    service_log_mode = true;
    service_log_dir  = config_dir;

    if(!log_file_enabled)
    {
        return;
    }

    if(log_stream.is_open())
    {
        log_stream.flush();
        log_stream.close();

        if(!daily_rollover && !log_has_entries && !current_log_path.empty())
        {
            std::error_code error;
            filesystem::remove(current_log_path, error);
        }
    }

    daily_rollover  = true;
    daily_log_limit = configured_log_limit;
    daily_basename  = GetDailyLogBasename(configured_log_template);
    log_base_dir    = config_dir / "logs";

    time_t t = time(0);
    struct tm* tmp = localtime(&t);
    char date_string[16];
    snprintf(date_string, sizeof(date_string), "%04d%02d%02d", 1900 + tmp->tm_year, tmp->tm_mon + 1, tmp->tm_mday);
    OpenDailyLog(date_string);
    LogFlush();
}

void LogManager::ReconfigureDailyLog(const filesystem::path& config_dir)
{
    std::lock_guard<std::recursive_mutex> guard(entry_mutex);

    if(!daily_rollover)
    {
        return;
    }

    if(log_stream.is_open())
    {
        log_stream.flush();
        log_stream.close();
    }

    service_log_dir = config_dir;
    log_base_dir    = config_dir / "logs";

    time_t t = time(0);
    struct tm* tmp = localtime(&t);
    char date_string[16];
    snprintf(date_string, sizeof(date_string), "%04d%02d%02d", 1900 + tmp->tm_year, tmp->tm_mon + 1, tmp->tm_mday);
    OpenDailyLog(date_string);
    LogFlush();
}

/*---------------------------------------------------------*\
| Log Buffer Functions                                      |
\*---------------------------------------------------------*/
void LogManager::ClearLogBuffer()
{
    if(ResourceManager::get()->IsLocalClient() && (ResourceManager::get()->GetLocalClient()->GetSupportsProfileManagerAPI()))
    {
        ResourceManager::get()->GetLocalClient()->LogManager_ClearLogBuffer();
    }

    all_messages.clear();
}

std::vector<PLogMessage> LogManager::GetLogBuffer()
{
    return(all_messages);
}

void LogManager::StartSuppressing()
{
    std::lock_guard<std::recursive_mutex> guard(entry_mutex);
    suppress_mode = true;
    suppressed_messages.clear();
}

void LogManager::StopSuppressing(bool flush)
{
    std::lock_guard<std::recursive_mutex> guard(entry_mutex);
    suppress_mode = false;

    if(flush)
    {
        std::vector<PLogMessage> messages = std::move(suppressed_messages);
        suppressed_messages.clear();

        for(const PLogMessage& message : messages)
        {
            LogEntry_message(message);
        }
    }

    suppressed_messages.clear();
}

/*---------------------------------------------------------*\
| Log Level Functions                                       |
\*---------------------------------------------------------*/
unsigned int LogManager::GetLogLevel()
{
    return(loglevel);
}

unsigned int LogManager::GetVerbosity()
{
    return(verbosity);
}

void LogManager::SetLogLevel(unsigned int level, bool local_only)
{
    /*-----------------------------------------------------*\
    | Check that the new log level is valid, otherwise set  |
    | it within the valid range                             |
    \*-----------------------------------------------------*/
    if(level > LL_TRACE)
    {
        level = LL_TRACE;
    }

    /*-----------------------------------------------------*\
    | Set the server's log level if local client            |
    \*-----------------------------------------------------*/
    if(!local_only && ResourceManager::get()->IsLocalClient() && (ResourceManager::get()->GetLocalClient()->GetSupportsLogManagerAPI()))
    {
        ResourceManager::get()->GetLocalClient()->LogManager_SetLogLevel(level);
    }

    LOG_DEBUG("[%s] LogLevel set to %d", LOGMANAGER, level);

    /*-----------------------------------------------------*\
    | Set the new log level                                 |
    \*-----------------------------------------------------*/
    loglevel = level;
}

void LogManager::SetVerbosity(unsigned int level)
{
    /*-----------------------------------------------------*\
    | Check that the new verbosity is valid, otherwise set  |
    | it within the valid range                             |
    \*-----------------------------------------------------*/
    if(level > LL_TRACE)
    {
        level = LL_TRACE;
    }

    LOG_DEBUG("[%s] Verbosity set to %d", LOGMANAGER, level);

    /*-----------------------------------------------------*\
    | Set the new verbosity                                 |
    \*-----------------------------------------------------*/
    verbosity = level;
}

/*---------------------------------------------------------*\
| Log Format Functions                                      |
\*---------------------------------------------------------*/
void LogManager::SetPrintSource(bool print)
{
    LOG_DEBUG("[LogManager] Source code location printouts were %s", print ? "enabled" : "disabled");
    print_source = print;
}

/*---------------------------------------------------------*\
| Logging Functions                                         |
\*---------------------------------------------------------*/
void LogManager::LogEntry(const char* filename, int line, unsigned int level, const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);

    LogEntry_va(filename, line, level, fmt, va);

    va_end(va);
}

void LogManager::LogEntry_message(PLogMessage message)
{
    /*-----------------------------------------------------*\
    | Lock the entry mutex while adding an entry            |
    \*-----------------------------------------------------*/
    std::lock_guard<std::recursive_mutex> guard(entry_mutex);

    if(suppress_mode && message->level > LL_ERROR && message->level != LL_DIALOG)
    {
        suppressed_messages.push_back(message);
        return;
    }

    /*-----------------------------------------------------*\
    | If this is a dialog message, call the dialog show     |
    | callback                                              |
    \*-----------------------------------------------------*/
    if(message->level == LL_DIALOG)
    {
        for(std::size_t idx = 0; idx < LogManagerCallbacks.size(); idx++)
        {
            LogManagerCallbacks[idx](LogManagerCallbackArgs[idx], LOGMANAGER_UPDATE_REASON_SHOW_DIALOG, message);
        }
    }
    else
    {
        for(std::size_t idx = 0; idx < LogManagerCallbacks.size(); idx++)
        {
            LogManagerCallbacks[idx](LogManagerCallbackArgs[idx], LOGMANAGER_UPDATE_REASON_LOG_ENTRY, message);
        }
    }

    /*-----------------------------------------------------*\
    | If the message is within the current verbosity, print |
    | it on the screen                                      |
    \*-----------------------------------------------------*/
    if(message->level <= verbosity || message->level == LL_DIALOG)
    {
        std::cout << std::left << "[" << std::setw(10) << message->timestamp  << "]";
        std::cout << std::left << "[" << std::setw(7) << LOG_CODES[message->level] << "]";
        if(print_source)
        {
            std::cout << " [" << message->filename << ":" << message->line << "]";
        }
        std::cout << message->text;
        std::cout << std::endl;
    }

    /*-----------------------------------------------------*\
    | Add the message to the logfile queue                  |
    \*-----------------------------------------------------*/
    temp_messages.push_back(message);

    if(log_console_enabled)
    {
        all_messages.push_back(message);

        NetworkServer* server = ResourceManager::get()->GetServer();

        if(server)
        {
            server->SignalLogManagerLoggedEntry(*message);
        }
    }

    /*-----------------------------------------------------*\
    | Flush the queues                                      |
    \*-----------------------------------------------------*/
    LogFlush();
}

void LogManager::LogEntry_va(const char* filename, int line, unsigned int level, const char* fmt, va_list va)
{
    /*-----------------------------------------------------*\
    | If a critical message occurs, enable source           |
    | printing and set loglevel and verbosity to highest    |
    \*-----------------------------------------------------*/
    if(level == LL_FATAL)
    {
        print_source    = true;
        loglevel        = LL_DEBUG;
        verbosity       = LL_DEBUG;
    }

    /*-----------------------------------------------------*\
    | Create a new message                                  |
    \*-----------------------------------------------------*/
    PLogMessage message = std::make_shared<LogMessage>();

    /*-----------------------------------------------------*\
    | Resize the buffer, then fill in the message text      |
    \*-----------------------------------------------------*/
    va_list va2;
    va_copy(va2, va);
    int len = vsnprintf(nullptr, 0, fmt, va);
    message->text.resize(len);
    vsnprintf(&(message->text[0]), len + 1, fmt, va2);
    va_end(va2);

    /*-----------------------------------------------------*\
    | Fill in message information                           |
    \*-----------------------------------------------------*/
    message->level                      = level;
    message->filename                   = filename;
    message->line                       = line;
    message->timestamp                  = (unsigned int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - base_clock).count();

    /*-----------------------------------------------------*\
    | Remove newlines in the log text unless this is a      |
    | dialog                                                |
    \*-----------------------------------------------------*/
    if(level != LL_DIALOG)
    {
        message->text.erase(std::remove(message->text.begin(), message->text.end(), '\n'), message->text.end());
        message->text.erase(std::remove(message->text.begin(), message->text.end(), '\r'), message->text.end());
    }

    LogEntry_message(message);
}

/*---------------------------------------------------------*\
| Private Functions                                         |
\*---------------------------------------------------------*/
void LogManager::LogFlush()
{
    /*-----------------------------------------------------*\
    | Lock the entry mutex while flushing                   |
    \*-----------------------------------------------------*/
    std::lock_guard<std::recursive_mutex> guard(entry_mutex);

    if(daily_rollover && log_stream.is_open())
    {
        time_t t = time(0);
        struct tm* tmp = localtime(&t);
        char today[16];
        snprintf(today, sizeof(today), "%04d%02d%02d", 1900 + tmp->tm_year, tmp->tm_mon + 1, tmp->tm_mday);

        if(current_log_date != today)
        {
            log_stream.flush();
            log_stream.close();
            OpenDailyLog(today);
        }
    }

    /*-----------------------------------------------------*\
    | If the log is open, write out buffered messages       |
    \*-----------------------------------------------------*/
    if(log_stream.is_open())
    {
        for(std::size_t message_idx = 0; message_idx < temp_messages.size(); message_idx++)
        {
            if(temp_messages[message_idx]->level <= loglevel || temp_messages[message_idx]->level == LL_DIALOG)
            {
                log_stream << std::left << "[" << std::setw(10) << temp_messages[message_idx]->timestamp  << "]";
                log_stream << std::left << "[" << std::setw(7) << LOG_CODES[temp_messages[message_idx]->level] << "]";
                if(print_source)
                {
                    std::cout << " [" << temp_messages[message_idx]->filename << ":" << temp_messages[message_idx]->line << "]";
                }
                log_stream << temp_messages[message_idx]->text;
                log_stream << std::endl;
                log_has_entries = true;
            }
        }

        /*-------------------------------------------------*\
        | Clear temp message buffers after writing them out |
        \*-------------------------------------------------*/
        temp_messages.clear();

        /*-------------------------------------------------*\
        | Flush the stream                                  |
        \*-------------------------------------------------*/
        log_stream.flush();
    }
}

void LogManager::LogRotate(const filesystem::path& folder, const filesystem::path& templ, std::size_t max_count, const char* timestamp_regex)
{
    if(max_count < 1)
    {
        return;
    }

    std::string templ2 = templ.filename().generic_u8string();

    /*-----------------------------------------------------*\
    | Process the templ2 into a usable regex                |
    | The # symbol is replaced with a timestamp regex       |
    | Any regex-unfriendly symbols are escaped with a       |
    | backslash                                             |
    \*-----------------------------------------------------*/
    std::string regex_templ = "^";
    for(std::size_t i = 0; i < templ2.size(); ++i)
    {
        switch(templ2[i])
        {
        /*-------------------------------------------------*\
        | Symbols that have special meanings in regex'es    |
        | need backslash escaping                           |
        \*-------------------------------------------------*/
        case '.':
        case '^':
        case '$':
        case '(':
        case ')':
        case '{':
        case '}':
        case '+':
        case '[':
        case ']':
        case '*':
        case '-':
        /*-------------------------------------------------*\
        | Should have been filtered out by the filesystem   |
        | processing, but... who knows                      |
        \*-------------------------------------------------*/
        case '\\':
            regex_templ.push_back('\\');
            regex_templ.push_back(templ2[i]);
            break;

        /*-------------------------------------------------*\
        | The # symbol is reserved for the timestamp and    |
        | thus is replaced with the timestamp regex         |
        | template                                          |
        \*-------------------------------------------------*/
        case '#':
            regex_templ.append(timestamp_regex);
            break;

        default:
            regex_templ.push_back(templ2[i]);
            break;
        }
    }
    regex_templ.push_back('$');

    std::regex r(regex_templ);

    std::vector<filesystem::path> valid_paths;
    std::filesystem::directory_iterator it(folder);
    for(; it != filesystem::end(it); ++it)
    {
        if(it->is_regular_file())
        {
            std::string fname = it->path().filename().u8string();
            if(std::regex_match(fname, r))
            {
                valid_paths.push_back(it->path());
            }
        }
    }
    std::sort(valid_paths.begin(), valid_paths.end());

    /*-----------------------------------------------------*\
    | NOTE: the "1" extra file to remove creates space for  |
    | the one we're about to create for max_count <= 0 and  |
    | to prevent any possible errors in the above logic     |
    \*-----------------------------------------------------*/
    if(valid_paths.size() > (max_count - 1))
    {
        std::size_t remove_count = valid_paths.size() - max_count + 1;
        if(remove_count > valid_paths.size())
        {
            remove_count = valid_paths.size();
        }

        for(std::size_t i = 0; i < remove_count; ++i)
        {
            /*-------------------------------------------------*\
            | Uses error code to force the `remove` call to be  |
            | `noexcept`                                        |
            \*-------------------------------------------------*/
            std::error_code ec;
            if(filesystem::remove(valid_paths[i], ec))
            {
                LOG_VERBOSE("[LogManager] Removed log file [%s] during rotation", valid_paths[i].u8string().c_str());
            }
            else
            {
                LOG_WARNING("[LogManager] Failed to remove log file [%s] during rotation: %s", valid_paths[i].u8string().c_str(), ec.message().c_str());
            }
        }
    }
}
