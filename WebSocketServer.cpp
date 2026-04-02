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
#include <QHostAddress>
#include <QUrlQuery>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QThread>

WebSocketServer::WebSocketServer(std::vector<RGBController*>& controllers,
                                 ResourceManager* resource_manager,
                                 QObject* parent)
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

    if(rpc_handler)
    {
        delete rpc_handler;
    }
}

/*---------------------------------------------------------*\
| Server Control                                            |
\*---------------------------------------------------------*/
void WebSocketServer::StartServer()
{
    // If called from a different thread, invoke in the correct thread
    if(QThread::currentThread() != this->thread())
    {
        QMetaObject::invokeMethod(this, "StartServer", Qt::QueuedConnection);
        return;
    }

    if(server_online)
    {
        return;
    }

    if(!enabled)
    {
        return;
    }

    // Create WebSocket server (no parent to avoid threading issues)
    ws_server = new QWebSocketServer(QStringLiteral("OpenRGB WebSocket Server"),
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
    if(ws_server->listen(QHostAddress(host_str), port))
    {
        server_online = true;
        server_listening = true;
        printf("[WebSocketServer] Server started on %s:%d\n", host.c_str(), port);
        emit ServerStateChanged();
    }
    else
    {
        printf("[WebSocketServer] Failed to start server: %s\n",
               ws_server->errorString().toStdString().c_str());
        delete ws_server;
        ws_server = nullptr;
        server_online = false;
        server_listening = false;
        emit ServerStateChanged();
    }
}

void WebSocketServer::StopServer()
{
    // If called from a different thread, invoke in the correct thread
    if(QThread::currentThread() != this->thread())
    {
        QMetaObject::invokeMethod(this, "StopServer", Qt::QueuedConnection);
        return;
    }

    if(!server_online)
    {
        return;
    }

    printf("[WebSocketServer] Stopping server...\n");

    // Close all client connections
    {
        std::lock_guard<std::mutex> lock(clients_mutex);

        for(auto client_info : clients)
        {
            if(client_info && client_info->GetSocket())
            {
                client_info->GetSocket()->close();
            }
            delete client_info;
        }
        clients.clear();
    }

    // Stop server
    if(ws_server)
    {
        ws_server->close();
        delete ws_server;
        ws_server = nullptr;
    }

    server_online = false;
    server_listening = false;
    emit ServerStateChanged();

    printf("[WebSocketServer] Server stopped\n");
}

/*---------------------------------------------------------*\
| Configuration                                             |
\*---------------------------------------------------------*/
void WebSocketServer::SetHost(const std::string& host)
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

void WebSocketServer::SetAuthToken(const std::string& token)
{
    auth_tokens.clear();
    if(!token.empty())
    {
        auth_tokens.push_back(token);
    }
}

void WebSocketServer::SetAuthTokens(const std::vector<std::string>& tokens)
{
    auth_tokens = tokens;
}

void WebSocketServer::SetRequireAuth(bool require)
{
    this->require_auth = require;
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
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(clients_mutex));
    return clients.size();
}

const char* WebSocketServer::GetClientIP(unsigned int client_idx)
{
    std::lock_guard<std::mutex> lock(clients_mutex);

    if(client_idx < clients.size())
    {
        static std::string ip_str;
        ip_str = clients[client_idx]->GetClientIP().toStdString();
        return ip_str.c_str();
    }

    return "";
}

const char* WebSocketServer::GetClientString(unsigned int client_idx)
{
    std::lock_guard<std::mutex> lock(clients_mutex);

    if(client_idx < clients.size())
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
void WebSocketServer::RegisterClientInfoChangeCallback(WebSocketServerCallback callback, void* arg)
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
        QString::fromStdString(data.dump())
    );
}

void WebSocketServer::ProfileListChanged()
{
    nlohmann::json data;
    data["message"] = "Profile list changed";

    // Emit signal instead of calling BroadcastNotification directly
    emit SignalBroadcastNotification(
        QString::fromStdString(JSONRPCProtocol::Events::PROFILE_SAVED),
        QString::fromStdString(data.dump())
    );
}

