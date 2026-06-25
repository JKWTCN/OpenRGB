/*---------------------------------------------------------*\
| WebSocketServer.cpp                                       |
|                                                           |
|   WebSocket Server Implementation                         |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

/*---------------------------------------------------------*\
| Modified by JKWTCN <jkwtcn@icloud.com>                   |
| Date: 2026-04-02                                          |
| Changes:                                                  |
|   - Enhanced scan completion event broadcasting          |
|   - Added async scan event support                       |
\*---------------------------------------------------------*/

#include "WebSocketServer.h"
#include "AppInfo.h"
#include "LogManager.h"
#include "SettingsManager.h"
#include "startup/startup.h"
#include <QHostAddress>
#include <QUrlQuery>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QThread>
#include <fstream>

static bool IsLoopbackAddress(const QHostAddress& address)
{
    if((address == QHostAddress::LocalHost) || (address == QHostAddress::LocalHostIPv6))
    {
        return true;
    }

    bool ok = false;
    quint32 ipv4_address = address.toIPv4Address(&ok);

    return ok && ((ipv4_address & 0xFF000000) == 0x7F000000);
}

WebSocketServer::WebSocketServer(std::vector<RGBController *> &controllers,
                                 ResourceManager *resource_manager,
                                 QObject *parent)
    : QObject(parent),
      host("0.0.0.0"),
      port(6743),
      enabled(false),
      require_auth(false),
      ws_server(nullptr),
      controllers(controllers),
      resource_manager(resource_manager),
      profile_manager(nullptr),
      rpc_handler(nullptr),
      server_online(false),
      server_listening(false)
{
    rpc_handler = new JSONRPCHandler(controllers, resource_manager, profile_manager);
}

WebSocketServer::~WebSocketServer()
{
    StopServer();

    if (rpc_handler)
    {
        delete rpc_handler;
    }
}

/*---------------------------------------------------------*\
| Server Control                                            |
\*---------------------------------------------------------*/
void WebSocketServer::StartServer()
{
    // If called from a different thread, invoke in the correct thread.  All
    // QWebSocketServer / QWebSocket I/O must happen on this object's owning
    // thread, which runs the Qt event loop (cli_app->exec()).
    if (QThread::currentThread() != this->thread())
    {
        QMetaObject::invokeMethod(this, "StartServer", Qt::QueuedConnection);
        return;
    }

    if (server_online)
    {
        last_error.clear();
        LOG_VERBOSE("[WebSocketServer] StartServer called but server is already online");
        return;
    }

    if (!enabled)
    {
        last_error = "server is not enabled";
        LOG_WARNING("[WebSocketServer] StartServer called but server is not enabled");
        return;
    }

    last_error.clear();
    LOG_INFO("[WebSocketServer] Starting server on %s:%d", host.c_str(), port);

    // Create WebSocket server (no parent to avoid threading issues)
    ws_server = new QWebSocketServer(QStringLiteral(APP_NAME " WebSocket Server"),
                                     QWebSocketServer::NonSecureMode,
                                     nullptr);

    // Connect signals
    connect(ws_server, &QWebSocketServer::newConnection,
            this, &WebSocketServer::OnNewConnection);
    connect(ws_server, &QWebSocketServer::closed,
            this, &WebSocketServer::ServerStateChanged);
    connect(ws_server, &QWebSocketServer::serverError,
            this, &WebSocketServer::OnSocketError);

    // Connect internal signal-slot for thread-safe broadcasting
    connect(this, &WebSocketServer::SignalBroadcastNotification,
            this, &WebSocketServer::OnBroadcastNotification,
            Qt::QueuedConnection);

    // Start listening
    QString host_str = QString::fromStdString(host);
    QHostAddress listen_address(host_str);
    if (ws_server->listen(listen_address, port))
    {
        server_online = true;
        server_listening = true;
        LOG_INFO("[WebSocketServer] Server started successfully on %s:%d", host.c_str(), port);
        WriteEndpointFile();
        emit ServerStateChanged();
    }
    else
    {
        QString error_string = ws_server->errorString();
        if(error_string.isEmpty())
        {
            error_string = QString("listen returned false without errorString (host='%1', address='%2', address_is_null=%3, port=%4)")
                .arg(host_str)
                .arg(listen_address.toString())
                .arg(listen_address.isNull() ? "true" : "false")
                .arg(port);
        }
        last_error = error_string.toStdString();
        LOG_ERROR("[WebSocketServer] Failed to start server on %s:%d - %s",
                  host.c_str(), port,
                  last_error.c_str());
        delete ws_server;
        ws_server = nullptr;
        server_online = false;
        server_listening = false;
        emit ServerStateChanged();
    }
}

