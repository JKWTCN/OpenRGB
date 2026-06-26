/*---------------------------------------------------------*\
| WebSocketClientInfo.cpp                                   |
|                                                           |
|   WebSocket Client Session Information Implementation    |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "WebSocketClientInfo.h"
#include <chrono>

WebSocketClientInfo::WebSocketClientInfo(websocketpp::connection_hdl hdl,
                                         const std::string& client_ip,
                                         unsigned short client_port)
    : hdl(hdl), client_ip(client_ip), client_port(client_port),
      authenticated(false), connection_time(0), last_activity_time(0)
{
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    connection_time = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    last_activity_time = connection_time;
}

WebSocketClientInfo::~WebSocketClientInfo()
{
}

websocketpp::connection_hdl WebSocketClientInfo::GetHandle() const
{
    return hdl;
}

std::string WebSocketClientInfo::GetClientIP() const
{
    return client_ip;
}

std::string WebSocketClientInfo::GetClientString() const
{
    return client_ip + ":" + std::to_string(client_port);
}

std::string WebSocketClientInfo::GetAuthToken() const
{
    return auth_token;
}

bool WebSocketClientInfo::IsAuthenticated() const
{
    return authenticated;
}

void WebSocketClientInfo::SetAuthToken(const std::string& token)
{
    auth_token = token;
}

void WebSocketClientInfo::SetAuthenticated(bool authenticated)
{
    this->authenticated = authenticated;
}

unsigned long long WebSocketClientInfo::GetConnectionTime() const
{
    return connection_time;
}

unsigned long long WebSocketClientInfo::GetLastActivityTime() const
{
    return last_activity_time;
}

void WebSocketClientInfo::UpdateActivityTime()
{
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    last_activity_time = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}
