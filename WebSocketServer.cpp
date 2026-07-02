/*---------------------------------------------------------*\
| WebSocketServer.cpp                                       |
|                                                           |
|   WebSocket Server Implementation (websocketpp + asio)    |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "WebSocketServer.h"
#include "AppInfo.h"
#include "LogManager.h"
#include "SettingsManager.h"
#include "startup/startup.h"
#include <nlohmann/json.hpp>
#include <QCoreApplication>

#include <fstream>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cstdlib>

WebSocketServer::WebSocketServer(std::vector<RGBController *> &controllers,
                                 ResourceManager *resource_manager)
    : host("0.0.0.0"),
      port(6743),
      enabled(false),
      require_auth(false),
      server_online(false),
      server_listening(false),
      controllers(controllers),
      resource_manager(resource_manager),
      profile_manager(nullptr),
      rpc_handler(nullptr)
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
    std::lock_guard<std::mutex> state_lock(server_state_mutex);

    if (server_online)
    {
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

    try
    {
        // Quiet the websocketpp access log (otherwise it spews per-frame logs)
        ws_server.set_access_channels(websocketpp::log::alevel::none);
        ws_server.clear_access_channels(websocketpp::log::alevel::all);
        ws_server.set_error_channels(websocketpp::log::elevel::info |
                                     websocketpp::log::elevel::warn |
                                     websocketpp::log::elevel::rerror |
                                     websocketpp::log::elevel::fatal);

        ws_server.init_asio();
        ws_server.set_reuse_addr(true);

        // Register handlers
        ws_server.set_open_handler(
            websocketpp::lib::bind(&WebSocketServer::OnOpen, this,
                                   websocketpp::lib::placeholders::_1));
        ws_server.set_close_handler(
            websocketpp::lib::bind(&WebSocketServer::OnClose, this,
                                   websocketpp::lib::placeholders::_1));
        ws_server.set_fail_handler(
            websocketpp::lib::bind(&WebSocketServer::OnFail, this,
                                   websocketpp::lib::placeholders::_1));
        ws_server.set_message_handler(
            websocketpp::lib::bind(&WebSocketServer::OnMessage, this,
                                   websocketpp::lib::placeholders::_1,
                                   websocketpp::lib::placeholders::_2));

        // Listen on host:port.  Resolve the configured host string into an
        // endpoint so we bind to the right interface (e.g. 127.0.0.1) rather
        // than always wildcard-binding.
        asio::ip::tcp::resolver resolver(ws_server.get_io_service());
        std::string port_str = std::to_string(port);
        asio::ip::tcp::resolver::results_type endpoints = resolver.resolve(host, port_str);

        websocketpp::lib::error_code listen_ec;
        ws_server.listen(*endpoints.begin(), listen_ec);
        if (listen_ec)
        {
            throw websocketpp::exception(listen_ec.message());
        }

        ws_server.start_accept();

        // Keep the io_service alive even when idle
        io_work = std::unique_ptr<asio::io_service::work>(
            new asio::io_service::work(ws_server.get_io_service()));

        server_online = true;
        server_listening = true;

        // Run the io loop on a dedicated thread
        io_thread = std::thread(&WebSocketServer::RunThread, this);

        LOG_INFO("[WebSocketServer] Server started successfully on %s:%d", host.c_str(), port);
        WriteEndpointFile();
    }
    catch (websocketpp::exception const &e)
    {
        last_error = e.what();
        LOG_ERROR("[WebSocketServer] Failed to start server on %s:%d - %s",
                  host.c_str(), port, last_error.c_str());
        server_online = false;
        server_listening = false;
    }
    catch (std::exception const &e)
    {
        last_error = e.what();
        LOG_ERROR("[WebSocketServer] Failed to start server on %s:%d - %s",
                  host.c_str(), port, last_error.c_str());
        server_online = false;
        server_listening = false;
    }
}

void WebSocketServer::StopServer()
{
    std::lock_guard<std::mutex> state_lock(server_state_mutex);

    if (!server_online)
    {
        return;
    }

    server_online = false;
    server_listening = false;

    LOG_INFO("[WebSocketServer] Stopping server");

    try
    {
        // Close all active connections gracefully
        CloseAllConnections();

        // Give connections a moment to close
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Stop accepting new connections
        ws_server.stop_listening();

        // Release the work guard so io_service::run() can return
        io_work.reset();

        // Stop the io_service
        ws_server.stop();

        // Wait for the io thread to finish (with timeout)
        if (io_thread.joinable())
        {
            if (!JoinWithTimeout(io_thread, 1000))
            {
                LOG_WARNING("[WebSocketServer] IO thread did not stop in time");
            }
        }

        // Clear client list
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            for (auto &kv : clients)
            {
                delete kv.second;
            }
            clients.clear();
        }

        ClearEndpointFile();
        LOG_INFO("[WebSocketServer] Server stopped");
        NotifyClientInfoCallbacks();
    }
    catch (websocketpp::exception const &e)
    {
        LOG_ERROR("[WebSocketServer] WebSocket exception during stop: %s", e.what());
    }
    catch (std::exception const &e)
    {
        LOG_ERROR("[WebSocketServer] Exception during stop: %s", e.what());
    }
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

void WebSocketServer::SetEndpointFilePath(const filesystem::path &path)
{
    endpoint_file_path = path;
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
    std::lock_guard<std::mutex> lock(clients_mutex);
    return clients.size();
}

/*---------------------------------------------------------*\
| Client Information                                        |
\*---------------------------------------------------------*/
const char *WebSocketServer::GetClientIP(unsigned int client_idx)
{
    std::lock_guard<std::mutex> lock(clients_mutex);

    if (client_idx >= clients.size())
    {
        return "";
    }

    auto it = clients.begin();
    std::advance(it, client_idx);
    client_ip_scratch = it->second->GetClientIP();
    return client_ip_scratch.c_str();
}