void WebSocketServer::StopServer()
{
    // If called from a different thread, invoke in the correct thread.
    if (QThread::currentThread() != this->thread())
    {
        QMetaObject::invokeMethod(this, "StopServer", Qt::QueuedConnection);
        return;
    }

    if (!server_online)
    {
        return;
    }

    unsigned int num_clients = 0;
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        num_clients = clients.size();
    }

    LOG_INFO("[WebSocketServer] Stopping server (%u client(s) connected)", num_clients);

    // Close all client connections
    {
        std::lock_guard<std::mutex> lock(clients_mutex);

        for (auto client_info : clients)
        {
            if (client_info && client_info->GetSocket())
            {
                client_info->GetSocket()->close();
            }
            delete client_info;
        }
        clients.clear();
    }

    // Stop server
    if (ws_server)
    {
        ws_server->close();
        delete ws_server;
        ws_server = nullptr;
    }

    server_online = false;
    server_listening = false;
    ClearEndpointFile();
    emit ServerStateChanged();

    LOG_INFO("[WebSocketServer] Server stopped");
}

/*---------------------------------------------------------*\
| Configuration                                             |
\*---------------------------------------------------------*/
void WebSocketServer::SetHost(const std::string &host)
{
    this->host = host;
}

void WebSocketServer::SetPort(unsigned short port)
{
    this->port = port;
}

void WebSocketServer::SetEnabled(bool enabled)
{
    this->enabled = enabled;
}

void WebSocketServer::SetAuthToken(const std::string &token)
{
    auth_tokens.clear();
    if (!token.empty())
    {
        auth_tokens.push_back(token);
    }
}

void WebSocketServer::SetAuthTokens(const std::vector<std::string> &tokens)
{
    auth_tokens = tokens;
}

void WebSocketServer::SetRequireAuth(bool require)
{
    this->require_auth = require;
}

void WebSocketServer::SetEndpointFilePath(const std::string &path)
{
    endpoint_file_path = filesystem::path(path);
}

void WebSocketServer::EnsureOnApplicationThread()
{
    // Must only be called before StartServer()/StopServer() create any sockets.
    QCoreApplication* app = QCoreApplication::instance();
    QThread* app_thread = app ? app->thread() : nullptr;
    QThread* cur_thread = QThread::currentThread();
    QThread* my_thread = this->thread();

    auto writeDiag = [&](const std::string& line)
    {
        try
        {
            filesystem::path dp;
            if(!endpoint_file_path.empty())
            {
                dp = endpoint_file_path.parent_path();
            }
            dp /= "ensure.diag";
            std::ofstream diag(dp, std::ios::app);
            if(diag)
            {
                diag << line << std::endl;
            }
        }
        catch(...) {}
    };

    writeDiag("app=" + std::string(app ? "exists" : "null")
              + " app_thread=" + std::to_string(reinterpret_cast<uintptr_t>(app_thread))
              + " cur=" + std::to_string(reinterpret_cast<uintptr_t>(cur_thread))
              + " my_thread=" + std::to_string(reinterpret_cast<uintptr_t>(my_thread))
              + " parent=" + (this->parent() ? "yes" : "no"));

    if(app_thread == nullptr)
    {
        LOG_WARNING("[WebSocketServer] EnsureOnApplicationThread: no QCoreApplication yet");
        return;
    }

    if(my_thread == app_thread)
    {
        writeDiag("skip: already on app thread");
        return;
    }

    // QObject::moveToThread() refuses an object that still has a parent, so
    // detach first.  The ResourceManager singleton outlives the event loop in
    // service mode and never destructs during normal shutdown, so lifetime is
    // managed by the process; detaching here is safe.
    this->setParent(nullptr);
    this->moveToThread(app_thread);
    bool moved = (this->thread() == app_thread);

    writeDiag("moveToThread moved=" + std::string(moved ? "1" : "0")
              + " now_thread=" + std::to_string(reinterpret_cast<uintptr_t>(this->thread())));

    LOG_INFO("[WebSocketServer] Moved onto application thread %p (was %p, moved=%d)",
             static_cast<void*>(app_thread), static_cast<void*>(my_thread), moved);
}

