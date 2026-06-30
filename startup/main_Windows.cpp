/*---------------------------------------------------------*\
| main_Windows.cpp                                          |
|                                                           |
|   Entry point for the OpenRGB application on Windows      |
|   Differentiate between service and application startup   |
|   Housekeeping for Windows service management             |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <system_error>
#include <windows.h>
#include <shellapi.h>
#include <thread>

#include <QCoreApplication>
#include <QMetaObject>

#include "cli.h"
#include "startup.h"
#include "AppInfo.h"
#include "filesystem.h"
#include "LogManager.h"
#include "NetworkServer.h"
#include "ResourceManager.h"
#include "SettingsManager.h"
#include "StringUtils.h"
#include "WebSocketServer.h"

using namespace std::chrono_literals;

static int common_main(int argc, char* argv[]);

static void WINAPI ServiceMain(DWORD dwArgc, LPTSTR *lpszArgv);
static void ReportServiceStatus(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint);
static int  ProcessServiceCommand(int argc, char* argv[]);
static bool StopServiceIfRunning(SC_HANDLE service);
static SC_HANDLE EnsureServiceInstalled(SC_HANDLE service_control_manager, int argc, char* argv[]);
static int  StartServiceCommand(int argc, char* argv[]);

static char                  service_name[]             = APP_NAME;

/*---------------------------------------------------------*\
| service_name_w                                            |
|                                                           |
|   Wide-character copy of APP_NAME for the Unicode (W)     |
|   Service Control Manager / Shell APIs, which we must use |
|   when the executable path may contain non-ASCII chars    |
|   (otherwise the ANSI APIs corrupt the path).             |
\*---------------------------------------------------------*/
#define _WIDE_STRING(x) L##x
#define WIDE_STRING(x) _WIDE_STRING(x)
static const wchar_t         service_name_w[]           = WIDE_STRING(APP_NAME);

static SERVICE_TABLE_ENTRY   service_dispatch_table[]   = { { service_name, ServiceMain }, { NULL, NULL } };
static DWORD                 service_checkpoint         = 1;
static SERVICE_STATUS_HANDLE service_status_handle;
static SERVICE_STATUS        service_status;

static bool                  started_as_service;
static volatile bool         service_stop_requested;
static bool                  have_console;

static std::mutex            service_status_mutex;
static filesystem::path      service_settings_path;

/*---------------------------------------------------------*\
| Detection progress goes from 0 to 100 twice, passing an   |
| estimate based on that confuses the service manager       |
\*---------------------------------------------------------*/
static int                   detection_pass;
static unsigned int          lastpercent                = 101;

/*---------------------------------------------------------*\
| AttachParentConsole                                      |
|                                                           |
|   Enable stdout/stderr when launched from a terminal.     |
\*---------------------------------------------------------*/
static void AttachParentConsole()
{
    if(have_console)
    {
        return;
    }

    if(AttachConsole(ATTACH_PARENT_PROCESS))
    {
        freopen("CONIN$",  "r", stdin);
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        have_console = true;
    }
}

/*---------------------------------------------------------*\
| GetExecutablePath                                        |
|                                                           |
|   Get current executable path as filesystem::path.        |
|                                                           |
|   The path is taken from GetModuleFileNameW (UTF-16) and  |
|   constructed straight from the wide string. It must NOT   |
|   round-trip through a UTF-8 std::string: on this _MBCS    |
|   build filesystem::path(std::string) decodes the bytes    |
|   using the system ANSI code page (e.g. GBK/936), which    |
|   reinterprets the UTF-8 bytes incorrectly and corrupts    |
|   any non-ASCII characters (mojibake) in the path. The     |
|   corrupted path then propagates to the service_config     |
|   directory and the service settings file.                |
\*---------------------------------------------------------*/
static filesystem::path GetExecutablePath()
{
    WCHAR exe_path_wchar[MAX_PATH];
    GetModuleFileNameW(NULL, exe_path_wchar, MAX_PATH);

    return filesystem::path(std::wstring(exe_path_wchar));
}

/*---------------------------------------------------------*\
| GetExecutablePathW                                        |
|                                                           |
|   Get current executable path as a wide (UTF-16) string.  |
|                                                           |
|   Unlike GetExecutablePath() above, this does NOT round-  |
|   trip through a UTF-8 std::string. The UTF-8 bytes would |
|   be misinterpreted by the ANSI Win32 APIs on systems     |
|   whose active code page is not UTF-8 (e.g. GBK/936 on a  |
|   Chinese system), producing mojibake in the path.        |
|                                                           |
|   Use this whenever the path is destined for a wide (W)   |
|   Service Control Manager or Shell API.                   |
\*---------------------------------------------------------*/
static std::wstring GetExecutablePathW()
{
    WCHAR exe_path_wchar[MAX_PATH];
    GetModuleFileNameW(NULL, exe_path_wchar, MAX_PATH);

    return std::wstring(exe_path_wchar);
}

/*---------------------------------------------------------*\
| MultiByteToWideACP                                        |
|                                                           |
|   Convert a string in the system ANSI code page (the      |
|   encoding of argv[] and of the CRT in this _MBCS build)  |
|   to a UTF-16 std::wstring, for passing to wide Win32     |
|   APIs.                                                   |
\*---------------------------------------------------------*/
static std::wstring MultiByteToWideACP(const std::string& str)
{
    if(str.empty())
    {
        return std::wstring();
    }

    int length = MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.size(), NULL, 0);

    std::wstring result(length, L'\0');

    MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.size(), &result[0], length);

    return result;
}

/*---------------------------------------------------------*\
| GetExecutableDirectoryW                                   |
|                                                           |
|   Parent directory of the executable as a wide string.    |
|   Computed directly on the wide path so it never passes   |
|   through an ANSI code page.                             |
\*---------------------------------------------------------*/
static std::wstring GetExecutableDirectoryW()
{
    std::wstring exe_path = GetExecutablePathW();

    size_t separator = exe_path.find_last_of(L"\\/");
    if(separator == std::wstring::npos)
    {
        return std::wstring();
    }

    return exe_path.substr(0, separator);
}

/*---------------------------------------------------------*\
| GetServiceConfigurationDirectory                         |
\*---------------------------------------------------------*/
static filesystem::path GetServiceConfigurationDirectory()
{
    filesystem::path exe_path = GetExecutablePath();
    return exe_path.remove_filename() / filesystem::u8path("service_config");
}

/*---------------------------------------------------------*\
| GetServiceSettingsPath                                   |
\*---------------------------------------------------------*/
static filesystem::path GetServiceSettingsPath()
{
    return GetServiceConfigurationDirectory() / filesystem::u8path(APP_CONFIG_FILE_NAME);
}

/*---------------------------------------------------------*\
| GetDefaultUserConfigurationDirectory                      |
\*---------------------------------------------------------*/
static filesystem::path GetDefaultUserConfigurationDirectory()
{
    filesystem::path config_dir;
    const wchar_t* appdata = _wgetenv(L"APPDATA");

    if(appdata != NULL)
    {
        config_dir = appdata;
        config_dir.append(APP_CONFIG_DIR_NAME);
    }
    else
    {
        config_dir = "./";
    }

    return config_dir;
}