const char *WebSocketServer::GetClientString(unsigned int client_idx)
{
    std::lock_guard<std::mutex> lock(clients_mutex);

    if (client_idx >= clients.size())
    {
        return "";
    }

    auto it = clients.begin();
    std::advance(it, client_idx);
    client_string_scratch = it->second->GetClientString();
    return client_string_scratch.c_str();
}

/*---------------------------------------------------------*\
| Callbacks / Notifications                                 |
\*---------------------------------------------------------*/
void WebSocketServer::RegisterClientInfoChangeCallback(WebSocketServerCallback callback, void *arg)
{
    std::lock_guard<std::mutex> lock(clients_mutex);
    client_info_callbacks.push_back(callback);
    client_info_callback_args.push_back(arg);
}

void WebSocketServer::DeviceListChanged()
{
    unsigned int count = 0;
    if (resource_manager)
    {
        std::lock_guard<std::mutex> lock(resource_manager->GetDeviceListChangeMutex());
        count = controllers.size();
    }
    else
    {
        count = controllers.size();
    }

    DeviceListChanged(count);
}

void WebSocketServer::DeviceListChanged(unsigned int controller_count)
{
    nlohmann::json data;
    data["controllerCount"] = controller_count;
    BroadcastNotification(JSONRPCProtocol::Events::DEVICE_LIST_CHANGED, data);
}

void WebSocketServer::ProfileListChanged()
{
    nlohmann::json data;
    data["message"] = "Profile list changed";
    BroadcastNotification(JSONRPCProtocol::Events::PROFILE_SAVED, data);
}

void WebSocketServer::DeviceConnected(unsigned int deviceIndex, const std::string& deviceName)
{
    nlohmann::json data;
    data["deviceIndex"] = deviceIndex;
    data["deviceName"]   = deviceName;

    BroadcastNotification(JSONRPCProtocol::Events::DEVICE_CONNECTED, data);
}

void WebSocketServer::DeviceDisconnected(unsigned int deviceIndex)
{
    nlohmann::json data;
    data["deviceIndex"] = deviceIndex;

    BroadcastNotification(JSONRPCProtocol::Events::DEVICE_DISCONNECTED, data);
}

void WebSocketServer::ProfileSaved(const std::string& profileName)
{
    nlohmann::json data;
    data["profileName"] = profileName;

    BroadcastNotification(JSONRPCProtocol::Events::PROFILE_SAVED, data);
}