/*---------------------------------------------------------*\
| Server State                                              |
\*---------------------------------------------------------*/
bool WebSocketServer::GetEnabled() const
{
    return enabled;
}

bool WebSocketServer::GetOnline() const
{
    return server_online;
}

bool WebSocketServer::GetListening() const
{
    return server_listening;
}

std::string WebSocketServer::GetLastError() const
{
    return last_error;
}

std::string WebSocketServer::GetHost() const
{
    return host;
}

unsigned short WebSocketServer::GetPort() const
{
    return port;
}

unsigned int WebSocketServer::GetNumClients() const
{
    std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(clients_mutex));
    return clients.size();
}

const char *WebSocketServer::GetClientIP(unsigned int client_idx)
{
    std::lock_guard<std::mutex> lock(clients_mutex);

    if (client_idx < clients.size())
    {
        static std::string ip_str;
        ip_str = clients[client_idx]->GetClientIP().toStdString();
        return ip_str.c_str();
    }

    return "";
}

const char *WebSocketServer::GetClientString(unsigned int client_idx)
{
    std::lock_guard<std::mutex> lock(clients_mutex);

    if (client_idx < clients.size())
    {
        static std::string client_str;
        client_str = clients[client_idx]->GetClientString().toStdString();
        return client_str.c_str();
    }

    return "";
}

/*---------------------------------------------------------*\
| Callbacks                                                 |
\*---------------------------------------------------------*/
void WebSocketServer::RegisterClientInfoChangeCallback(WebSocketServerCallback callback, void *arg)
{
    client_info_callbacks.push_back(callback);
    client_info_callback_args.push_back(arg);
}

void WebSocketServer::DeviceListChanged()
{
    nlohmann::json data;
    data["controllerCount"] = controllers.size();

    // Emit signal instead of calling BroadcastNotification directly
    // This ensures the broadcast happens in the main thread
    emit SignalBroadcastNotification(
        QString::fromStdString(JSONRPCProtocol::Events::DEVICE_LIST_CHANGED),
        QString::fromStdString(data.dump()));
}

void WebSocketServer::ProfileListChanged()
{
    nlohmann::json data;
    data["message"] = "Profile list changed";

    // Emit signal instead of calling BroadcastNotification directly
    emit SignalBroadcastNotification(
        QString::fromStdString(JSONRPCProtocol::Events::PROFILE_SAVED),
        QString::fromStdString(data.dump()));
}

void WebSocketServer::ScanComplete(unsigned int device_count)
{
    nlohmann::json data;
    data["controllerCount"] = device_count;
    data["message"] = "Device scan completed";

    // Add full controller data
    nlohmann::json controllers_array = nlohmann::json::array();
    for (unsigned int i = 0; i < controllers.size(); i++)
    {
        controllers_array.push_back(rpc_handler->ControllerToJSON(controllers[i]));
    }
    data["controllers"] = controllers_array;

    qDebug() << "[WebSocketServer] ScanComplete: emitting signal for" << device_count << "devices with" << controllers_array.size() << "controller details";

    LOG_VERBOSE("[WebSocketServer] Scan complete: %u devices, broadcasting notification", device_count);

    // Emit signal instead of calling BroadcastNotification directly
    emit SignalBroadcastNotification(
        QString::fromStdString(JSONRPCProtocol::Events::SCAN_COMPLETE),
        QString::fromStdString(data.dump()));
}

void WebSocketServer::SetProfileManager(ProfileManagerInterface *profile_manager)
{
    this->profile_manager = profile_manager;
    if (rpc_handler)
    {
        rpc_handler->SetProfileManager(profile_manager);
    }
}

