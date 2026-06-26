/*---------------------------------------------------------*\
| WebSocketClientInfo.h                                     |
|                                                           |
|   WebSocket Client Session Information                    |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <string>
#include <chrono>

class WebSocketClientInfo
{
public:
    WebSocketClientInfo(websocketpp::connection_hdl hdl, const std::string& client_ip, unsigned short client_port);
    ~WebSocketClientInfo();

    websocketpp::connection_hdl GetHandle() const;

    // Return the remote endpoint as a plain string ("ip:port").  These replace
    // the former QString-returning accessors; callers that need a C string use
    // .c_str() / .toStdString() which works identically on std::string.
    std::string             GetClientIP() const;
    std::string             GetClientString() const;
    std::string             GetAuthToken() const;
    bool                    IsAuthenticated() const;

    void                    SetAuthToken(const std::string& token);
    void                    SetAuthenticated(bool authenticated);

    unsigned long long      GetConnectionTime() const;
    unsigned long long      GetLastActivityTime() const;
    void                    UpdateActivityTime();

private:
    websocketpp::connection_hdl hdl;
    std::string                 client_ip;
    unsigned short              client_port;
    std::string                 auth_token;
    bool                        authenticated;

    // Connection timestamps (milliseconds since epoch)
    unsigned long long          connection_time;
    unsigned long long          last_activity_time;
};
