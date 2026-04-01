/*---------------------------------------------------------*\
| OpenRGBWebSocketInfoPage.h                                 |
|                                                           |
|   User interface for WebSocket server information page    |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <QFrame>
#include "RGBController.h"
#include "WebSocketServer.h"

namespace Ui
{
    class OpenRGBWebSocketInfoPage;
}

class OpenRGBWebSocketInfoPage : public QFrame
{
    Q_OBJECT

public:
    explicit OpenRGBWebSocketInfoPage(WebSocketServer * server, QWidget *parent = nullptr);
    ~OpenRGBWebSocketInfoPage();

public slots:
    void UpdateInfo();

private slots:
    void changeEvent(QEvent *event);
    void on_ServerStartButton_clicked();
    void on_ServerStopButton_clicked();
    void on_RequireAuthCheckBox_stateChanged(int state);
    void on_AuthTokenValue_textChanged(const QString &text);

private:
    Ui::OpenRGBWebSocketInfoPage *ui;

    WebSocketServer* websocket_server;
};