/*---------------------------------------------------------*\
| RemoveLegacyServiceEndpointFiles                         |
\*---------------------------------------------------------*/
static void RemoveLegacyServiceEndpointFiles()
{
    filesystem::path exe_path = GetExecutablePath();
    filesystem::path exe_dir  = exe_path.remove_filename();

    const char* legacy_files[] =
    {
        "rgb_server_service.json",
        "openrgb_service.json"
    };

    for(const char* legacy_file : legacy_files)
    {
        try
        {
            filesystem::path legacy_path = exe_dir / filesystem::u8path(legacy_file);
            if(filesystem::exists(legacy_path))
            {
                filesystem::remove(legacy_path);
            }
        }
        catch(...)
        {
            /* Best effort cleanup only. */
        }
    }
}

/*---------------------------------------------------------*\
| RemoveServiceConfigurationDirectory                       |
|                                                           |
|   Remove the service_config directory and everything in   |
|   it (configuration, runtime info and log files). Used    |
|   when the service is uninstalled so it doesn't leave     |
|   leftover files behind.                                  |
\*---------------------------------------------------------*/
static void RemoveServiceConfigurationDirectory()
{
    try
    {
        filesystem::path service_config_path = GetServiceConfigurationDirectory();

        if(filesystem::exists(service_config_path))
        {
            filesystem::remove_all(service_config_path);
        }
    }
    catch(...)
    {
        /* Best effort cleanup only. */
    }
}

/*---------------------------------------------------------*\
| LoadJsonFile                                             |
\*---------------------------------------------------------*/
static json LoadJsonFile(const filesystem::path& file_path)
{
    json data = json::object();

    try
    {
        if(filesystem::exists(file_path))
        {
            std::ifstream file(file_path, std::ios::in | std::ios::binary);
            if(file)
            {
                file >> data;
            }
        }
    }
    catch(...)
    {
        data = json::object();
    }

    if(!data.is_object())
    {
        data = json::object();
    }

    return data;
}

/*---------------------------------------------------------*\
| SaveJsonFile                                             |
\*---------------------------------------------------------*/
static void SaveJsonFile(const filesystem::path& file_path, const json& data)
{
    filesystem::create_directories(file_path.parent_path());

    std::ofstream file(file_path, std::ios::out | std::ios::binary | std::ios::trunc);
    if(file)
    {
        file << data.dump(4);
    }
}

/*---------------------------------------------------------*\
| WriteServiceRuntimeInfo                                  |
\*---------------------------------------------------------*/
static void WriteServiceRuntimeInfo(unsigned short port, bool running)
{
    if(service_settings_path.empty())
    {
        service_settings_path = GetServiceSettingsPath();
    }

    try
    {
        json data = LoadJsonFile(service_settings_path);
        json service_settings = data.value("Service", json::object());

        if(!service_settings.is_object())
        {
            service_settings = json::object();
        }

        service_settings["name"]           = "RGB Server";
        service_settings["host"]           = "127.0.0.1";
        service_settings["port"]           = port;
        service_settings["websocket_port"] = port;
        service_settings["pid"]            = running ? GetCurrentProcessId() : 0;
        service_settings["running"]        = running;

        data["Service"] = service_settings;
        SaveJsonFile(service_settings_path, data);
    }
    catch(...)
    {
        /* Best effort; WebSocketServer will try again once it is listening. */
    }
}

/*---------------------------------------------------------*\
| WriteServiceRuntimeInfoToSettings                        |
\*---------------------------------------------------------*/
static void WriteServiceRuntimeInfoToSettings(unsigned short port, bool running)
{
    SettingsManager* settings_manager = ResourceManager::get()->GetSettingsManager();

    if(!settings_manager)
    {
        return;
    }

    json service_settings = settings_manager->GetSettings("Service");

    if(!service_settings.is_object())
    {
        service_settings = json::object();
    }

    service_settings["name"]           = "RGB Server";
    service_settings["host"]           = "127.0.0.1";
    service_settings["port"]           = port;
    service_settings["websocket_port"] = port;
    service_settings["pid"]            = running ? GetCurrentProcessId() : 0;
    service_settings["running"]        = running;

    settings_manager->SetSettings("Service", service_settings);
    settings_manager->SaveSettings();
}

/*---------------------------------------------------------*\
| AppendServiceLog                                         |
\*---------------------------------------------------------*/
static void AppendServiceLog(const std::string& message)
{
    LOG_INFO("[service] %s", message.c_str());
}

/*---------------------------------------------------------*\
| GetConfiguredServicePort                                 |
\*---------------------------------------------------------*/
static unsigned short GetConfiguredServicePort()
{
    if(service_settings_path.empty())
    {
        service_settings_path = GetServiceSettingsPath();
    }

    json data = LoadJsonFile(service_settings_path);
    json service_settings = data.value("Service", json::object());

    if(service_settings.is_object())
    {
        int port = service_settings.value("websocket_port", service_settings.value("port", 6743));
        if((port >= 1024) && (port <= 65535))
        {
            return (unsigned short)port;
        }
    }

    return 6743;
}

/*---------------------------------------------------------*\
| SaveConfiguredServicePort                                |
\*---------------------------------------------------------*/
static void SaveConfiguredServicePort(unsigned short port)
{
    service_settings_path = GetServiceSettingsPath();

    json data = LoadJsonFile(service_settings_path);
    json service_settings = data.value("Service", json::object());

    if(!service_settings.is_object())
    {
        service_settings = json::object();
    }

    service_settings["name"]           = "RGB Server";
    service_settings["host"]           = "127.0.0.1";
    service_settings["port"]           = port;
    service_settings["websocket_port"] = port;
    service_settings["configuration_directory"] = GetDefaultUserConfigurationDirectory().u8string();

    data["Service"] = service_settings;
    SaveJsonFile(service_settings_path, data);
}

/*---------------------------------------------------------*\
| SaveConfiguredServiceDirectory                           |
\*---------------------------------------------------------*/
static void SaveConfiguredServiceDirectory()
{
    service_settings_path = GetServiceSettingsPath();

    json data = LoadJsonFile(service_settings_path);
    json service_settings = data.value("Service", json::object());

    if(!service_settings.is_object())
    {
        service_settings = json::object();
    }

    service_settings["configuration_directory"] = GetDefaultUserConfigurationDirectory().u8string();

    data["Service"] = service_settings;
    SaveJsonFile(service_settings_path, data);
}

/*---------------------------------------------------------*\
| GetConfiguredServiceDirectory                            |
\*---------------------------------------------------------*/
static filesystem::path GetConfiguredServiceDirectory()
{
    if(service_settings_path.empty())
    {
        service_settings_path = GetServiceSettingsPath();
    }

    json data = LoadJsonFile(service_settings_path);
    json service_settings = data.value("Service", json::object());

    if(service_settings.is_object())
    {
        std::string config_dir = service_settings.value("configuration_directory", "");
        if(!config_dir.empty())
        {
            return filesystem::u8path(config_dir);
        }
    }

    return GetDefaultUserConfigurationDirectory();
}

