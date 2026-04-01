/*---------------------------------------------------------*\
| WebSocketServer.h                                         |
|                                                           |
|   WebSocket Server for JSON-RPC Communication            |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <QObject>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QTimer>
#include <vector>
#include <mutex>
#include <string>
#include "RGBController.h"
#include "ResourceManager.h"
#include "ProfileManager.h"
#include "WebSocketClientInfo.h"
#include "JSONRPCHandler.h"

typedef void (*WebSocketServerCallback)(void *);

class WebSocketServer : public QObject
{
    Q_OBJECT

public:
    WebSocketServer(std::vector<RGBController*>& controllers,
                   ResourceManager* resource_manager,
                   QObject* parent = nullptr);

    ~WebSocketServer();

    // Server control
    void            StartServer();
    void            StopServer();

    // Configuration
    void            SetHost(const std::string& host);
    void            SetPort(unsigned short port);
    void            SetEnabled(bool enabled);
    void            SetAuthToken(const std::string& token);
    void            SetAuthTokens(const std::vector<std::string>& tokens);
    void            SetRequireAuth(bool require);

    // Server state
    bool            GetEnabled() const;
    bool            GetOnline() const;
    bool            GetListening() const;
    std::string     GetHost() const;
    unsigned short  GetPort() const;
    unsigned int    GetNumClients() const;

    // Client information
    const char*     GetClientIP(unsigned int client_idx);
    const char*     GetClientString(unsigned int client_idx);

    // Callbacks for events
    void            RegisterClientInfoChangeCallback(WebSocketServerCallback callback, void* arg);
    void            DeviceListChanged();
    void            ProfileListChanged();
    void            ScanComplete(unsigned int device_count);

    // Settings integration
    void            SetProfileManager(ProfileManagerInterface* profile_manager);

signals:
    void            ClientConnected();
    void            ClientDisconnected();
    void            ServerStateChanged();
    void            SignalBroadcastNotification(const QString& event, const QString& data);

private slots:
    void            OnNewConnection();
    void            OnClientDisconnected();
    void            OnTextMessageReceived(const QString& message);
    void            OnBinaryMessageReceived(const QByteArray& message);
    void            OnSocketError();
    void            OnBroadcastNotification(const QString& event, const QString& data);

private:
    void            BroadcastNotification(const std::string& event,
                                        const nlohmann::json& data);
    void            SendToClient(QWebSocket* client,
                               const nlohmann::json& response);
    bool            AuthenticateClient(QWebSocket* socket, const QString& token);
    QString         ExtractTokenFromRequest(const QWebSocket* socket);

    std::string                         host;
    unsigned short                      port;
    bool                                enabled;
    bool                                require_auth;

    QWebSocketServer*                   ws_server;
    std::vector<WebSocketClientInfo*>   clients;
    std::mutex                          clients_mutex;

    std::vector<RGBController*>&        controllers;
    ResourceManager*                    resource_manager;
    ProfileManagerInterface*            profile_manager;
    JSONRPCHandler*                     rpc_handler;

    std::vector<std::string>            auth_tokens;

    std::vector<WebSocketServerCallback> client_info_callbacks;
    std::vector<void*>                  client_info_callback_args;

    std::atomic<bool>                   server_online;
    std::atomic<bool>                   server_listening;
};
