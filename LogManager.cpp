/*---------------------------------------------------------*\
| LogManager.cpp                                            |
|                                                           |
|   Manages log file and output to the console              |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "LogManager.h"

#include <regex>
#include <stdarg.h>
#include <iostream>
#include <iomanip>
#include <chrono>

#include "filesystem.h"
#include "AppInfo.h"
#include "startup/startup.h"

const char* LogManager::log_codes[] = {"FATAL:", "ERROR:", "Warning:", "Info:", "Verbose:", "Debug:", "Trace:", "Dialog:"};

const char* TimestampPattern = "%04d%02d%02d_%02d%02d%02d";

/*---------------------------------------------------------*\
| Relies on the structure of the template above             |
\*---------------------------------------------------------*/
const char* TimestampRegex = "[0-9]{8}_[0-9]{6}";
const char* DailyDateRegex = "[0-9]{8}";

static std::string GetDailyLogBasename(const std::string& logtempl)
{
    filesystem::path log_path = filesystem::u8path(logtempl);
    std::string basename = log_path.stem().generic_u8string();
    size_t marker = basename.find("#");

    if(marker != basename.npos)
    {
        basename.erase(marker);
    }

    while(!basename.empty() && ((basename.back() == '_') || (basename.back() == '-') || (basename.back() == '.') || (basename.back() == ' ')))
    {
        basename.pop_back();
    }

    return basename.empty() ? "RGBServer" : basename;
}

LogManager::LogManager()
{
    base_clock          = std::chrono::steady_clock::now();
    log_console_enabled = false;
    log_file_enabled    = true;
}

LogManager* LogManager::get()
{
    static LogManager* _instance = nullptr;
    static std::mutex instance_mutex;
    std::lock_guard<std::mutex> grd(instance_mutex);

    /*-----------------------------------------------------*\
    | Create a new instance if one does not exist           |
    \*-----------------------------------------------------*/
    if(!_instance)
    {
        _instance = new LogManager();
    }

    return _instance;
}

unsigned int LogManager::getLoglevel()
{
    if(log_console_enabled)
    {
        return(LL_TRACE);
    }
    else
    {
        return(loglevel);
    }
}