/*---------------------------------------------------------*\
| Connection Handlers                                       |
\*---------------------------------------------------------*/
void WebSocketServer::OnNewConnection()
{
    QWebSocket *socket = ws_server->nextPendingConnection();

    if (!socket)
    {
        return;
    }

    LOG_VERBOSE("[WebSocketServer] New connection from %s",
                socket->peerAddress().toString().toStdString().c_str());

    // Extract token from URL query
    QString token = ExtractTokenFromRequest(socket);

    // Authenticate if required
    if (require_auth && !AuthenticateClient(socket, token))
    {
        LOG_WARNING("[WebSocketServer] Authentication failed for %s",
                    socket->peerAddress().toString().toStdString().c_str());

        nlohmann::json error_response;
        error_response["jsonrpc"] = "2.0";
        error_response["error"]["code"] = JSONRPCProtocol::ERR_AUTHENTICATION_FAILED;
        error_response["error"]["message"] = "Authentication failed";
        error_response["id"] = nullptr;

        socket->sendTextMessage(QString::fromStdString(error_response.dump()));
        socket->close();
        socket->deleteLater();
        return;
    }

    // Create client info
    WebSocketClientInfo *client_info = new WebSocketClientInfo(socket);

    if (!token.isEmpty())
    {
        client_info->SetAuthToken(token.toStdString());
    }

    if (require_auth)
    {
        client_info->SetAuthenticated(true);
    }

    // Add to clients list
    unsigned int client_count;
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.push_back(client_info);
        client_count = clients.size();
    }

    // Connect signals
    connect(socket, &QWebSocket::textMessageReceived,
            this, &WebSocketServer::OnTextMessageReceived);
    connect(socket, &QWebSocket::binaryMessageReceived,
            this, &WebSocketServer::OnBinaryMessageReceived);
    connect(socket, &QWebSocket::disconnected,
            this, &WebSocketServer::OnClientDisconnected);

    LOG_INFO("[WebSocketServer] Client connected: %s (total: %u)",
             socket->peerAddress().toString().toStdString().c_str(), client_count);

    // Notify callbacks
    for (unsigned int i = 0; i < client_info_callbacks.size(); i++)
    {
        if (client_info_callbacks[i])
        {
            client_info_callbacks[i](client_info_callback_args[i]);
        }
    }

    emit ClientConnected();

    // Send client connected notification
    nlohmann::json data;
    data["clientIP"] = client_info->GetClientIP().toStdString();
    emit SignalBroadcastNotification(
        QString::fromStdString(JSONRPCProtocol::Events::CLIENT_CONNECTED),
        QString::fromStdString(data.dump()));
}

void WebSocketServer::OnClientDisconnected()
{
    QWebSocket *socket = qobject_cast<QWebSocket *>(sender());

    if (!socket)
    {
        return;
    }

    std::string client_ip = socket->peerAddress().toString().toStdString();

    // Find and remove client
    nlohmann::json disconnect_data;
    bool client_found = false;
    unsigned int remaining = 0;

    {
        std::lock_guard<std::mutex> lock(clients_mutex);

        for (auto it = clients.begin(); it != clients.end(); ++it)
        {
            if ((*it)->GetSocket() == socket)
            {
                // Collect data before removing client
                disconnect_data["clientIP"] = (*it)->GetClientIP().toStdString();
                client_found = true;

                delete *it;
                clients.erase(it);
                break;
            }
        }
        remaining = clients.size();
    }

    LOG_INFO("[WebSocketServer] Client disconnected: %s (remaining: %u)", client_ip.c_str(), remaining);

    // Broadcast notification AFTER releasing the lock
    if (client_found)
    {
        emit SignalBroadcastNotification(
            QString::fromStdString(JSONRPCProtocol::Events::CLIENT_DISCONNECTED),
            QString::fromStdString(disconnect_data.dump()));
    }

    socket->deleteLater();

    // Notify callbacks
    for (unsigned int i = 0; i < client_info_callbacks.size(); i++)
    {
        if (client_info_callbacks[i])
        {
            client_info_callbacks[i](client_info_callback_args[i]);
        }
    }

    emit ClientDisconnected();
}

