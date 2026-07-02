/*---------------------------------------------------------*\
| startup.cpp                                               |
|                                                           |
|   Startup for the OpenRGB application                     |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "cli.h"
#include "ResourceManager.h"
#include "NetworkServer.h"
#include "WebSocketServer.h"
#include "startup.h"
#include "LogManager.h"
#include "AppInfo.h"
#include <atomic>
#include <memory>
#include <QCoreApplication>
#include <QTimer>

#ifndef RGBSERVER_HEADLESS
#include <QApplication>
#include "OpenRGBDialog.h"

#ifdef __APPLE__
#include "macutils.h"
#endif
#endif

#ifdef __linux__
#include <csignal>
#endif

/******************************************************************************************\
*                                                                                          *
*   Linux signal handler                                                                   *
*                                                                                          *
\******************************************************************************************/
#ifdef __linux__
void sigHandler(int s)
{
    std::signal(s, SIG_DFL);
    qApp->quit();
}
#endif

static bool startup_service_mode = false;
static filesystem::path startup_service_configuration_directory;
static std::atomic<bool> startup_shutdown_requested_flag(false);
static void (*startup_service_started_callback)(void) = nullptr;

void startup_set_service_mode(bool service_mode)
{
    startup_service_mode = service_mode;
}

bool startup_is_service_mode()
{
    return startup_service_mode;
}

void startup_set_service_configuration_directory(const filesystem::path& directory)
{
    startup_service_configuration_directory = directory;
}

filesystem::path startup_get_service_configuration_directory()
{
    return startup_service_configuration_directory;
}

void startup_set_service_started_callback(void (*callback)(void))
{
    startup_service_started_callback = callback;
}

void startup_request_shutdown()
{
    startup_shutdown_requested_flag = true;
}

bool startup_shutdown_requested()
{
    return startup_shutdown_requested_flag;
}

