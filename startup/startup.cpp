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

#include <QApplication>
#include <QCoreApplication>
#include <QTimer>

#include "OpenRGBDialog.h"

#ifdef __APPLE__
#include "macutils.h"
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
        QGuiApplication::setDesktopFileName("org.openrgb.OpenRGB");
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
    }
    else
    {
        /*-------------------------------------------------*\
        | CLI mode: Create QCoreApplication to provide     |
        | event loop for WebSocketServer and other Qt      |
        | network services                                 |
        \*-------------------------------------------------*/
        QCoreApplication cli_app(argc, argv);
        LOG_TRACE("[startup] QCoreApplication created for CLI mode");

        /*-------------------------------------------------*\
        | Wait for initialization to finish                 |
        \*-------------------------------------------------*/
        ResourceManager::get()->WaitForInitialization();

        if(ret_flags & RET_FLAG_START_SERVER)
        {
            NetworkServer* server = ResourceManager::get()->GetServer();
            if(server)
            {
                /*-----------------------------------------*\
                | Start the event loop to process Qt events |
                | Exit when server is stopped               |
                \*-----------------------------------------*/
                QTimer::singleShot(0, [&cli_app, server]() {
                    if(!server->GetOnline())
                    {
                        cli_app.quit();
                    }
                });

                QTimer* server_check_timer = new QTimer(&cli_app);
                QObject::connect(server_check_timer, &QTimer::timeout, [&cli_app, server]() {
                    if(!server->GetOnline())
                    {
                        cli_app.quit();
                    }
                });
                server_check_timer->start(1000);

                exitval = cli_app.exec();
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
                /*-----------------------------------------*\
                | Start the event loop to process Qt events |
                | Exit when WebSocket server is stopped    |
                \*-----------------------------------------*/
                QTimer::singleShot(0, [&cli_app, ws_server]() {
                    if(!ws_server->GetOnline())
                    {
                        cli_app.quit();
                    }
                });

                QTimer* server_check_timer = new QTimer(&cli_app);
                QObject::connect(server_check_timer, &QTimer::timeout, [&cli_app, ws_server]() {
                    if(!ws_server->GetOnline())
                    {
                        cli_app.quit();
                    }
                });
                server_check_timer->start(1000);

                exitval = cli_app.exec();
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
            cli_app.processEvents();
        }
    }

    return(exitval);
}