void WebSocketServer::ProfileLoaded(const std::string& profileName)
{
    nlohmann::json data;
    data["profileName"] = profileName;

    BroadcastNotification(JSONRPCProtocol::Events::PROFILE_LOADED, data);
}

void WebSocketServer::ScanComplete(unsigned int device_count)
{
    nlohmann::json data;
    data["controllerCount"] = device_count;
    data["message"] = "Device scan completed";

    // Serialize the controllers under the device-list lock: this runs right
    // after a rescan finishes, concurrent with any RPC that may touch the
    // list, so we must not iterate the vector without it.
    nlohmann::json controllers_array = nlohmann::json::array();
    auto controller_to_scan_complete_json = [this](RGBController *controller) {
        if (controller->type == DEVICE_TYPE_KEYBOARD)
        {
            return rpc_handler->ControllerToScanCompleteJSON(controller);
        }

        return rpc_handler->ControllerToJSON(controller);
    };

    if (resource_manager)
    {
        std::lock_guard<std::mutex> lock(resource_manager->GetDeviceListChangeMutex());
        for (unsigned int i = 0; i < controllers.size(); i++)
        {
            controllers_array.push_back(controller_to_scan_complete_json(controllers[i]));
        }
    }
    else
    {
        for (unsigned int i = 0; i < controllers.size(); i++)
        {
            controllers_array.push_back(controller_to_scan_complete_json(controllers[i]));
        }
    }
    data["controllers"] = controllers_array;

    LOG_VERBOSE("[WebSocketServer] Scan complete: %u devices, broadcasting notification", device_count);

    BroadcastNotification(JSONRPCProtocol::Events::SCAN_COMPLETE, data);
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
| websocketpp handlers (run on the io thread)               |
\*---------------------------------------------------------*/
void WebSocketServer::OnOpen(websocketpp::connection_hdl hdl)
{
    std::string ip = GetRemoteIP(hdl);
    unsigned short remote_port = 0;

    try
    {
        websocket_server::connection_ptr con = ws_server.get_con_from_hdl(hdl);
        // get_remote_endpoint() returns "host:port"; extract the port.
        std::string endpoint = con->get_remote_endpoint();
        size_t colon = endpoint.rfind(':');
        if (colon != std::string::npos)
        {
            remote_port = static_cast<unsigned short>(std::atoi(endpoint.substr(colon + 1).c_str()));
        }
    }
    catch (...) {}

    // Token auth (optional)
    std::string token;
    try
    {
        websocket_server::connection_ptr con = ws_server.get_con_from_hdl(hdl);
        token = ExtractTokenFromUri(con->get_uri()->str());
    }
    catch (...) {}

    if (require_auth && !AuthenticateClient(token))
    {
        LOG_WARNING("[WebSocketServer] Authentication failed for %s", ip.c_str());
        try
        {
            nlohmann::json error_response;
            error_response["jsonrpc"] = "2.0";
            error_response["error"]["code"] = JSONRPCProtocol::ERR_AUTHENTICATION_FAILED;
            error_response["error"]["message"] = "Authentication failed";
            error_response["id"] = nullptr;
            ws_server.send(hdl, error_response.dump(), websocketpp::frame::opcode::text);
            ws_server.close(hdl, websocketpp::close::status::policy_violation, "Authentication failed");
        }
        catch (...) {}
        return;
    }

    WebSocketClientInfo *client_info = new WebSocketClientInfo(hdl, ip, remote_port);
    if (!token.empty())
    {
        client_info->SetAuthToken(token);
    }
    if (require_auth)
    {
        client_info->SetAuthenticated(true);
    }

    unsigned int client_count;
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients[hdl] = client_info;
        client_count = clients.size();
    }

    LOG_INFO("[WebSocketServer] Client connected: %s (total: %u)", ip.c_str(), client_count);

    // Broadcast the client-connected notification
    nlohmann::json data;
    data["clientIP"] = ip;
    BroadcastNotification(JSONRPCProtocol::Events::CLIENT_CONNECTED, data);

    NotifyClientInfoCallbacks();
}