/*---------------------------------------------------------*\
| PrintWindowsError                                        |
\*---------------------------------------------------------*/
static void PrintWindowsError(const char* operation, DWORD error)
{
    std::string message = std::system_category().message(error);
    printf("%s failed with error code %lu \"%s\"\n", operation, error, message.c_str());

    if(error == ERROR_ACCESS_DENIED)
    {
        printf("Run this command from an elevated Administrator terminal.\n");
    }
}

/*---------------------------------------------------------*\
| HasServiceCommand                                        |
\*---------------------------------------------------------*/
static bool HasServiceCommand(int argc, char* argv[])
{
    for(int arg_idx = 1; arg_idx < argc; arg_idx++)
    {
        if((strcmp(argv[arg_idx], "--install_service") == 0)
        || (strcmp(argv[arg_idx], "--uninstall_service") == 0)
        || (strcmp(argv[arg_idx], "--start_service") == 0))
        {
            return true;
        }
    }

    return false;
}

/*---------------------------------------------------------*\
| IsServiceCommand                                         |
\*---------------------------------------------------------*/
static bool IsServiceCommand(int argc, char* argv[], const char* command)
{
    for(int arg_idx = 1; arg_idx < argc; arg_idx++)
    {
        if(strcmp(argv[arg_idx], command) == 0)
        {
            return true;
        }
    }

    return false;
}

/*---------------------------------------------------------*\
| ParsePortArgument                                        |
\*---------------------------------------------------------*/
static bool ParsePortArgument(const char* option, const char* argument, unsigned short* port)
{
    if((argument == NULL) || (argument[0] == '\0'))
    {
        printf("Error: Missing argument for %s\n", option);
        return false;
    }

    try
    {
        int port_value = std::stoi(argument);

        if((port_value < 1024) || (port_value > 65535))
        {
            printf("Error: Port out of range for %s: %d (1024-65535)\n", option, port_value);
            return false;
        }

        *port = (unsigned short)port_value;
        return true;
    }
    catch(...)
    {
        printf("Error: Invalid data in %s argument (expected a number in range 1024-65535)\n", option);
        return false;
    }
}

/*---------------------------------------------------------*\
| GetRequestedServicePort                                  |
\*---------------------------------------------------------*/
static bool GetRequestedServicePort(int argc, char* argv[], unsigned short* port, bool* port_specified)
{
    *port_specified = false;

    for(int arg_idx = 1; arg_idx < argc; arg_idx++)
    {
        if((strcmp(argv[arg_idx], "--port") == 0)
        || (strcmp(argv[arg_idx], "--websocket-port") == 0))
        {
            if((arg_idx + 1) >= argc)
            {
                printf("Error: Missing argument for %s\n", argv[arg_idx]);
                return false;
            }

            if(!ParsePortArgument(argv[arg_idx], argv[arg_idx + 1], port))
            {
                return false;
            }

            *port_specified = true;
            arg_idx++;
        }
        else if(strcmp(argv[arg_idx], "--server-port") == 0)
        {
            printf("Error: --server-port is not supported with --install_service. Use --port or --websocket-port for the service WebSocket port.\n");
            return false;
        }
    }

    return true;
}

/*---------------------------------------------------------*\
| IsProcessElevated                                        |
\*---------------------------------------------------------*/
static bool IsProcessElevated()
{
    HANDLE token = NULL;

    if(!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        return false;
    }

    TOKEN_ELEVATION elevation;
    DWORD return_length = 0;
    bool elevated = false;

    if(GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &return_length))
    {
        elevated = elevation.TokenIsElevated != 0;
    }

    CloseHandle(token);
    return elevated;
}

/*---------------------------------------------------------*\
| QuoteCommandLineArgument                                 |
\*---------------------------------------------------------*/
static std::string QuoteCommandLineArgument(const char* argument)
{
    std::string arg(argument);

    if(arg.find_first_of(" \t\"") == std::string::npos)
    {
        return arg;
    }

    std::string quoted = "\"";
    unsigned int backslashes = 0;

    for(char ch : arg)
    {
        if(ch == '\\')
        {
            backslashes++;
            continue;
        }

        if(ch == '"')
        {
            quoted.append(backslashes * 2 + 1, '\\');
            quoted.push_back(ch);
            backslashes = 0;
            continue;
        }

        quoted.append(backslashes, '\\');
        backslashes = 0;
        quoted.push_back(ch);
    }

    quoted.append(backslashes * 2, '\\');
    quoted.push_back('"');

    return quoted;
}

/*---------------------------------------------------------*\
| RelaunchElevated                                         |
\*---------------------------------------------------------*/
static int RelaunchElevated(int argc, char* argv[])
{
    /*-----------------------------------------------------*\
    | Use the wide APIs end-to-end. argv[] comes from the   |
    | CRT in the system ANSI code page, and the executable  |
    | path may contain non-ASCII characters; converting both |
    | to UTF-16 and calling ShellExecuteExW avoids the      |
    | mojibake that ShellExecuteExA would introduce.        |
    \*-----------------------------------------------------*/
    std::wstring exe_path_string    = GetExecutablePathW();
    std::wstring working_directory  = GetExecutableDirectoryW();
    std::string  parameters;

    for(int arg_idx = 1; arg_idx < argc; arg_idx++)
    {
        if(!parameters.empty())
        {
            parameters += " ";
        }

        parameters += QuoteCommandLineArgument(argv[arg_idx]);
    }

    std::wstring parameters_w = MultiByteToWideACP(parameters);

    SHELLEXECUTEINFOW shell_execute_info;
    memset(&shell_execute_info, 0, sizeof(shell_execute_info));

    shell_execute_info.cbSize       = sizeof(shell_execute_info);
    shell_execute_info.fMask        = SEE_MASK_NOCLOSEPROCESS;
    shell_execute_info.lpVerb       = L"runas";
    shell_execute_info.lpFile       = exe_path_string.c_str();
    shell_execute_info.lpParameters = parameters_w.c_str();
    shell_execute_info.lpDirectory  = working_directory.c_str();
    shell_execute_info.nShow        = SW_HIDE;

    if(!ShellExecuteExW(&shell_execute_info))
    {
        DWORD error = GetLastError();

        if(error == ERROR_CANCELLED)
        {
            printf("Administrator elevation was cancelled.\n");
        }
        else
        {
            PrintWindowsError("ShellExecuteEx", error);
        }

        return EXIT_FAILURE;
    }

    if(shell_execute_info.hProcess)
    {
        WaitForSingleObject(shell_execute_info.hProcess, INFINITE);

        DWORD exit_code = EXIT_SUCCESS;
        if(!GetExitCodeProcess(shell_execute_info.hProcess, &exit_code))
        {
            PrintWindowsError("GetExitCodeProcess", GetLastError());
            exit_code = EXIT_FAILURE;
        }

        CloseHandle(shell_execute_info.hProcess);

        if(exit_code == EXIT_SUCCESS)
        {
            printf("Service command completed successfully.\n");
        }
        else
        {
            printf("Service command failed with exit code %lu.\n", exit_code);
        }

        return (int)exit_code;
    }

    return EXIT_SUCCESS;
}