void WebSocketServer::OnTextMessageReceived(const QString &message)
{
    QWebSocket *socket = qobject_cast<QWebSocket *>(sender());

    if (!socket)
    {
        return;
    }

    // Update client activity
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (auto client_info : clients)
        {
            if (client_info->GetSocket() == socket)
            {
                client_info->UpdateActivityTime();
                break;
            }
        }
    }

    // Parse JSON request
    nlohmann::json request;
    try
    {
        request = nlohmann::json::parse(message.toStdString());
    }
    catch (const std::exception &e)
    {
        LOG_WARNING("[WebSocketServer] Failed to parse message from %s: %s",
                    socket->peerAddress().toString().toStdString().c_str(), e.what());

        nlohmann::json error_response;
        error_response["jsonrpc"] = "2.0";
        error_response["error"]["code"] = JSONRPCProtocol::PARSE_ERROR;
        error_response["error"]["message"] = "Parse error";
        error_response["id"] = nullptr;

        SendToClient(socket, error_response);
        return;
    }

    if(!request.is_array()
    && request.contains("method")
    && request["method"].is_string()
    && (request["method"].get<std::string>() == JSONRPCProtocol::Methods::SHUTDOWN))
    {
        int id = request.value("id", 0);
        nlohmann::json response;
        response["jsonrpc"] = "2.0";
        response["id"] = id;

        if(!IsLoopbackAddress(socket->peerAddress()))
        {
            response["error"]["code"] = JSONRPCProtocol::ERR_OPERATION_NOT_PERMITTED;
            response["error"]["message"] = "Shutdown is only permitted from loopback clients";
            SendToClient(socket, response);
            return;
        }

        response["result"]["success"] = true;
        response["result"]["message"] = "Shutdown scheduled";
        SendToClient(socket, response);
        ScheduleShutdown();
        return;
    }

    if (request.is_array())
    {
        nlohmann::json responses = rpc_handler->HandleBatchRequest(request, IsLoopbackAddress(socket->peerAddress()));
        bool shutdown_requested = rpc_handler->TakeShutdownRequested();
        SendToClient(socket, responses);
        if(shutdown_requested)
        {
            ScheduleShutdown();
        }
    }
    else
    {
        nlohmann::json response = rpc_handler->HandleRequest(request, IsLoopbackAddress(socket->peerAddress()));
        bool shutdown_requested = rpc_handler->TakeShutdownRequested();
        SendToClient(socket, response);
        if(shutdown_requested)
        {
            ScheduleShutdown();
        }
    }
}

void WebSocketServer::OnBinaryMessageReceived(const QByteArray &message)
{
    QWebSocket *socket = qobject_cast<QWebSocket *>(sender());

    if (!socket)
    {
        return;
    }

    LOG_WARNING("[WebSocketServer] Binary message rejected from %s (not supported)",
                socket->peerAddress().toString().toStdString().c_str());

    socket->close(QWebSocketProtocol::CloseCodeBadOperation,
                  "Binary messages not supported");
}

void WebSocketServer::OnSocketError()
{
    LOG_ERROR("[WebSocketServer] Socket error: %s",
              ws_server->errorString().toStdString().c_str());
}

void WebSocketServer::OnBroadcastNotification(const QString &event, const QString &data)
{
    nlohmann::json notification;
    notification["jsonrpc"] = "2.0";
    notification["method"] = "notification";
    notification["params"]["event"] = event.toStdString();
    notification["params"]["data"] = nlohmann::json::parse(data.toStdString());

    QString message = QString::fromStdString(notification.dump());

    std::lock_guard<std::mutex> lock(clients_mutex);

    for (auto client_info : clients)
    {
        if (client_info && client_info->GetSocket())
        {
            client_info->GetSocket()->sendTextMessage(message);
        }
    }
}

/*---------------------------------------------------------*\
| Helper Functions                                          |
\*---------------------------------------------------------*/
void WebSocketServer::BroadcastNotification(const std::string &event,
                                            const nlohmann::json &data)
{
    nlohmann::json notification;
    notification["jsonrpc"] = "2.0";
    notification["method"] = "notification";
    notification["params"]["event"] = event;
    notification["params"]["data"] = data;

    QString message = QString::fromStdString(notification.dump());

    std::lock_guard<std::mutex> lock(clients_mutex);

    for (auto client_info : clients)
    {
        if (client_info && client_info->GetSocket())
        {
            client_info->GetSocket()->sendTextMessage(message);
        }
    }
}

void WebSocketServer::SendToClient(QWebSocket *client,
                                   const nlohmann::json &response)
{
    if (!client)
    {
        return;
    }

    QString message = QString::fromStdString(response.dump());
    client->sendTextMessage(message);
    client->flush();
}

bool WebSocketServer::AuthenticateClient(QWebSocket *socket, const QString &token)
{
    if (!require_auth)
    {
        return true; // Authentication not required
    }

    if (token.isEmpty())
    {
        return false; // No token provided
    }

    // Check if token is in the allowed tokens list
    std::string token_str = token.toStdString();

    for (const auto &allowed_token : auth_tokens)
    {
        if (allowed_token == token_str)
        {
            return true;
        }
    }

    return false; // Invalid token
}