void WebSocketServer::OnClose(websocketpp::connection_hdl hdl)
{
    std::string ip;
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        auto it = clients.find(hdl);
        if (it != clients.end())
        {
            ip = it->second->GetClientIP();
            delete it->second;
            clients.erase(it);
        }
    }

    unsigned int remaining = GetNumClients();
    LOG_INFO("[WebSocketServer] Client disconnected: %s (remaining: %u)", ip.c_str(), remaining);

    nlohmann::json data;
    data["clientIP"] = ip;
    BroadcastNotification(JSONRPCProtocol::Events::CLIENT_DISCONNECTED, data);

    NotifyClientInfoCallbacks();
}

void WebSocketServer::OnFail(websocketpp::connection_hdl hdl)
{
    LOG_WARNING("[WebSocketServer] Connection failed");

    std::lock_guard<std::mutex> lock(clients_mutex);
    auto it = clients.find(hdl);
    if (it != clients.end())
    {
        delete it->second;
        clients.erase(it);
    }
}

void WebSocketServer::OnMessage(websocketpp::connection_hdl hdl, websocket_server::message_ptr msg)
{
    std::string payload = msg->get_payload();
    std::string ip = GetRemoteIP(hdl);

    // Update client activity
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        auto it = clients.find(hdl);
        if (it != clients.end())
        {
            it->second->UpdateActivityTime();
        }
    }

    // Reject binary frames (only text/JSON supported)
    if (msg->get_opcode() != websocketpp::frame::opcode::text)
    {
        LOG_WARNING("[WebSocketServer] Binary message rejected from %s (not supported)", ip.c_str());
        try
        {
            ws_server.close(hdl, websocketpp::close::status::unsupported_data,
                            "Binary messages not supported");
        }
        catch (...) {}
        return;
    }

    bool loopback = IsLoopbackAddress(ip);

    // Parse JSON request
    nlohmann::json request;
    try
    {
        request = nlohmann::json::parse(payload);
    }
    catch (const std::exception &e)
    {
        LOG_WARNING("[WebSocketServer] Failed to parse message from %s: %s", ip.c_str(), e.what());

        nlohmann::json error_response;
        error_response["jsonrpc"] = "2.0";
        error_response["error"]["code"] = JSONRPCProtocol::PARSE_ERROR;
        error_response["error"]["message"] = "Parse error";
        error_response["id"] = nullptr;
        SendToClient(hdl, error_response);
        return;
    }

    // Special-case shutdown (same behaviour as the Qt version)
    if (!request.is_array()
        && request.contains("method")
        && request["method"].is_string()
        && (request["method"].get<std::string>() == JSONRPCProtocol::Methods::SHUTDOWN))
    {
        int id = request.value("id", 0);
        nlohmann::json response;
        response["jsonrpc"] = "2.0";
        response["id"] = id;

        if (!loopback)
        {
            response["error"]["code"] = JSONRPCProtocol::ERR_OPERATION_NOT_PERMITTED;
            response["error"]["message"] = "Shutdown is only permitted from loopback clients";
            SendToClient(hdl, response);
            return;
        }

        response["result"]["success"] = true;
        response["result"]["message"] = "Shutdown scheduled";
        SendToClient(hdl, response);
        ScheduleShutdown();
        return;
    }

    // Dispatch through the JSON-RPC handler
    if (request.is_array())
    {
        nlohmann::json responses = rpc_handler->HandleBatchRequest(request, loopback);
        bool shutdown_requested = rpc_handler->TakeShutdownRequested();
        SendToClient(hdl, responses);
        if (shutdown_requested)
        {
            ScheduleShutdown();
        }
    }
    else
    {
        nlohmann::json response = rpc_handler->HandleRequest(request, loopback);
        bool shutdown_requested = rpc_handler->TakeShutdownRequested();
        SendToClient(hdl, response);
        if (shutdown_requested)
        {
            ScheduleShutdown();
        }
    }
}

void WebSocketServer::RunThread()
{
    try
    {
        ws_server.run();
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("[WebSocketServer] IO thread exception: %s", e.what());
    }
}

void WebSocketServer::CloseAllConnections()
{
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (auto &kv : clients)
    {
        try
        {
            ws_server.close(kv.first, websocketpp::close::status::going_away,
                            "Server shutting down");
        }
        catch (const websocketpp::exception &) {}
    }
}