/*---------------------------------------------------------*\
| WaitForServiceState                                      |
\*---------------------------------------------------------*/
static bool WaitForServiceState(SC_HANDLE service, DWORD desired_state, DWORD timeout_ms)
{
    SERVICE_STATUS_PROCESS status;
    DWORD bytes_needed;
    DWORD elapsed_ms = 0;

    while(QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, (LPBYTE)&status, sizeof(status), &bytes_needed))
    {
        if(status.dwCurrentState == desired_state)
        {
            return true;
        }

        if(elapsed_ms >= timeout_ms)
        {
            return false;
        }

        Sleep(500);
        elapsed_ms += 500;
    }

    return false;
}

/*---------------------------------------------------------*\
| IsServiceRunning                                         |
|                                                          |
|   Query whether the service is currently running, using |
|   only the read-only access rights a non-elevated        |
|   process can obtain. Returns true only when the service |
|   is installed and reports SERVICE_RUNNING. Returns      |
|   false (without printing) when the service is not       |
|   installed, not running, or its status cannot be        |
|   queried, so the caller can fall back to the elevated   |
|   path.                                                  |
\*---------------------------------------------------------*/
static bool IsServiceRunning()
{
    SC_HANDLE service_control_manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);

    if(!service_control_manager)
    {
        return false;
    }

    SC_HANDLE service = OpenServiceW(service_control_manager, service_name_w, SERVICE_QUERY_STATUS);

    bool running = false;

    if(service)
    {
        SERVICE_STATUS_PROCESS status;
        DWORD bytes_needed;

        if(QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, (LPBYTE)&status, sizeof(status), &bytes_needed))
        {
            running = (status.dwCurrentState == SERVICE_RUNNING);
        }

        CloseServiceHandle(service);
    }

    CloseServiceHandle(service_control_manager);

    return running;
}

/*---------------------------------------------------------*\
| StartInstalledService                                    |
\*---------------------------------------------------------*/
static int StartInstalledService(SC_HANDLE service)
{
    if(StartServiceW(service, 0, NULL))
    {
        if(!WaitForServiceState(service, SERVICE_RUNNING, 30000))
        {
            printf("%s service start was requested but did not report running within 30 seconds.\n", service_name);
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
    }

    DWORD error = GetLastError();

    if(error == ERROR_SERVICE_ALREADY_RUNNING)
    {
        printf("%s service is already running.\n", service_name);
        return EXIT_SUCCESS;
    }

    PrintWindowsError("StartService", error);
    return EXIT_FAILURE;
}

/*---------------------------------------------------------*\
| EnsureServiceInstalled                                    |
|                                                           |
|   Creates the service if it does not exist, or updates    |
|   the existing service configuration to point at the      |
|   current executable. Also applies the requested port     |
|   and configuration directory settings.                   |
|                                                           |
|   Returns an open service handle on success, or NULL on   |
|   failure. The caller owns the handle and must close it   |
|   together with the service control manager handle.       |
\*---------------------------------------------------------*/
static SC_HANDLE EnsureServiceInstalled(SC_HANDLE service_control_manager, int argc, char* argv[])
{
    unsigned short requested_port = 6743;
    bool port_specified = false;

    if(!GetRequestedServicePort(argc, argv, &requested_port, &port_specified))
    {
        return NULL;
    }

    RemoveLegacyServiceEndpointFiles();

    std::wstring binary_path = L"\"" + GetExecutablePathW() + L"\"";

    SC_HANDLE service = CreateServiceW(
        service_control_manager,
        service_name_w,
        service_name_w,
        SERVICE_CHANGE_CONFIG | SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        binary_path.c_str(),
        NULL,
        NULL,
        NULL,
        NULL,
        NULL);

    if(!service)
    {
        DWORD error = GetLastError();

        if(error != ERROR_SERVICE_EXISTS)
        {
            PrintWindowsError("CreateService", error);
            return NULL;
        }

        service = OpenServiceW(service_control_manager, service_name_w, SERVICE_CHANGE_CONFIG | SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP);

        if(!service)
        {
            PrintWindowsError("OpenService", GetLastError());
            return NULL;
        }

        if(!ChangeServiceConfigW(
            service,
            SERVICE_WIN32_OWN_PROCESS,
            SERVICE_AUTO_START,
            SERVICE_ERROR_NORMAL,
            binary_path.c_str(),
            NULL,
            NULL,
            NULL,
            NULL,
            NULL,
            service_name_w))
        {
            PrintWindowsError("ChangeServiceConfig", GetLastError());
            CloseServiceHandle(service);
            return NULL;
        }

        printf("%s service already existed and was updated.\n", service_name);
    }
    else
    {
        printf("%s service installed.\n", service_name);
    }

    if(port_specified)
    {
        SaveConfiguredServicePort(requested_port);
        printf("%s service WebSocket port set to %u.\n", service_name, requested_port);
    }
    else if(!filesystem::exists(GetServiceSettingsPath()))
    {
        SaveConfiguredServicePort(6743);
    }

    SaveConfiguredServiceDirectory();

    return service;
}

/*---------------------------------------------------------*\
| InstallService                                           |
\*---------------------------------------------------------*/
static int InstallService(int argc, char* argv[])
{
    SC_HANDLE service_control_manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);

    if(!service_control_manager)
    {
        PrintWindowsError("OpenSCManager", GetLastError());
        return EXIT_FAILURE;
    }

    SC_HANDLE service = EnsureServiceInstalled(service_control_manager, argc, argv);

    if(!service)
    {
        CloseServiceHandle(service_control_manager);
        return EXIT_FAILURE;
    }

    if(!StopServiceIfRunning(service))
    {
        CloseServiceHandle(service);
        CloseServiceHandle(service_control_manager);
        return EXIT_FAILURE;
    }

    int start_result = StartInstalledService(service);

    CloseServiceHandle(service);
    CloseServiceHandle(service_control_manager);

    return start_result;
}

/*---------------------------------------------------------*\
| StopServiceIfRunning                                     |
\*---------------------------------------------------------*/
static bool StopServiceIfRunning(SC_HANDLE service)
{
    SERVICE_STATUS_PROCESS status;
    DWORD bytes_needed;

    if(!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, (LPBYTE)&status, sizeof(status), &bytes_needed))
    {
        PrintWindowsError("QueryServiceStatusEx", GetLastError());
        return false;
    }

    if(status.dwCurrentState == SERVICE_STOPPED)
    {
        return true;
    }

    if(status.dwCurrentState != SERVICE_STOP_PENDING)
    {
        SERVICE_STATUS stop_status;
        if(!ControlService(service, SERVICE_CONTROL_STOP, &stop_status))
        {
            DWORD error = GetLastError();
            if(error != ERROR_SERVICE_NOT_ACTIVE)
            {
                PrintWindowsError("ControlService", error);
                return false;
            }
        }
    }

    if(!WaitForServiceState(service, SERVICE_STOPPED, 30000))
    {
        printf("%s service did not stop within 30 seconds.\n", service_name);
        return false;
    }

    return true;
}

