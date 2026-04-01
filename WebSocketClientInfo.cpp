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

WebSocketClientInfo::WebSocketClientInfo(QWebSocket* socket)
    : socket(socket), authenticated(false), connection_time(0), last_activity_time(0)
{
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    connection_time = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    last_activity_time = connection_time;
}

WebSocketClientInfo::~WebSocketClientInfo()
{
    // Socket is owned and managed by QWebSocketServer, don't delete it here
}

QWebSocket* WebSocketClientInfo::GetSocket() const
{
    return socket;
}

QString WebSocketClientInfo::GetClientIP() const
{
    if(socket)
    {
        return socket->peerAddress().toString();
    }
    return QString();
}

QString WebSocketClientInfo::GetClientString() const
{
    if(socket)
    {
        return socket->peerAddress().toString() + ":" + QString::number(socket->peerPort());
    }
    return QString();
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