QString WebSocketServer::ExtractTokenFromRequest(const QWebSocket *socket)
{
    if (!socket)
    {
        return QString();
    }

    // Get the request URL (resource name)
    QString resource_name = socket->resourceName();

    // Parse query parameters
    QUrlQuery query(resource_name);

    return query.queryItemValue("token");
}

void WebSocketServer::ScheduleShutdown()
{
    LOG_INFO("[WebSocketServer] Shutdown requested by local JSON-RPC client");
    startup_request_shutdown();

    QCoreApplication* app = QCoreApplication::instance();
    QTimer::singleShot(1000, app, [this, app]() {
        if(resource_manager)
        {
            resource_manager->StopDeviceDetection();
        }

        StopServer();

        if(app)
        {
            app->quit();
        }
    });
}

void WebSocketServer::WriteEndpointFile()
{
    if(endpoint_file_path.empty())
    {
        return;
    }

    try
    {
        if(resource_manager && resource_manager->GetSettingsManager())
        {
            SettingsManager* settings_manager = resource_manager->GetSettingsManager();
            nlohmann::json service_settings = settings_manager->GetSettings("Service");

            if(!service_settings.is_object())
            {
                service_settings = nlohmann::json::object();
            }

            service_settings["name"]           = "RGB Server";
            service_settings["host"]           = "127.0.0.1";
            service_settings["port"]           = port;
            service_settings["websocket_port"] = port;
            service_settings["pid"]            = QCoreApplication::applicationPid();
            service_settings["running"]        = true;

            settings_manager->SetSettings("Service", service_settings);
            settings_manager->SaveSettings();
            return;
        }

        nlohmann::json settings = nlohmann::json::object();

        if(filesystem::exists(endpoint_file_path))
        {
            std::ifstream input_file(endpoint_file_path.string(), std::ios::in | std::ios::binary);
            if(input_file)
            {
                input_file >> settings;
            }
        }

        if(!settings.is_object())
        {
            settings = nlohmann::json::object();
        }

        nlohmann::json service_settings = settings.value("Service", nlohmann::json::object());
        if(!service_settings.is_object())
        {
            service_settings = nlohmann::json::object();
        }

        service_settings["name"]           = "RGB Server";
        service_settings["host"]           = "127.0.0.1";
        service_settings["port"]           = port;
        service_settings["websocket_port"] = port;
        service_settings["pid"]            = QCoreApplication::applicationPid();
        service_settings["running"]        = true;

        settings["Service"] = service_settings;

        std::ofstream file(endpoint_file_path.string(), std::ios::out | std::ios::binary | std::ios::trunc);
        file << settings.dump(4);
        file << std::endl;
    }
    catch(const std::exception& e)
    {
        LOG_WARNING("[WebSocketServer] Failed to write endpoint file %s: %s",
                    endpoint_file_path.string().c_str(), e.what());
    }
}

void WebSocketServer::ClearEndpointFile()
{
    if(endpoint_file_path.empty())
    {
        return;
    }

    try
    {
        if(resource_manager && resource_manager->GetSettingsManager())
        {
            SettingsManager* settings_manager = resource_manager->GetSettingsManager();
            nlohmann::json service_settings = settings_manager->GetSettings("Service");

            if(!service_settings.is_object())
            {
                service_settings = nlohmann::json::object();
            }

            service_settings["running"] = false;
            service_settings["pid"]     = 0;

            settings_manager->SetSettings("Service", service_settings);
            settings_manager->SaveSettings();
            return;
        }

        if(!filesystem::exists(endpoint_file_path))
        {
            return;
        }

        nlohmann::json settings = nlohmann::json::object();
        std::ifstream input_file(endpoint_file_path.string(), std::ios::in | std::ios::binary);
        if(input_file)
        {
            input_file >> settings;
        }

        if(!settings.is_object())
        {
            settings = nlohmann::json::object();
        }

        nlohmann::json service_settings = settings.value("Service", nlohmann::json::object());
        if(!service_settings.is_object())
        {
            service_settings = nlohmann::json::object();
        }

        service_settings["running"] = false;
        service_settings["pid"]     = 0;

        settings["Service"] = service_settings;

        std::ofstream file(endpoint_file_path.string(), std::ios::out | std::ios::binary | std::ios::trunc);
        file << settings.dump(4);
        file << std::endl;
    }
    catch(const std::exception& e)
    {
        LOG_WARNING("[WebSocketServer] Failed to remove endpoint file %s: %s",
                    endpoint_file_path.string().c_str(), e.what());
    }
}