/*---------------------------------------------------------*\
| UninstallService                                         |
\*---------------------------------------------------------*/
static int UninstallService()
{
    SC_HANDLE service_control_manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);

    if(!service_control_manager)
    {
        PrintWindowsError("OpenSCManager", GetLastError());
        return EXIT_FAILURE;
    }

    SC_HANDLE service = OpenServiceW(service_control_manager, service_name_w, SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);

    if(!service)
    {
        DWORD error = GetLastError();

        if(error == ERROR_SERVICE_DOES_NOT_EXIST)
        {
            RemoveLegacyServiceEndpointFiles();
            RemoveServiceConfigurationDirectory();
            printf("%s service is not installed.\n", service_name);
            CloseServiceHandle(service_control_manager);
            return EXIT_SUCCESS;
        }

        PrintWindowsError("OpenService", error);
        CloseServiceHandle(service_control_manager);
        return EXIT_FAILURE;
    }

    if(!StopServiceIfRunning(service))
    {
        CloseServiceHandle(service);
        CloseServiceHandle(service_control_manager);
        return EXIT_FAILURE;
    }

    if(!DeleteService(service))
    {
        PrintWindowsError("DeleteService", GetLastError());
        CloseServiceHandle(service);
        CloseServiceHandle(service_control_manager);
        return EXIT_FAILURE;
    }

    RemoveLegacyServiceEndpointFiles();
    RemoveServiceConfigurationDirectory();
    printf("%s service uninstalled.\n", service_name);

    CloseServiceHandle(service);
    CloseServiceHandle(service_control_manager);

    return EXIT_SUCCESS;
}

/*---------------------------------------------------------*\
| StartServiceCommand                                      |
|                                                           |
|   Idempotently make sure the service is running:          |
|     - If it is not installed, install it (and start it)   |
|     - If it is installed but not running, start it        |
|     - If it is already running, do nothing                |
|                                                           |
|   If --port/--websocket-port is given, the configured     |
|   WebSocket port is updated in all cases. When the service|
|   is already running it is restarted so the new port takes|
|   effect immediately.                                     |
\*---------------------------------------------------------*/
static int StartServiceCommand(int argc, char* argv[])
{
    unsigned short requested_port = 6743;
    bool port_specified = false;

    if(!GetRequestedServicePort(argc, argv, &requested_port, &port_specified))
    {
        return EXIT_FAILURE;
    }

    SC_HANDLE service_control_manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);

    if(!service_control_manager)
    {
        PrintWindowsError("OpenSCManager", GetLastError());
        return EXIT_FAILURE;
    }

    SC_HANDLE service = OpenServiceW(service_control_manager, service_name_w,
        SERVICE_CHANGE_CONFIG | SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP);

    if(!service)
    {
        DWORD error = GetLastError();

        if(error != ERROR_SERVICE_DOES_NOT_EXIST)
        {
            PrintWindowsError("OpenService", error);
            CloseServiceHandle(service_control_manager);
            return EXIT_FAILURE;
        }

        /*-------------------------------------------------*\
        | Not installed yet: install and start it.          |
        | EnsureServiceInstalled applies the requested      |
        | port and configuration directory settings.        |
        \*-------------------------------------------------*/
        printf("%s service is not installed. Installing...\n", service_name);

        service = EnsureServiceInstalled(service_control_manager, argc, argv);

        if(!service)
        {
            CloseServiceHandle(service_control_manager);
            return EXIT_FAILURE;
        }
    }
    else
    {
        /*-------------------------------------------------*\
        | Already installed. Refresh the configuration so   |
        | any --port change is persisted, then decide       |
        | whether (re)starting is needed.                   |
        \*-------------------------------------------------*/
        if(port_specified)
        {
            SaveConfiguredServicePort(requested_port);
            printf("%s service WebSocket port set to %u.\n", service_name, requested_port);
        }

        SERVICE_STATUS_PROCESS status;
        DWORD bytes_needed;

        if(!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, (LPBYTE)&status, sizeof(status), &bytes_needed))
        {
            PrintWindowsError("QueryServiceStatusEx", GetLastError());
            CloseServiceHandle(service);
            CloseServiceHandle(service_control_manager);
            return EXIT_FAILURE;
        }

        if(status.dwCurrentState == SERVICE_RUNNING)
        {
            if(!port_specified)
            {
                /*---------------------------------------------*\
                | Already running and no port change requested: |
                | nothing to do.                                |
                \*---------------------------------------------*/
                printf("%s service is already running.\n", service_name);
                CloseServiceHandle(service);
                CloseServiceHandle(service_control_manager);
                return EXIT_SUCCESS;
            }

            /*---------------------------------------------*\
            | Port changed while running: restart so the    |
            | new port takes effect.                         |
            \*---------------------------------------------*/
            printf("%s service is running; restarting to apply new port %u.\n", service_name, requested_port);

            if(!StopServiceIfRunning(service))
            {
                CloseServiceHandle(service);
                CloseServiceHandle(service_control_manager);
                return EXIT_FAILURE;
            }
        }
    }

    int start_result = StartInstalledService(service);

    CloseServiceHandle(service);
    CloseServiceHandle(service_control_manager);

    return start_result;
}

/*---------------------------------------------------------*\
| ProcessServiceCommand                                    |
\*---------------------------------------------------------*/
static int ProcessServiceCommand(int argc, char* argv[])
{
    /*-----------------------------------------------------*\
    | Fast path for --start_service: if the service is      |
    | already running and no port change was requested,     |
    | there is nothing to do. Skip elevation entirely so    |
    | the command does not trigger a UAC prompt for the     |
    | common "make sure it's running" case. Any other case  |
    | (not installed, not running, port change) needs       |
    | administrative rights, so we fall through to the      |
    | elevated path below.                                  |
    \*-----------------------------------------------------*/
    if(!IsProcessElevated()
    && IsServiceCommand(argc, argv, "--start_service"))
    {
        unsigned short requested_port = 6743;
        bool port_specified = false;

        if(!GetRequestedServicePort(argc, argv, &requested_port, &port_specified))
        {
            /* Invalid --port argument: fail now rather than after elevating. */
            return EXIT_FAILURE;
        }

        if(!port_specified && IsServiceRunning())
        {
            printf("%s service is already running.\n", service_name);
            return EXIT_SUCCESS;
        }
    }

    if(!IsProcessElevated())
    {
        return RelaunchElevated(argc, argv);
    }

    if(IsServiceCommand(argc, argv, "--install_service"))
    {
        return InstallService(argc, argv);
    }

    if(IsServiceCommand(argc, argv, "--uninstall_service"))
    {
        return UninstallService();
    }

    if(IsServiceCommand(argc, argv, "--start_service"))
    {
        return StartServiceCommand(argc, argv);
    }

    return EXIT_FAILURE;
}

