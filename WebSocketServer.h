/*---------------------------------------------------------*\
| WebSocketServer.h                                         |
|                                                           |
|   WebSocket Server for JSON-RPC Communication            |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <vector>
#include <mutex>
#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include "filesystem.h"
#include "RGBController.h"
#include "ResourceManager.h"
#include "ProfileManager.h"
#include "WebSocketClientInfo.h"
#include "JSONRPCHandler.h"

// Use standalone (non-boost) asio.  Must be defined before including any
// websocketpp/asio header that pulls in the asio backend.
#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

typedef void (*WebSocketServerCallback)(void *);

typedef websocketpp::server<websocketpp::config::asio> websocket_server;

class WebSocketServer
{
public:
    WebSocketServer(std::vector<RGBController *>& controllers, ResourceManager *resource_manager);
    ~WebSocketServer();

    /*---------------------------------------------------------*\
    | Server Control                                            |
    \*---------------------------------------------------------*/
    void                            StartServer();
    void                            StopServer();

    /*---------------------------------------------------------*\
    | Configuration                                             |
    \*---------------------------------------------------------*/
    void                            SetHost(const std::string& host);
    void                            SetPort(unsigned short port);
    void                            SetEnabled(bool enabled);
    void                            SetAuthToken(const std::string& token);
    void                            SetAuthTokens(const std::vector<std::string>& tokens);
    void                            SetRequireAuth(bool require);
    void                            SetEndpointFilePath(const filesystem::path& path);

    /*---------------------------------------------------------*\
    | Server State                                              |
    \*---------------------------------------------------------*/
    bool                            GetEnabled() const;
    bool                            GetOnline() const;
    bool                            GetListening() const;
    std::string                     GetLastError() const;
    std::string                     GetHost() const;
    unsigned short                  GetPort() const;
    unsigned int                    GetNumClients() const;

    /*---------------------------------------------------------*\
    | Client Information                                        |
    \*---------------------------------------------------------*/
    const char*                     GetClientIP(unsigned int client_idx);
    const char*                     GetClientString(unsigned int client_idx);

    /*---------------------------------------------------------*\
    | Callbacks for events                                      |
    \*---------------------------------------------------------*/
    void                            RegisterClientInfoChangeCallback(WebSocketServerCallback callback, void* arg);
    void                            DeviceListChanged();
    void                            DeviceListChanged(unsigned int controller_count);
    void                            ProfileListChanged();
    void                            ScanComplete(unsigned int device_count);

    /*---------------------------------------------------------*\
    | Per-event notification emitters                           |
    |                                                           |
    | Each builds the event payload and forwards it to          |
    | BroadcastNotification().  Callers are responsible for any |
    | required locking; none of these methods acquire the       |
    | device-list mutex themselves.                             |
    \*---------------------------------------------------------*/
    void                            DeviceConnected(unsigned int deviceIndex, const std::string& deviceName);
    void                            DeviceDisconnected(unsigned int deviceIndex);
    void                            ProfileSaved(const std::string& profileName);
    void                            ProfileLoaded(const std::string& profileName);

    /*---------------------------------------------------------*\
    | Settings integration                                      |
    \*---------------------------------------------------------*/
    void                            SetProfileManager(ProfileManagerInterface* profile_manager);

private:
    /*---------------------------------------------------------*\
    | websocketpp handlers (run on the io thread)               |
    \*---------------------------------------------------------*/
    void                            OnOpen(websocketpp::connection_hdl hdl);
    void                            OnClose(websocketpp::connection_hdl hdl);
    void                            OnFail(websocketpp::connection_hdl hdl);
    void                            OnMessage(websocketpp::connection_hdl hdl, websocket_server::message_ptr msg);

    void                            RunThread();
    void                            CloseAllConnections();
    bool                            JoinWithTimeout(std::thread& th, unsigned int timeout_ms);

    /*---------------------------------------------------------*\
    | Helpers                                                   |
    \*---------------------------------------------------------*/
    void                            SendToClient(websocketpp::connection_hdl hdl, const nlohmann::json& response);
    void                            BroadcastNotification(const std::string& event, const nlohmann::json& data);
    bool                            IsLoopbackAddress(const std::string& ip) const;
    bool                            AuthenticateClient(const std::string& token);
    std::string                     ExtractTokenFromUri(const std::string& uri) const;
    std::string                     GetRemoteIP(websocketpp::connection_hdl hdl);
    void                            NotifyClientInfoCallbacks();
    void                            ScheduleShutdown();
    void                            WriteEndpointFile();
    void                            ClearEndpointFile();

    std::string                         host;
    unsigned short                      port;
    bool                                enabled;
    bool                                require_auth;
    filesystem::path                    endpoint_file_path;

    websocket_server                   ws_server;
    std::thread                         io_thread;
    std::unique_ptr<asio::io_service::work> io_work;
    std::atomic<bool>                   server_online;
    std::atomic<bool>                   server_listening;
    std::mutex                          server_state_mutex;

    std::vector<RGBController *>&       controllers;
    ResourceManager *                   resource_manager;
    ProfileManagerInterface *           profile_manager;
    JSONRPCHandler *                    rpc_handler;

    mutable std::mutex                      clients_mutex;
    // Map connection handles to client info.  owner_less is required because
    // connection_hdl is a weak_ptr.
    std::map<websocketpp::connection_hdl, WebSocketClientInfo *,
             std::owner_less<websocketpp::connection_hdl>> clients;

    std::vector<std::string>            auth_tokens;
    std::vector<WebSocketServerCallback> client_info_callbacks;
    std::vector<void *>                 client_info_callback_args;

    std::string                         last_error;

    // Scratch buffers for the C-string getters
    mutable std::string                 client_ip_scratch;
    mutable std::string                 client_string_scratch;
};