bool WebSocketServer::JoinWithTimeout(std::thread &th, unsigned int timeout_ms)
{
    (void)timeout_ms;

    if (!th.joinable())
    {
        return true;
    }

    if (th.get_id() == std::this_thread::get_id())
    {
        th.detach();
        return true;
    }

    th.join();
    return true;
}

/*---------------------------------------------------------*\
| Helpers                                                   |
\*---------------------------------------------------------*/
void WebSocketServer::SendToClient(websocketpp::connection_hdl hdl, const nlohmann::json &response)
{
    try
    {
        ws_server.send(hdl, response.dump(), websocketpp::frame::opcode::text);
    }
    catch (const websocketpp::exception &e)
    {
        LOG_WARNING("[WebSocketServer] Failed to send to client: %s", e.what());
    }
}

void WebSocketServer::BroadcastNotification(const std::string &event, const nlohmann::json &data)
{
    nlohmann::json notification;
    notification["jsonrpc"] = "2.0";
    notification["method"] = "notification";
    notification["params"]["event"] = event;
    notification["params"]["data"] = data;

    std::string message = notification.dump();

    // Send to every connected client.  This may be called from any thread, but
    // websocketpp::server::send is safe to call concurrently for distinct
    // connections.  We hold the lock to keep the client map stable.
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (auto it = clients.begin(); it != clients.end();)
    {
        try
        {
            ws_server.send(it->first, message, websocketpp::frame::opcode::text);
            ++it;
        }
        catch (const websocketpp::exception &)
        {
            // Drop dead connections from the map (don't delete the info here;
            // OnClose/OnFail will handle cleanup).
            it = clients.erase(it);
        }
    }
}

bool WebSocketServer::IsLoopbackAddress(const std::string &ip) const
{
    if (ip == "127.0.0.1" || ip == "::1" || ip == "localhost")
    {
        return true;
    }
    // 127.x.x.x range
    if (ip.size() >= 8 && ip.substr(0, 4) == "127.")
    {
        return true;
    }
    return false;
}

bool WebSocketServer::AuthenticateClient(const std::string &token)
{
    if (!require_auth)
    {
        return true;
    }
    if (token.empty())
    {
        return false;
    }
    for (const auto &allowed_token : auth_tokens)
    {
        if (allowed_token == token)
        {
            return true;
        }
    }
    return false;
}

std::string WebSocketServer::ExtractTokenFromUri(const std::string &uri) const
{
    // uri looks like "ws://host:port/?token=xxx" — extract the token query param
    std::string token_key = "token=";
    size_t pos = uri.find(token_key);
    if (pos == std::string::npos)
    {
        return "";
    }
    pos += token_key.size();
    size_t end = uri.find('&', pos);
    if (end == std::string::npos)
    {
        end = uri.size();
    }
    return uri.substr(pos, end - pos);
}

std::string WebSocketServer::GetRemoteIP(websocketpp::connection_hdl hdl)
{
    try
    {
        websocket_server::connection_ptr con = ws_server.get_con_from_hdl(hdl);
        // get_remote_endpoint() returns a "host:port" style string.
        std::string endpoint = con->get_remote_endpoint();
        // Strip the port to return just the address.
        size_t colon = endpoint.rfind(':');
        if (colon != std::string::npos)
        {
            return endpoint.substr(0, colon);
        }
        return endpoint;
    }
    catch (...)
    {
        return "";
    }
}

void WebSocketServer::NotifyClientInfoCallbacks()
{
    std::vector<WebSocketServerCallback> callbacks;
    std::vector<void *> args;
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        callbacks = client_info_callbacks;
        args = client_info_callback_args;
    }

    for (size_t i = 0; i < callbacks.size(); i++)
    {
        if (callbacks[i])
        {
            callbacks[i](args[i]);
        }
    }
}