/*---------------------------------------------------------*\
| RequestApplicationShutdown                               |
\*---------------------------------------------------------*/
static void RequestApplicationShutdown()
{
    service_stop_requested = true;
    startup_request_shutdown();
    ResourceManager::get()->StopDeviceDetection();

    WebSocketServer* ws_server = ResourceManager::get()->GetWebSocketServer();
    if(ws_server)
    {
        ws_server->StopServer();
    }

    QCoreApplication* app = QCoreApplication::instance();
    if(app)
    {
        QMetaObject::invokeMethod(app, "quit", Qt::QueuedConnection);
    }
}

/*---------------------------------------------------------*\
| ServiceStarted                                           |
\*---------------------------------------------------------*/
static void ServiceStarted()
{
    AppendServiceLog("Service reported running.");
    ReportServiceStatus(SERVICE_RUNNING, NO_ERROR, 0);
}

/*---------------------------------------------------------*\
| ServiceStartupProgress                                    |
|                                                           |
|   Report detection progress when running as a service     |
\*---------------------------------------------------------*/
static void ServiceStartupProgress(void*)
{
    unsigned int percent = ResourceManager::get()->GetDetectionPercent();
    unsigned int estimate;

    percent = std::clamp(percent, 0u, 100u);

    if(lastpercent > percent)
    {
        detection_pass += 1;
    }

    lastpercent = percent;

    switch(detection_pass)
    {
        case 0:
            percent = 0;
            break;
        case 1:
            percent = percent * 4 / 5;
            break;
        case 2:
            percent = percent / 5 + 80;
            break;
        default:
            percent = 100;
            break;
    }

    estimate = (100 - percent) / 5 + 10;

    ReportServiceStatus(SERVICE_START_PENDING, NO_ERROR, estimate * 1000);
}

/*---------------------------------------------------------*\
| ReportServiceStatus                                       |
|                                                           |
|   As of writing this (24H2) there are 7 possible service  |
|   states:                                                 |
|       SERVICE_START_PENDING, SERVICE_RUNNING,             |
|       SERVICE_STOP_PENDING, SERVICE_STOPPED,              |
|       SERVICE_PAUSE_PENDING, SERVICE_PAUSED,              |
|       SERVICE_CONTINUE_PENDING                            |
|                                                           |
|   We don't accept pause requests, so only start_pending   |
|   through stopped are possible.                           |
|                                                           |
|   Control requests can arrive asynchronously so we take   |
|   some precautions here not to report an out of order     |
|   status.                                                 |
\*---------------------------------------------------------*/
static void ReportServiceStatus(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint)
{
    static DWORD last_state = SERVICE_START_PENDING;

    LOG_TRACE("ReportServiceStatus(%lu, %lu, %lu), PID %lu Thread %lu", dwCurrentState, dwWin32ExitCode, dwWaitHint, GetCurrentProcessId(), GetCurrentThreadId());

    service_status_mutex.lock();

    switch(last_state)
    {
        case SERVICE_RUNNING:
            /*---------------------------------------------*\
            | Don't go back to SERVICE_START_PENDING        |
            \*---------------------------------------------*/
            if(dwCurrentState == SERVICE_START_PENDING)
            {
                service_status_mutex.unlock();
                return;
            }
            break;

        case SERVICE_STOP_PENDING:
            /*---------------------------------------------*\
            | Don't go back to starting or running          |
            \*---------------------------------------------*/
            if((dwCurrentState != SERVICE_STOP_PENDING) && (dwCurrentState != SERVICE_STOPPED))
            {
                service_status_mutex.unlock();
                return;
            }
            break;

        case SERVICE_STOPPED:
            /*---------------------------------------------*\
            | Don't call SetServiceStatus anymore after     |
            | SERVICE_STOPPED has been reported             |
            \*---------------------------------------------*/
            service_status_mutex.unlock();
            return;
    }

    switch(dwCurrentState)
    {
        case SERVICE_START_PENDING:
        case SERVICE_STOP_PENDING:
            service_status.dwControlsAccepted = 0;
            service_status.dwCheckPoint = service_checkpoint;
            service_checkpoint += 1;
            break;

        case SERVICE_RUNNING:
        case SERVICE_STOPPED:
            service_status.dwCheckPoint = 0;
            service_status.dwControlsAccepted =
                // SERVICE_ACCEPT_POWEREVENT   |
                SERVICE_ACCEPT_STOP         |
                SERVICE_ACCEPT_PRESHUTDOWN;
            break;

        default:
            /*---------------------------------------------*\
            | Shouldn't happen                              |
            \*---------------------------------------------*/
            service_status_mutex.unlock();
            return;
    }

    last_state                      = dwCurrentState;
    service_status.dwCurrentState               = dwCurrentState;
    service_status.dwWin32ExitCode              = dwWin32ExitCode;
    service_status.dwServiceSpecificExitCode    = (dwWin32ExitCode == ERROR_SERVICE_SPECIFIC_ERROR) ? 1 : 0;
    service_status.dwWaitHint                   = dwWaitHint;

    SetServiceStatus(service_status_handle, &service_status);

    service_status_mutex.unlock();

    return;
}

/*---------------------------------------------------------*\
| ServiceControlHandler                                     |
|                                                           |
|   Handler function to register with                       |
|   RegisterServiceControlHandlerEx, to handle service      |
|   control operations (stop/shutdown).                     |
\*---------------------------------------------------------*/
static DWORD WINAPI ServiceControlHandler(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext)
{
    DWORD retval = NO_ERROR;

    LOG_TRACE("ServiceControlHandler(%lu, %lu, %p, %p) called, PID %lu Thread %lu", dwControl, dwEventType, lpEventData, lpContext, GetCurrentProcessId(), GetCurrentThreadId());

    switch(dwControl)
    {
        /*-------------------------------------------------*\
        | TODO either this or                               |
        | RegisterSuspendResumeNotification somewhere       |
        \*-------------------------------------------------*/
        // case SERVICE_CONTROL_POWEREVENT:
        case SERVICE_CONTROL_INTERROGATE:
            /*---------------------------------------------*\
            | Should do nothing but return NO_ERROR         |
            \*---------------------------------------------*/
            break;

        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_PRESHUTDOWN:
            ReportServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 10000);
            RequestApplicationShutdown();
            break;

        default:
            retval = ERROR_CALL_NOT_IMPLEMENTED;
            break;
    }

    return retval;
}

