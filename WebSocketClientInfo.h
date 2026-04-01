/*---------------------------------------------------------*\
| WebSocketClientInfo.h                                     |
|                                                           |
|   WebSocket Client Session Information                    |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <QString>
#include <QWebSocket>
#include <string>
#include <chrono>

class WebSocketClientInfo
{
public:
    WebSocketClientInfo(QWebSocket* socket);
    ~WebSocketClientInfo();

    QWebSocket*             GetSocket() const;
    QString                 GetClientIP() const;
    QString                 GetClientString() const;
    std::string             GetAuthToken() const;
    bool                    IsAuthenticated() const;

    void                    SetAuthToken(const std::string& token);
    void                    SetAuthenticated(bool authenticated);

    unsigned long long      GetConnectionTime() const;
    unsigned long long      GetLastActivityTime() const;
    void                    UpdateActivityTime();

private:
    QWebSocket*             socket;
    std::string             auth_token;
    bool                    authenticated;

    // Connection timestamps (milliseconds since epoch)
    unsigned long long      connection_time;
    unsigned long long      last_activity_time;
};