/******************************************************************************************\
*                                                                                          *
*   startup                                                                                *
*                                                                                          *
*       Opens the main windows or starts the server                                        *
*                                                                                          *
\******************************************************************************************/
int startup(int argc, char* argv[], unsigned int ret_flags)
{
    /*-----------------------------------------------------*\
    | Initialize exit value, which will be returned on exit |
    | in main()                                             |
    \*-----------------------------------------------------*/
    int exitval = EXIT_SUCCESS;

    /*-----------------------------------------------------*\
    | If the command line parser indicates that the GUI     |
    | should run, or if there were no command line          |
    | arguments, start the GUI.                             |
    \*-----------------------------------------------------*/
    if(ret_flags & RET_FLAG_START_GUI)
    {
#ifdef RGBSERVER_HEADLESS
        LOG_ERROR("[startup] GUI mode is not available in this headless build");
        return EXIT_FAILURE;
#else
        LOG_TRACE("[main] initializing GUI");

        /*-------------------------------------------------*\
        | Enable high DPI scaling support (Qt5 only)        |
        \*-------------------------------------------------*/
        #if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps,    true);
            QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
        #endif

        /*-------------------------------------------------*\
        | Enable high DPI fractional scaling support on     |
        | Windows                                           |
        \*-------------------------------------------------*/
        #if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0) && defined(Q_OS_WIN)
            QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
        #endif

        /*-------------------------------------------------*\
        | Create Qt application                             |
        \*-------------------------------------------------*/
        QApplication a(argc, argv);
        QGuiApplication::setDesktopFileName(APP_DESKTOP_ID);
        LOG_TRACE("[startup] QApplication created");

        /*-------------------------------------------------*\
        | Main UI widget                                    |
        \*-------------------------------------------------*/
        OpenRGBDialog dlg;
        LOG_TRACE("[startup] Dialog created");

        if(ret_flags & RET_FLAG_I2C_TOOLS)
        {
            dlg.AddI2CToolsPage();
        }

        dlg.AddClientTab();

        if(ret_flags & RET_FLAG_START_MINIMIZED)
        {
#ifdef _WIN32
            /*---------------------------------------------*\
            | Show the window always, even if it will       |
            | immediately be hidden.  On Windows, events    |
            | are not delivered to nativeEventFilter (for   |
            | SuspendResume) until the window has been      |
            | shown once.                                   |
            |                                               |
            | TODO Try using                                |
            | RegisterSuspendResumeNotification instead,    |
            | that should work in headless mode too         |
            \*---------------------------------------------*/
            dlg.showMinimized();
#endif
#ifdef __APPLE__
            MacUtils::ToggleApplicationDocklessState(false);
#endif
            dlg.hide();
        }
        else
        {
            dlg.show();
        }

        LOG_TRACE("[main] Ready to exec() the dialog");

#ifdef __linux__
        std::signal(SIGINT,  sigHandler);
        std::signal(SIGTERM, sigHandler);
#endif

        exitval = a.exec();
#endif
    }
    else
    {
        /*-------------------------------------------------*\
        | CLI mode: Create QCoreApplication to provide     |
        | event loop for WebSocketServer and other Qt      |
        | network services                                 |
        \*-------------------------------------------------*/
        QCoreApplication* cli_app = QCoreApplication::instance();
        std::unique_ptr<QCoreApplication> cli_app_owner;

        if(cli_app == nullptr)
        {
            cli_app_owner.reset(new QCoreApplication(argc, argv));
            cli_app = cli_app_owner.get();
            LOG_TRACE("[startup] QCoreApplication created for CLI mode");
        }
        else
        {
            LOG_TRACE("[startup] Reusing existing QCoreApplication for CLI mode");
        }

        /*-------------------------------------------------*\
        | Wait for initialization to finish.  In service    |
        | mode the Qt event loop must start first so        |
        | WebSocketServer calls queued from ResourceManager |
        | can run on the owning thread.                     |
        \*-------------------------------------------------*/
        if(!startup_service_mode)
        {
            ResourceManager::get()->WaitForInitialization();
        }

        if(ret_flags & RET_FLAG_START_SERVER)
        {
            NetworkServer* server = ResourceManager::get()->GetServer();
            if(server)
            {
                if(startup_service_mode && startup_service_started_callback)
                {
                    startup_service_started_callback();
                }

                /*-----------------------------------------*\
                | Start the event loop to process Qt events |
                | Exit when server is stopped               |
                \*-----------------------------------------*/
                bool server_was_online = server->GetOnline();
                QTimer* server_check_timer = new QTimer(cli_app);
                QObject::connect(server_check_timer, &QTimer::timeout, [cli_app, server, server_was_online]() mutable {
                    if(startup_service_mode)
                    {
                        return;
                    }

                    if(server->GetOnline())
                    {
                        server_was_online = true;
                    }
                    else if(server_was_online)
                    {
                        cli_app->quit();
                    }
                });
                server_check_timer->start(1000);

                exitval = cli_app->exec();
                if(startup_service_mode && !startup_shutdown_requested())
                {
                    ResourceManager::get()->WaitForInitialization();
                }
                delete server_check_timer;
            }
            else
            {
                exitval = EXIT_FAILURE;
            }
        }
        else if(ret_flags & RET_FLAG_START_WEBSOCKET_SERVER)
        {
            WebSocketServer* ws_server = ResourceManager::get()->GetWebSocketServer();
            if(ws_server)
            {
                if(!startup_service_mode && !ws_server->GetOnline())
                {
                    ws_server->StartServer();
                }

                if(startup_service_mode && startup_service_started_callback)
                {
                    startup_service_started_callback();
                }

                /*-----------------------------------------*\
                | Start the event loop to process Qt events |
                | Exit when WebSocket server is stopped    |
                \*-----------------------------------------*/
                bool server_was_online = ws_server->GetOnline();
                QTimer* server_check_timer = new QTimer(cli_app);
                QObject::connect(server_check_timer, &QTimer::timeout, [cli_app, ws_server, server_was_online]() mutable {
                    if(startup_service_mode)
                    {
                        return;
                    }

                    if(ws_server->GetOnline())
                    {
                        server_was_online = true;
                    }
                    else if(server_was_online)
                    {
                        cli_app->quit();
                    }
                });
                server_check_timer->start(startup_service_mode ? 100 : 1000);

                exitval = cli_app->exec();
                if(startup_service_mode && !startup_shutdown_requested())
                {
                    ResourceManager::get()->WaitForInitialization();
                }
                delete server_check_timer;
            }
            else
            {
                exitval = EXIT_FAILURE;
            }
        }
        else
        {
            /*-----------------------------------------*\
            | No server mode, process any pending events |
            \*-----------------------------------------*/
            cli_app->processEvents();
        }
    }

    return(exitval);
}