/*---------------------------------------------------------*\
| LogEvent                                                  |
|                                                           |
|   Log an event to the service log when running as a       |
|   service.  Only used for startup and shutdown events.    |
\*---------------------------------------------------------*/
static void LogEvent(const char *fmt, ...)
{
    va_list args1;
    va_list args2;

    /*-----------------------------------------------------*\
    | Handle variable arguments                             |
    \*-----------------------------------------------------*/
    va_start(args1, fmt);
    va_copy(args2, args1);

    /*-----------------------------------------------------*\
    | Determine string length and allocate buffer           |
    \*-----------------------------------------------------*/
    int     len = vsnprintf(NULL, 0, fmt, args1);
    char *  buf = (char *)malloc(len + 1);

    /*-----------------------------------------------------*\
    | Fill the string buffer                                |
    \*-----------------------------------------------------*/
    va_end(args1);
    vsnprintf(buf, len + 1, fmt, args2);
    va_end(args2);

    /*-----------------------------------------------------*\
    | If we somehow ended up here with a working console    |
    | then just print it                                    |
    \*-----------------------------------------------------*/
    if(have_console)
    {
        printf("%s\n", buf);
        free(buf);
        return;
    }

    /*-----------------------------------------------------*\
    | Otherwise, register event source with our service     |
    | name and report event to the event log                |
    \*-----------------------------------------------------*/
    HANDLE event_source = RegisterEventSource(NULL, service_name);

    if(event_source)
    {
        LPCSTR strings[2] = { service_name, buf };
        ReportEvent(
            event_source,
            EVENTLOG_ERROR_TYPE,
            0,
            1, // error #
            NULL,
            2,
            0,
            strings,
            NULL);
        DeregisterEventSource(event_source);
    }
    else
    {
        /*-------------------------------------------------*\
        | No console, no logmanager, no eventlog, no more   |
        | ideas. Sometimes it's OK to give up.              |
        \*-------------------------------------------------*/
    }

    /*-----------------------------------------------------*\
    | Free the buffer                                       |
    \*-----------------------------------------------------*/
    free(buf);
}

/*---------------------------------------------------------*\
| LogError                                                  |
|                                                           |
|   Log an error event when running as a service            |
\*---------------------------------------------------------*/
static void LogError(const char *fn, DWORD lasterror)
{
    std::string message = std::system_category().message(lasterror);
    LogEvent("%s failed with error code %lu \"%s\"", fn, lasterror, message.c_str());
}

/*---------------------------------------------------------*\
| ServiceMain                                               |
|                                                           |
|   Figures out whether we are started as a service, calls  |
|   common_main one way or another.                         |
\*---------------------------------------------------------*/
static void WINAPI ServiceMain(DWORD dwArgc, LPTSTR *lpszArgv)
{
    /*-----------------------------------------------------*\
    | Initialize service status                             |
    \*-----------------------------------------------------*/
    service_status.dwServiceType                = SERVICE_WIN32_OWN_PROCESS;
    service_status.dwServiceSpecificExitCode    = 0;

    started_as_service                          = true;
    service_status_handle                       = RegisterServiceCtrlHandlerEx(service_name, ServiceControlHandler, NULL);

    /*-----------------------------------------------------*\
    | Exit if service status handle is invalid              |
    \*-----------------------------------------------------*/
    if(service_status_handle == 0)
    {
        LogError("RegisterServiceCtrlHandlerEx", GetLastError());
        return;
    }

    /*-----------------------------------------------------*\
    | Report service status start pending                   |
    \*-----------------------------------------------------*/
    ReportServiceStatus(SERVICE_START_PENDING, NO_ERROR, 30000);

    /*-----------------------------------------------------*\
    | Perform common main processing                        |
    \*-----------------------------------------------------*/
    int exitval = common_main(dwArgc, lpszArgv);

    /*-----------------------------------------------------*\
    | Log if exit was unsuccessful                          |
    \*-----------------------------------------------------*/
    if(exitval != EXIT_SUCCESS)
    {
        LogEvent("%s finishing with exit code %d", APP_NAME, exitval);
    }

    /*-----------------------------------------------------*\
    | Report service status stopped                         |
    \*-----------------------------------------------------*/
    ReportServiceStatus(SERVICE_STOPPED, NO_ERROR, 0);
}

/*---------------------------------------------------------*\
| main                                                      |
|                                                           |
|   Entry point, checks if started as a service and then    |
|   calls the common main processing                        |
\*---------------------------------------------------------*/
int main(int argc, char* argv[])
{
    started_as_service = false;

    if(HasServiceCommand(argc, argv))
    {
        AttachParentConsole();
        return ProcessServiceCommand(argc, argv);
    }

    /*-----------------------------------------------------*\
    | This will call ServiceMain() if we are started as a   |
    | service                                               |
    \*-----------------------------------------------------*/
    bool sscd = StartServiceCtrlDispatcher(service_dispatch_table);

    /*-----------------------------------------------------*\
    | ... returns a specific error code otherwise           |
    \*-----------------------------------------------------*/
    DWORD lasterror = GetLastError();

    if(sscd)
    {
        if(started_as_service)
        {
            /*---------------------------------------------*\
            | StartServiceCtrlDispatcher reported success,  |
            | ServiceMain was reached. Don't log anything,  |
            | it's the normal exit path when a service is   |
            | stopped.                                      |
            \*---------------------------------------------*/
            return EXIT_SUCCESS;
        }
        else
        {
            /*---------------------------------------------*\
            | Should not happen                             |
            \*---------------------------------------------*/
            LogEvent("StartServiceCtrlDispatcher returned true, but ServiceMain has not been reached.");
            return EXIT_FAILURE;
        }
    }

    /*-----------------------------------------------------*\
    | StartServiceCtrlDispatcher reported failure.          |
    | First, check that the error isn't the one returned    |
    | when it's called from an app                          |
    \*-----------------------------------------------------*/
    if(lasterror != ERROR_FAILED_SERVICE_CONTROLLER_CONNECT)
    {
        LogError("StartServiceCtrlDispatcher", lasterror);
        return EXIT_FAILURE;
    }

    /*-----------------------------------------------------*\
    | Now we are quite sure we are not started as a service |
    \*-----------------------------------------------------*/
    if(started_as_service)
    {
        /*-------------------------------------------------*\
        | ... so this should not happen                     |
        \*-------------------------------------------------*/
        LogEvent("StartServiceCtrlDispatcher returned false, but ServiceMain has been reached.");
        return EXIT_FAILURE;
    }

    /*-----------------------------------------------------*\
    | Code that needs to be run at startup when we are      |
    | started as an app and not as a service goes here.     |
    \*-----------------------------------------------------*/

    /*-----------------------------------------------------*\
    | Attach console output                                 |
    \*-----------------------------------------------------*/
    AttachParentConsole();

    return common_main(argc, argv);
}

/*---------------------------------------------------------*\
| InitializeTimerResolutionThreadFunction (Win32)           |
|                                                           |
|   On Windows, the default timer resolution is 15.6ms.     |
|   For higher accuracy delays, the timer resolution should |
|   be set to a shorter interval.  The shortest interval    |
|   that can be set is 0.5ms.                               |
\*---------------------------------------------------------*/
void InitializeTimerResolutionThreadFunction()
{
    /*-----------------------------------------------------*\
    | NtSetTimerResolution function pointer type            |
    \*-----------------------------------------------------*/
    typedef unsigned int NTSTATUS;
    typedef NTSTATUS (*NTSETTIMERRESOLUTION)(ULONG DesiredResolution, BOOLEAN SetResolution, PULONG CurrentResolution);

    /*-----------------------------------------------------*\
    | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION = 4, |
    | isn't defined in Win10 headers                        |
    \*-----------------------------------------------------*/
    PROCESS_POWER_THROTTLING_STATE PowerThrottlingState { PROCESS_POWER_THROTTLING_CURRENT_VERSION, 4, 0 };

    ULONG                CurrentResolution;
    HMODULE              NtDllHandle;
    NTSETTIMERRESOLUTION NtSetTimerResolution;

    /*-----------------------------------------------------*\
    | Load ntdll.dll and get pointer to NtSetTimerResolution|
    \*-----------------------------------------------------*/
    NtDllHandle             = LoadLibrary("ntdll.dll");
    NtSetTimerResolution    = (NTSETTIMERRESOLUTION)GetProcAddress(NtDllHandle, "NtSetTimerResolution");

    /*-----------------------------------------------------*\
    | Windows 11 requires                                   |
    | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION      |
    \*-----------------------------------------------------*/
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &PowerThrottlingState, sizeof(PowerThrottlingState));

    /*-----------------------------------------------------*\
    | Call NtSetTimerResolution to set timer resolution to  |
    | 0.5ms every 500ms                                     |
    \*-----------------------------------------------------*/
    while(1)
    {
        NtSetTimerResolution(5000, TRUE, &CurrentResolution);

        std::this_thread::sleep_for(500ms);
    }
}