void LogManager::configure(json config, const filesystem::path& defaultDir)
{
    std::lock_guard<std::recursive_mutex> grd(entry_mutex);

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
        | 0 or less equals no limit (default)               |
        \*-------------------------------------------------*/
        int loglimit = 0;
        if(config.contains("file_count_limit") && config["file_count_limit"].is_number_integer())
        {
            loglimit = config["file_count_limit"];
        }
        configured_log_limit = loglimit;

        if(config.contains("log_file"))
        {
            log_file_enabled = config["log_file"];
        }

        /*-------------------------------------------------*\
        | Default template for the logfile name             |
        | The # symbol is replaced with a timestamp         |
        \*-------------------------------------------------*/
        std::string logtempl = "RGBServer_#.log";

        if(log_file_enabled)
        {
            /*---------------------------------------------*\
            | If the logfile is defined in the              |
            | configuration, use the configured name        |
            \*---------------------------------------------*/
            if(config.contains("logfile"))
            {
                const json& logfile_obj = config["logfile"];
                if(logfile_obj.is_string())
                {
                    std::string tmpname = config["logfile"];
                    if(!tmpname.empty())
                    {
                        logtempl = tmpname;
                    }
                }
            }
            configured_log_template = logtempl;

            /*---------------------------------------------*\
            | Service mode uses one log file per day        |
            | (RGBServer_YYYYMMDD.log), appended to across  |
            | same-day restarts and rolled over at midnight |
            | while running. Non-service mode keeps the     |
            | per-launch file with full timestamp.          |
            \*---------------------------------------------*/
            if(startup_is_service_mode() || service_log_mode)
            {
                time_t t = time(0);
                struct tm* tmp = localtime(&t);
                char date_string[16];
                snprintf(date_string, sizeof(date_string), "%04d%02d%02d", 1900 + tmp->tm_year, tmp->tm_mon + 1, tmp->tm_mday);

                daily_rollover  = true;
                daily_log_limit = loglimit;
                daily_basename  = GetDailyLogBasename(logtempl);

                filesystem::path log_path = filesystem::u8path(logtempl);
                if(log_path.is_absolute() && log_path.has_parent_path())
                {
                    log_base_dir = log_path.parent_path();
                }
                else if(!service_log_dir.empty())
                {
                    log_base_dir = service_log_dir / "logs";
                }
                else
                {
                    log_base_dir = defaultDir / "logs";
                }

                _open_daily_log(date_string);
            }
            else
            {
                /*-----------------------------------------*\
                | If the # symbol is found in the log file |
                | name, replace it with a timestamp        |
                \-----------------------------------------*/
                time_t t = time(0);
                struct tm* tmp = localtime(&t);
                char time_string[64];
                snprintf(time_string, 64, TimestampPattern, 1900 + tmp->tm_year, tmp->tm_mon + 1, tmp->tm_mday, tmp->tm_hour, tmp->tm_min, tmp->tm_sec);

                std::string logname = logtempl;
                size_t oct = logname.find("#");
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
                    p = defaultDir / "logs" / logname;
                }
                filesystem::create_directories(p.parent_path());

                /*---------------------------------------------*\
                | "Log rotation": remove old log files          |
                | exceeding the current configured limit        |
                \*---------------------------------------------*/
                rotate_logs(p.parent_path(), filesystem::u8path(logtempl).filename(), loglimit, TimestampRegex);

                /*---------------------------------------------*\
                | Open the logfile                              |
                \*---------------------------------------------*/
                current_log_path = p;
                log_has_entries  = false;
                log_stream.open(p);

                /*---------------------------------------------*\
                | Print Git Commit info, version, etc.          |
                \*---------------------------------------------*/
                log_stream << "    " << APP_NAME << " v" << VERSION_STRING << std::endl;
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
    if(config.contains("loglevel"))
    {
        const json& loglevel_obj = config["loglevel"];

        /*-------------------------------------------------*\
        | Set the log level if configured                   |
        \*-------------------------------------------------*/
        if(loglevel_obj.is_number_integer())
        {
            loglevel = loglevel_obj;
        }
    }

    /*-----------------------------------------------------*\
    | Check log console configuration                       |
    \*-----------------------------------------------------*/
    if(config.contains("log_console"))
    {
        log_console_enabled = config["log_console"];
    }

    /*-----------------------------------------------------*\
    | Flush the log                                         |
    \*-----------------------------------------------------*/
    _flush();
}

void LogManager::_open_daily_log(const std::string& yyyymmdd)
{
    /*-----------------------------------------------------*\
    | Build the per-day log filename in the form            |
    | <basename>_YYYYMMDD.log                               |
    \*-----------------------------------------------------*/
    std::string basename = daily_basename.empty() ? APP_NAME : daily_basename;
    std::string logname  = basename + "_" + yyyymmdd + ".log";

    filesystem::path p = log_base_dir / logname;
    filesystem::create_directories(p.parent_path());

    /*-----------------------------------------------------*\
    | Log rotation template: the date is a 8-digit run,     |
    | so reuse rotate_logs() with a matching template       |
    \*-----------------------------------------------------*/
    std::string rot_templ = basename + "_#.log";
    rotate_logs(p.parent_path(), filesystem::u8path(rot_templ).filename(), daily_log_limit, DailyDateRegex);

    /*-----------------------------------------------------*\
    | Append to the file if it already exists (same-day     |
    | restart), so all entries for a given day land in one   |
    | file. Only print the header on first creation.         |
    \*-----------------------------------------------------*/
    bool file_existed = filesystem::exists(p) && filesystem::file_size(p) > 0;

    current_log_path = p;
    log_has_entries  = file_existed;
    log_stream.open(p, std::ios::app);

    if(!file_existed && log_stream.is_open())
    {
        time_t t = time(0);
        struct tm* tmp = localtime(&t);
        char time_string[64];
        snprintf(time_string, 64, TimestampPattern, 1900 + tmp->tm_year, tmp->tm_mon + 1, tmp->tm_mday, tmp->tm_hour, tmp->tm_min, tmp->tm_sec);

        log_stream << "    " << APP_NAME << " v" << VERSION_STRING << std::endl;
        log_stream << "    Commit: " << GIT_COMMIT_ID << " from " << GIT_COMMIT_DATE << std::endl;
        log_stream << "    Launched: " << time_string << std::endl;
        log_stream << "====================================================================================================" << std::endl;
        log_stream << std::endl;
    }

    current_log_date = yyyymmdd;
}

void LogManager::setServiceLogDirectory(const filesystem::path& defaultDir)
{
    std::lock_guard<std::recursive_mutex> grd(entry_mutex);

    service_log_mode = true;
    service_log_dir  = defaultDir;

    if(log_file_enabled)
    {
        if(log_stream.is_open())
        {
            log_stream.flush();
            log_stream.close();

            if(!daily_rollover && !log_has_entries && !current_log_path.empty())
            {
                std::error_code ec;
                filesystem::remove(current_log_path, ec);
            }
        }

        daily_rollover  = true;
        daily_log_limit = configured_log_limit;
        daily_basename  = GetDailyLogBasename(configured_log_template);
        log_base_dir    = defaultDir / "logs";

        time_t t = time(0);
        struct tm* tmp = localtime(&t);
        char date_string[16];
        snprintf(date_string, sizeof(date_string), "%04d%02d%02d", 1900 + tmp->tm_year, tmp->tm_mon + 1, tmp->tm_mday);

        _open_daily_log(date_string);
        _flush();
    }
}

void LogManager::reconfigure_daily_log(const filesystem::path& defaultDir)
{
    std::lock_guard<std::recursive_mutex> grd(entry_mutex);

    /*-------------------------------------------------*\
    | Nothing to do unless daily rollover is active     |
    \*-------------------------------------------------*/
    if(!daily_rollover)
    {
        return;
    }

    /*-------------------------------------------------*\
    | Close any log file opened against the wrong       |
    | directory (the ResourceManager constructor runs   |
    | before the service directory is repointed) and    |
    | reopen against the real directory.                |
    \*-------------------------------------------------*/
    if(log_stream.is_open())
    {
        log_stream.flush();
        log_stream.close();
    }

    service_log_dir = defaultDir;
    log_base_dir    = defaultDir / "logs";

    time_t t = time(0);
    struct tm* tmp = localtime(&t);
    char date_string[16];
    snprintf(date_string, sizeof(date_string), "%04d%02d%02d", 1900 + tmp->tm_year, tmp->tm_mon + 1, tmp->tm_mday);

    _open_daily_log(date_string);

    _flush();
}

void LogManager::_flush()
{
    /*-----------------------------------------------------*\
    | If the log is open, write out buffered messages       |
    \*-----------------------------------------------------*/
    if(log_stream.is_open())
    {
        for(size_t msg = 0; msg < temp_messages.size(); ++msg)
        {
            if(temp_messages[msg]->level <= loglevel || temp_messages[msg]->level == LL_DIALOG)
            {
                /*-----------------------------------------*\
                | Put the timestamp here                    |
                \*-----------------------------------------*/
                std::chrono::milliseconds counter = std::chrono::duration_cast<std::chrono::milliseconds>(temp_messages[msg]->counted_second);
                log_stream << std::left << std::setw(6) << counter.count()  << "|";
                log_stream << std::left << std::setw(9) << log_codes[temp_messages[msg]->level];
                log_stream << temp_messages[msg]->buffer;

                if(print_source)
                {
                    log_stream << " [" << temp_messages[msg]->filename << ":" << temp_messages[msg]->line << "]";
                }

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

void LogManager::flush()
{
    std::lock_guard<std::recursive_mutex> grd(entry_mutex);
    _flush();
}

void LogManager::StartSuppressing()
{
    std::lock_guard<std::recursive_mutex> grd(entry_mutex);

    suppress_mode        = true;
    suppressed_messages.clear();
}

void LogManager::StopSuppressing(bool flush)
{
    std::lock_guard<std::recursive_mutex> grd(entry_mutex);

    /*-----------------------------------------------------*\
    | Always leave suppression mode first so that _flush /  |
    | _append run normally afterwards                       |
    \*-----------------------------------------------------*/
    suppress_mode = false;

    if(flush)
    {
        for(size_t msg = 0; msg < suppressed_messages.size(); msg++)
        {
            PLogMessage mes = suppressed_messages[msg];

            /*-----------------------------------------*\
            | Replay the stdout output that _append     |
            | would have done for this message          |
            \-----------------------------------------*/
            if(mes->level <= verbosity || mes->level == LL_DIALOG)
            {
                std::cout << mes->buffer;
                if(print_source)
                {
                    std::cout << " [" << mes->filename << ":" << mes->line << "]";
                }
                std::cout << std::endl;
            }

            temp_messages.push_back(mes);

            if(log_console_enabled)
            {
                all_messages.push_back(mes);
            }
        }

        _flush();
    }

    suppressed_messages.clear();
}

void LogManager::_append(const char* filename, int line, unsigned int level, const char* fmt, va_list va)
{
    /*-----------------------------------------------------*\
    | If a critical message occurs, enable source           |
    | printing and set loglevel and verbosity to highest    |
    \*-----------------------------------------------------*/
    if(level == LL_FATAL)
    {
        print_source = true;
        loglevel = LL_DEBUG;
        verbosity = LL_DEBUG;
    }

    /*-----------------------------------------------------*\
    | In service mode, roll over to a new file when the     |
    | calendar day changes, so a long-running service gets  |
    | one file per day. _append() runs under entry_mutex,   |
    | so the close/reopen here is thread-safe.              |
    \*-----------------------------------------------------*/
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
            _open_daily_log(today);
        }
    }

    /*-----------------------------------------------------*\
    | Create a new message                                  |
    \*-----------------------------------------------------*/
    PLogMessage mes = std::make_shared<LogMessage>();

    /*-----------------------------------------------------*\
    | Resize the buffer, then fill in the message text      |
    \*-----------------------------------------------------*/
    va_list va2;
    va_copy(va2, va);
    int len = vsnprintf(nullptr, 0, fmt, va);
    if(len < 0)
    {
        mes->buffer = "[LogManager] Failed to format log message";
    }
    else
    {
        std::vector<char> buffer(len + 1);
        vsnprintf(buffer.data(), buffer.size(), fmt, va2);
        mes->buffer.assign(buffer.data(), len);
    }
    va_end(va2);

    /*-----------------------------------------------------*\
    | Fill in message information                           |
    \*-----------------------------------------------------*/
    mes->level          = level;
    mes->filename       = filename;
    mes->line           = line;
    mes->counted_second = std::chrono::steady_clock::now() - base_clock;

    /*-----------------------------------------------------*\
    | If this is a dialog message, call the dialog show     |
    | callback                                              |
    \*-----------------------------------------------------*/
    if(level == LL_DIALOG)
    {
        for(size_t idx = 0; idx < dialog_show_callbacks.size(); idx++)
        {
            dialog_show_callbacks[idx](dialog_show_callback_args[idx], mes);
        }
    }

    /*-----------------------------------------------------*\
    | Suppression mode: buffer the message instead of       |
    | emitting it. FATAL/ERROR/DIALOG are never suppressed  |
    | so real errors can't be silently dropped.             |
    \*-----------------------------------------------------*/
    if(suppress_mode && level > LL_ERROR && level != LL_DIALOG)
    {
        suppressed_messages.push_back(mes);

        return;
    }

    /*-----------------------------------------------------*\
    | If the message is within the current verbosity, print |
    | it on the screen                                      |
    | TODO: Put the timestamp here                          |
    \*-----------------------------------------------------*/
    if(level <= verbosity || level == LL_DIALOG)
    {
        std::cout << mes->buffer;
        if(print_source)
        {
            std::cout << " [" << mes->filename << ":" << mes->line << "]";
        }
        std::cout << std::endl;
    }

    /*-----------------------------------------------------*\
    | Add the message to the logfile queue                  |
    \*-----------------------------------------------------*/
    temp_messages.push_back(mes);

    if(log_console_enabled)
    {
        all_messages.push_back(mes);
    }

    /*-----------------------------------------------------*\
    | Flush the queues                                      |
    \*-----------------------------------------------------*/
    _flush();
}

std::vector<PLogMessage> LogManager::messages()
{
    return all_messages;
}

void LogManager::clearMessages()
{
    all_messages.clear();
}

void LogManager::append(const char* filename, int line, unsigned int level, const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);

    std::lock_guard<std::recursive_mutex> grd(entry_mutex);
    _append(filename, line, level, fmt, va);

    va_end(va);
}

void LogManager::setLoglevel(unsigned int level)
{
    /*-----------------------------------------------------*\
    | Check that the new log level is valid, otherwise set  |
    | it within the valid range                             |
    \*-----------------------------------------------------*/
    if(level > LL_TRACE)
    {
        level = LL_TRACE;
    }

    LOG_DEBUG("[LogManager] Loglevel set to %d", level);

    /*-----------------------------------------------------*\
    | Set the new log level                                 |
    \*-----------------------------------------------------*/
    loglevel = level;
}

void LogManager::setVerbosity(unsigned int level)
{
    /*-----------------------------------------------------*\
    | Check that the new verbosity is valid, otherwise set  |
    | it within the valid range                             |
    \*-----------------------------------------------------*/
    if(level > LL_TRACE)
    {
        level = LL_TRACE;
    }

    LOG_DEBUG("[LogManager] Verbosity set to %d", level);

    /*-----------------------------------------------------*\
    | Set the new verbosity                                 |
    \*-----------------------------------------------------*/
    verbosity = level;
}

void LogManager::setPrintSource(bool v)
{
    LOG_DEBUG("[LogManager] Source code location printouts were %s", v ? "enabled" : "disabled");
    print_source = v;
}

void LogManager::RegisterDialogShowCallback(LogDialogShowCallback callback, void* receiver)
{
    LOG_DEBUG("[LogManager] dialog show callback registered");
    dialog_show_callbacks.push_back(callback);
    dialog_show_callback_args.push_back(receiver);
}

void LogManager::UnregisterDialogShowCallback(LogDialogShowCallback callback, void* receiver)
{
    for(size_t idx = 0; idx < dialog_show_callbacks.size(); idx++)
    {
        if(dialog_show_callbacks[idx] == callback && dialog_show_callback_args[idx] == receiver)
        {
            dialog_show_callbacks.erase(dialog_show_callbacks.begin() + idx);
            dialog_show_callback_args.erase(dialog_show_callback_args.begin() + idx);
        }
    }
}

void LogManager::rotate_logs(const filesystem::path& folder, const filesystem::path& templ, int max_count, const char* timestamp_regex)
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
    for(size_t i = 0; i < templ2.size(); ++i)
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
    size_t remove_count = valid_paths.size() - max_count + 1;
    if(remove_count > valid_paths.size())
    {
        remove_count = valid_paths.size();
    }

    for(size_t i = 0; i < remove_count; ++i)
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