void WebSocketServer::ScanComplete(unsigned int device_count)
{
    nlohmann::json data;
    data["controllerCount"] = device_count;
    data["message"] = "Device scan completed";

    // Add full controller data
    nlohmann::json controllers_array = nlohmann::json::array();
    for(unsigned int i = 0; i < controllers.size(); i++)
    {
        controllers_array.push_back(rpc_handler->ControllerToJSON(controllers[i]));
    }
    data["controllers"] = controllers_array;

    qDebug() << "[WebSocketServer] ScanComplete: emitting signal for" << device_count << "devices with" << controllers_array.size() << "controller details";

    // Emit signal instead of calling BroadcastNotification directly
    emit SignalBroadcastNotification(
        QString::fromStdString(JSONRPCProtocol::Events::SCAN_COMPLETE),
        QString::fromStdString(data.dump())
    );
}

void WebSocketServer::SetProfileManager(ProfileManagerInterface* profile_manager)
{
    this->profile_manager = profile_manager;
    if(rpc_handler)
    {
        rpc_handler->SetProfileManager(profile_manager);
    }
}

/*---------------------------------------------------------*\
| Connection Handlers                                       |
\*---------------------------------------------------------*/
void WebSocketServer::OnNewConnection()
{
    QWebSocket* socket = ws_server->nextPendingConnection();

    if(!socket)
    {
        return;
    }

    printf("[WebSocketServer] New connection from %s\n",
           socket->peerAddress().toString().toStdString().c_str());

    // Extract token from URL query
    QString token = ExtractTokenFromRequest(socket);

    // Authenticate if required
    if(require_auth && !AuthenticateClient(socket, token))
    {
        printf("[WebSocketServer] Authentication failed for %s\n",
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
    WebSocketClientInfo* client_info = new WebSocketClientInfo(socket);

    if(!token.isEmpty())
    {
        client_info->SetAuthToken(token.toStdString());
    }

    if(require_auth)
    {
        client_info->SetAuthenticated(true);
    }

    // Add to clients list
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.push_back(client_info);
    }

    // Connect signals
    connect(socket, &QWebSocket::textMessageReceived,
            this, &WebSocketServer::OnTextMessageReceived);
    connect(socket, &QWebSocket::binaryMessageReceived,
            this, &WebSocketServer::OnBinaryMessageReceived);
    connect(socket, &QWebSocket::disconnected,
            this, &WebSocketServer::OnClientDisconnected);

    printf("[WebSocketServer] Client authenticated and connected\n");

    // Notify callbacks
    for(unsigned int i = 0; i < client_info_callbacks.size(); i++)
    {
        if(client_info_callbacks[i])
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
        QString::fromStdString(data.dump())
    );
}

void WebSocketServer::OnClientDisconnected()
{
    QWebSocket* socket = qobject_cast<QWebSocket*>(sender());

    if(!socket)
    {
        return;
    }

    printf("[WebSocketServer] Client disconnected: %s\n",
           socket->peerAddress().toString().toStdString().c_str());

    // Find and remove client
    nlohmann::json disconnect_data;
    bool client_found = false;

    {
        std::lock_guard<std::mutex> lock(clients_mutex);

        for(auto it = clients.begin(); it != clients.end(); ++it)
        {
            if((*it)->GetSocket() == socket)
            {
                // Collect data before removing client
                disconnect_data["clientIP"] = (*it)->GetClientIP().toStdString();
                client_found = true;

                delete *it;
                clients.erase(it);
                break;
            }
        }
    }

    // Broadcast notification AFTER releasing the lock
    if(client_found)
    {
        emit SignalBroadcastNotification(
            QString::fromStdString(JSONRPCProtocol::Events::CLIENT_DISCONNECTED),
            QString::fromStdString(disconnect_data.dump())
        );
    }

    socket->deleteLater();

    // Notify callbacks
    for(unsigned int i = 0; i < client_info_callbacks.size(); i++)
    {
        if(client_info_callbacks[i])
        {
            client_info_callbacks[i](client_info_callback_args[i]);
        }
    }

    emit ClientDisconnected();
}

void WebSocketServer::OnTextMessageReceived(const QString& message)
{
    QWebSocket* socket = qobject_cast<QWebSocket*>(sender());

    if(!socket)
    {
        return;
    }

    // Update client activity
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        for(auto client_info : clients)
        {
            if(client_info->GetSocket() == socket)
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
    catch(const std::exception& e)
    {
        nlohmann::json error_response;
        error_response["jsonrpc"] = "2.0";
        error_response["error"]["code"] = JSONRPCProtocol::PARSE_ERROR;
        error_response["error"]["message"] = "Parse error";
        error_response["id"] = nullptr;

        SendToClient(socket, error_response);
        return;
    }

    // Handle request
    nlohmann::json response = rpc_handler->HandleRequest(request);

    // Send response
    SendToClient(socket, response);
}

void WebSocketServer::OnBinaryMessageReceived(const QByteArray& message)
{
    QWebSocket* socket = qobject_cast<QWebSocket*>(sender());

    if(!socket)
    {
        return;
    }

    // Binary messages not supported yet
    socket->close(QWebSocketProtocol::CloseCodeBadOperation,
                  "Binary messages not supported");
}

void WebSocketServer::OnSocketError()
{
    printf("[WebSocketServer] Socket error: %s\n",
           ws_server->errorString().toStdString().c_str());
}

void WebSocketServer::OnBroadcastNotification(const QString& event, const QString& data)
{
    nlohmann::json notification;
    notification["jsonrpc"] = "2.0";
    notification["method"] = "notification";
    notification["params"]["event"] = event.toStdString();
    notification["params"]["data"] = nlohmann::json::parse(data.toStdString());

    QString message = QString::fromStdString(notification.dump());

    std::lock_guard<std::mutex> lock(clients_mutex);

    for(auto client_info : clients)
    {
        if(client_info && client_info->GetSocket())
        {
            client_info->GetSocket()->sendTextMessage(message);
        }
    }
}

/*---------------------------------------------------------*\
| Helper Functions                                          |
\*---------------------------------------------------------*/
void WebSocketServer::BroadcastNotification(const std::string& event,
                                           const nlohmann::json& data)
{
    nlohmann::json notification;
    notification["jsonrpc"] = "2.0";
    notification["method"] = "notification";
    notification["params"]["event"] = event;
    notification["params"]["data"] = data;

    QString message = QString::fromStdString(notification.dump());

    std::lock_guard<std::mutex> lock(clients_mutex);

    for(auto client_info : clients)
    {
        if(client_info && client_info->GetSocket())
        {
            client_info->GetSocket()->sendTextMessage(message);
        }
    }
}

void WebSocketServer::SendToClient(QWebSocket* client,
                                   const nlohmann::json& response)
{
    if(!client)
    {
        return;
    }

    QString message = QString::fromStdString(response.dump());
    client->sendTextMessage(message);
}

bool WebSocketServer::AuthenticateClient(QWebSocket* socket, const QString& token)
{
    if(!require_auth)
    {
        return true;  // Authentication not required
    }

    if(token.isEmpty())
    {
        return false;  // No token provided
    }

    // Check if token is in the allowed tokens list
    std::string token_str = token.toStdString();

    for(const auto& allowed_token : auth_tokens)
    {
        if(allowed_token == token_str)
        {
            return true;
        }
    }

    return false;  // Invalid token
}

QString WebSocketServer::ExtractTokenFromRequest(const QWebSocket* socket)
{
    if(!socket)
    {
        return QString();
    }

    // Get the request URL (resource name)
    QString resource_name = socket->resourceName();

    // Parse query parameters
    QUrlQuery query(resource_name);

    return query.queryItemValue("token");
}