/*---------------------------------------------------------*\
| WaitWhileServerOnline                                     |
|                                                           |
|   Wait while NetworkServer is online and return only when |
|   it has shut down                                        |
\*---------------------------------------------------------*/
static void WaitWhileServerOnline(NetworkServer* srv)
{
    while(srv->GetOnline())
    {
        std::this_thread::sleep_for(1s);
        if(service_stop_requested)
        {
            srv->StopServer();
        }
    };
}

/*---------------------------------------------------------*\
| common_main                                               |
|                                                           |
|   Common entry functionality after determining whether we |
|   were started as a service or not.                       |
\*---------------------------------------------------------*/
static int common_main(int argc, char* argv[])
{
    unsigned int ret_flags;
    std::unique_ptr<QCoreApplication> service_app;

    if(started_as_service)
    {
        startup_set_service_mode(true);
        startup_set_service_started_callback(ServiceStarted);

        if(QCoreApplication::instance() == nullptr)
        {
            service_app.reset(new QCoreApplication(argc, argv));
            LOG_TRACE("[service] QCoreApplication created before ResourceManager");
        }

        /*-------------------------------------------------*\
        | Passing command line arguments to a service is    |
        | difficult, can cause all kinds of trouble and     |
        | doesn't have a way to warn about them             |
        \*-------------------------------------------------*/
        ret_flags = RET_FLAG_START_WEBSOCKET_SERVER | RET_FLAG_NO_AUTO_CONNECT;

        /*-------------------------------------------------*\
        | Use service_config for service runtime endpoint   |
        | state only. Application config and logs remain in |
        | the normal %APPDATA%\RGB Server directory.        |
        \*-------------------------------------------------*/
        service_settings_path = GetServiceSettingsPath();

        filesystem::create_directories(service_settings_path.parent_path());
        RemoveLegacyServiceEndpointFiles();

        filesystem::path app_config_path = GetConfiguredServiceDirectory();
        filesystem::create_directories(app_config_path);
        startup_set_service_configuration_directory(app_config_path);
        LogManager::get()->setServiceLogDirectory(app_config_path);

        unsigned short service_port = GetConfiguredServicePort();
        WebSocketServer * ws_server = ResourceManager::get()->GetWebSocketServer();

        ws_server->SetHost("127.0.0.1");
        ws_server->SetPort(service_port);
        ws_server->SetEnabled(true);
        ws_server->SetEndpointFilePath(service_settings_path);

        WriteServiceRuntimeInfo(service_port, false);
        WriteServiceRuntimeInfoToSettings(service_port, false);
        AppendServiceLog("Service startup requested. WebSocket target 127.0.0.1:" + std::to_string(service_port));

        /*-------------------------------------------------*\
        | The WebSocket server is started from startup()     |
        | once the Qt event loop is ready, so that all       |
        | socket I/O happens on the owning thread.  Starting |
        | it here would race the event loop and break        |
        | signal delivery.                                   |
        \-------------------------------------------------*/

        if(service_stop_requested)
        {
            AppendServiceLog("Service stop was already requested before startup.");
            return EXIT_SUCCESS;
        }
    }
    else
    {
        /*-------------------------------------------------*\
        | Perform CLI pre-detection processing to get       |
        | return flags                                      |
        \*-------------------------------------------------*/
        ret_flags = cli_pre_detection(argc, argv);
    }

    /*-----------------------------------------------------*\
    | Start timer resolution correction thread              |
    \*-----------------------------------------------------*/
    std::thread * InitializeTimerResolutionThread;
    InitializeTimerResolutionThread = new std::thread(InitializeTimerResolutionThreadFunction);
    InitializeTimerResolutionThread->detach();

    /*-----------------------------------------------------*\
    | Initialize ResourceManager                            |
    \*-----------------------------------------------------*/
    ResourceManager::get()->Initialize(
        !(ret_flags & RET_FLAG_NO_AUTO_CONNECT),
        !(ret_flags & RET_FLAG_NO_DETECT),
        ret_flags & RET_FLAG_START_SERVER,
        ret_flags & RET_FLAG_CLI_POST_DETECTION);

    /*-----------------------------------------------------*\
    | If running as a service, register the service startup |
    | progress callback with ResourceManager to report      |
    | startup progress based on detection progress.         |
    \*-----------------------------------------------------*/
    if(started_as_service)
    {
        ResourceManager::get()->RegisterDetectionProgressCallback(ServiceStartupProgress, NULL);
    }

    /*-----------------------------------------------------*\
    | Perform application startup and run the application.  |
    | This call returns only when the GUI application is    |
    | closing or if not running the GUI.                    |
    \*-----------------------------------------------------*/
    int exitval = startup(argc, argv, ret_flags);

    /*-----------------------------------------------------*\
    | If running as a service, unregister the service       |
    | startup progress callback when shutting down.         |
    \*-----------------------------------------------------*/
    if(started_as_service)
    {
        ResourceManager::get()->UnregisterDetectionProgressCallback(ServiceStartupProgress, NULL);
    }

    /*-----------------------------------------------------*\
    | Perform ResourceManager cleanup before exiting        |
    \*-----------------------------------------------------*/
    if(started_as_service && (exitval != EXIT_SUCCESS))
    {
        WebSocketServer* ws_server = ResourceManager::get()->GetWebSocketServer();
        std::string websocket_error = ws_server ? ws_server->GetLastError() : "WebSocket server object is null";
        AppendServiceLog("Service startup failed: " + websocket_error);
    }

    if(started_as_service && startup_shutdown_requested())
    {
        AppendServiceLog("Service cleanup skipped for shutdown request.");
    }
    else
    {
        ResourceManager::get()->Cleanup();
    }

    if(started_as_service)
    {
        unsigned short service_port = GetConfiguredServicePort();
        WriteServiceRuntimeInfoToSettings(service_port, false);
        WriteServiceRuntimeInfo(service_port, false);
        AppendServiceLog("Service exited.");
    }

    LOG_TRACE("%s finishing with exit code %d", APP_NAME, exitval);

    return exitval;
}