void WebSocketServer::ScheduleShutdown()
{
    LOG_INFO("[WebSocketServer] Shutdown requested by local JSON-RPC client");
    startup_request_shutdown();

    // Perform the actual shutdown on a detached worker thread so the response
    // frame is flushed before we tear the connection down.  This replaces the
    // former QTimer::singleShot (which needed the Qt event loop).
    std::thread([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        if (resource_manager)
        {
            resource_manager->StopDeviceDetection();
        }

        this->StopServer();

        // Quit the Qt application event loop (service / CLI / GUI all run one).
        QCoreApplication *app = QCoreApplication::instance();
        if (app)
        {
            QMetaObject::invokeMethod(app, "quit", Qt::QueuedConnection);
        }
    }).detach();
}

void WebSocketServer::WriteEndpointFile()
{
    if (endpoint_file_path.empty())
    {
        return;
    }

    qint64 pid = 0;
    QCoreApplication *app = QCoreApplication::instance();
    if (app)
    {
        pid = QCoreApplication::applicationPid();
    }

    try
    {
        if (resource_manager && resource_manager->GetSettingsManager())
        {
            SettingsManager *settings_manager = resource_manager->GetSettingsManager();
            nlohmann::json service_settings = settings_manager->GetSettings("Service");

            if (!service_settings.is_object())
            {
                service_settings = nlohmann::json::object();
            }

            service_settings["name"] = "RGB Server";
            service_settings["host"] = "127.0.0.1";
            service_settings["port"] = port;
            service_settings["websocket_port"] = port;
            service_settings["pid"] = pid;
            service_settings["running"] = true;

            settings_manager->SetSettings("Service", service_settings);
            settings_manager->SaveSettings();
        }

        nlohmann::json settings = nlohmann::json::object();

        if (filesystem::exists(endpoint_file_path))
        {
            std::ifstream input_file(endpoint_file_path, std::ios::in | std::ios::binary);
            if (input_file)
            {
                input_file >> settings;
            }
        }

        if (!settings.is_object())
        {
            settings = nlohmann::json::object();
        }

        nlohmann::json service_settings = settings.value("Service", nlohmann::json::object());
        if (!service_settings.is_object())
        {
            service_settings = nlohmann::json::object();
        }

        service_settings["name"] = "RGB Server";
        service_settings["host"] = "127.0.0.1";
        service_settings["port"] = port;
        service_settings["websocket_port"] = port;
        service_settings["pid"] = pid;
        service_settings["running"] = true;

        settings["Service"] = service_settings;

        std::ofstream file(endpoint_file_path, std::ios::out | std::ios::binary | std::ios::trunc);
        file << settings.dump(4);
        file << std::endl;
    }
    catch (const std::exception &e)
    {
        LOG_WARNING("[WebSocketServer] Failed to write endpoint file %s: %s",
                    endpoint_file_path.string().c_str(), e.what());
    }
}

void WebSocketServer::ClearEndpointFile()
{
    if (endpoint_file_path.empty())
    {
        return;
    }

    try
    {
        if (resource_manager && resource_manager->GetSettingsManager())
        {
            SettingsManager *settings_manager = resource_manager->GetSettingsManager();
            nlohmann::json service_settings = settings_manager->GetSettings("Service");

            if (!service_settings.is_object())
            {
                service_settings = nlohmann::json::object();
            }

            service_settings["running"] = false;
            service_settings["pid"] = 0;

            settings_manager->SetSettings("Service", service_settings);
            settings_manager->SaveSettings();
        }

        if (!filesystem::exists(endpoint_file_path))
        {
            return;
        }

        nlohmann::json settings = nlohmann::json::object();
        std::ifstream input_file(endpoint_file_path, std::ios::in | std::ios::binary);
        if (input_file)
        {
            input_file >> settings;
        }

        if (!settings.is_object())
        {
            settings = nlohmann::json::object();
        }

        nlohmann::json service_settings = settings.value("Service", nlohmann::json::object());
        if (!service_settings.is_object())
        {
            service_settings = nlohmann::json::object();
        }

        service_settings["running"] = false;
        service_settings["pid"] = 0;

        settings["Service"] = service_settings;

        std::ofstream file(endpoint_file_path, std::ios::out | std::ios::binary | std::ios::trunc);
        file << settings.dump(4);
        file << std::endl;
    }
    catch (const std::exception &e)
    {
        LOG_WARNING("[WebSocketServer] Failed to remove endpoint file %s: %s",
                    endpoint_file_path.string().c_str(), e.what());
    }
}
