/*---------------------------------------------------------*\
| OpenRGBWebSocketInfoPage.cpp                               |
|                                                           |
|   User interface for WebSocket server information page    |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "OpenRGBWebSocketInfoPage.h"
#include "ui_OpenRGBWebSocketInfoPage.h"
#include <QDateTime>

static void UpdateInfoCallback(void * this_ptr)
{
    OpenRGBWebSocketInfoPage * this_obj = (OpenRGBWebSocketInfoPage *)this_ptr;

    QMetaObject::invokeMethod(this_obj, "UpdateInfo", Qt::QueuedConnection);
}

OpenRGBWebSocketInfoPage::OpenRGBWebSocketInfoPage(WebSocketServer * server, QWidget *parent) :
    QFrame(parent),
    ui(new Ui::OpenRGBWebSocketInfoPage)
{
    websocket_server = server;

    ui->setupUi(this);

    // Set default values
    ui->ServerHostValue->setText(websocket_server->GetHost().c_str());
    ui->ServerPortValue->setText(QString::number(websocket_server->GetPort()));

    UpdateInfo();

    websocket_server->RegisterClientInfoChangeCallback(UpdateInfoCallback, this);
}

OpenRGBWebSocketInfoPage::~OpenRGBWebSocketInfoPage()
{
    delete ui;
}

void OpenRGBWebSocketInfoPage::changeEvent(QEvent *event)
{
    if(event->type() == QEvent::LanguageChange)
    {
        ui->retranslateUi(this);
    }
}

void OpenRGBWebSocketInfoPage::UpdateInfo()
{
    if(websocket_server->GetListening() && !websocket_server->GetOnline())
    {
        ui->ServerStatusValue->setText(tr("Stopping..."));
        ui->ServerStartButton->setEnabled(false);
        ui->ServerStopButton->setEnabled(false);
        ui->ServerHostValue->setEnabled(false);
        ui->ServerPortValue->setEnabled(false);
        ui->RequireAuthCheckBox->setEnabled(false);
        ui->AuthTokenValue->setEnabled(false);
    }
    else if(websocket_server->GetOnline())
    {
        ui->ServerStatusValue->setText(tr("Online"));
        ui->ServerStartButton->setEnabled(false);
        ui->ServerStopButton->setEnabled(true);
        ui->ServerHostValue->setEnabled(false);
        ui->ServerPortValue->setEnabled(false);
        ui->RequireAuthCheckBox->setEnabled(false);
        ui->AuthTokenValue->setEnabled(false);
    }
    else
    {
        ui->ServerStatusValue->setText(tr("Offline"));
        ui->ServerStartButton->setEnabled(true);
        ui->ServerStopButton->setEnabled(false);
        ui->ServerHostValue->setEnabled(true);
        ui->ServerPortValue->setEnabled(true);
        ui->RequireAuthCheckBox->setEnabled(true);
        ui->AuthTokenValue->setEnabled(true);
    }

    // Update client list
    ui->ServerClientTree->clear();
    for(unsigned int client_idx = 0; client_idx < websocket_server->GetNumClients(); client_idx++)
    {
        QTreeWidgetItem * new_item = new QTreeWidgetItem();

        new_item->setText(0, websocket_server->GetClientIP(client_idx));
        new_item->setText(1, tr("Connected"));
        new_item->setText(2, tr("Active"));

        ui->ServerClientTree->addTopLevelItem(new_item);
    }
}

void OpenRGBWebSocketInfoPage::on_ServerStartButton_clicked()
{
    if(websocket_server->GetOnline() == false)
    {
        websocket_server->SetHost(ui->ServerHostValue->text().toStdString());
        websocket_server->SetPort(ui->ServerPortValue->text().toUInt());

        // Apply authentication settings
        if(ui->RequireAuthCheckBox->isChecked())
        {
            websocket_server->SetRequireAuth(true);
            std::string token = ui->AuthTokenValue->text().toStdString();
            if(!token.empty())
            {
                websocket_server->SetAuthToken(token);
            }
        }
        else
        {
            websocket_server->SetRequireAuth(false);
        }

        websocket_server->SetEnabled(true);
        websocket_server->StartServer();

        UpdateInfo();
    }
}

void OpenRGBWebSocketInfoPage::on_ServerStopButton_clicked()
{
    if(websocket_server->GetOnline() == true)
    {
        websocket_server->StopServer();

        UpdateInfo();
    }
}

void OpenRGBWebSocketInfoPage::on_RequireAuthCheckBox_stateChanged(int state)
{
    // Enable/disable auth token field based on checkbox
    ui->AuthTokenValue->setEnabled(state == Qt::Checked);
}

void OpenRGBWebSocketInfoPage::on_AuthTokenValue_textChanged(const QString &text)
{
    // Auto-enable require auth if token is entered
    if(!text.isEmpty() && !ui->RequireAuthCheckBox->isChecked())
    {
        ui->RequireAuthCheckBox->setChecked(true);
    }
}
