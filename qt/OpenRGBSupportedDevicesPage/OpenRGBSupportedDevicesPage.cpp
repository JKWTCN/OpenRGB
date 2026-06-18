/*---------------------------------------------------------*\
| OpenRGBSupportedDevicePage.cpp                            |
|                                                           |
|   User interface for enabling and disabling devices       |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "OpenRGBSupportedDevicesPage.h"
#include "ui_OpenRGBSupportedDevicesPage.h"
#include "ResourceManager.h"
#include "OpenRGBHardwareIDsDialog.h"
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QSet>
#include <QStringList>
#include <QTextStream>

static QString CSVCell(const QString& value)
{
    QString escaped = value;
    escaped.replace("\"", "\"\"");
    return("\"" + escaped + "\"");
}

static void WriteCSVRow(QTextStream& stream, const QStringList& values)
{
    QStringList escaped_values;

    for(const QString& value: values)
    {
        escaped_values << CSVCell(value);
    }

    stream << escaped_values.join(",") << "\n";
}

OpenRGBSupportedDevicesPage::OpenRGBSupportedDevicesPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OpenRGBSupportedDevicesPage)
{
    ui->setupUi(this);

    /*-----------------------------------------------------*\
    | Create a detector table model and a sort model and    |
    | set them                                              |
    \*-----------------------------------------------------*/
    detectorTableModel = new DetectorTableModel;
    detectorSortModel = new QSortFilterProxyModel;

    detectorSortModel->setSourceModel(detectorTableModel);
    ui->SupportedDevicesTable->setModel(detectorSortModel);

    /*-----------------------------------------------------*\
    | Disable header, enable sorting, and sort in ascending |
    | order                                                 |
    \*-----------------------------------------------------*/
    ui->SupportedDevicesTable->verticalHeader()->setVisible(0);
    ui->SupportedDevicesTable->setSortingEnabled(true);
    ui->SupportedDevicesTable->sortByColumn(0, Qt::AscendingOrder);

    /*-----------------------------------------------------*\
    | Resize columns to fit the contents                    |
    \*-----------------------------------------------------*/
    ui->SupportedDevicesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
}

OpenRGBSupportedDevicesPage::~OpenRGBSupportedDevicesPage()
{
    delete ui;
}

void OpenRGBSupportedDevicesPage::changeEvent(QEvent *event)
{
    if(event->type() == QEvent::LanguageChange)
    {
        ui->retranslateUi(this);
    }
}

void OpenRGBSupportedDevicesPage::on_SaveButton_clicked()
{
    detectorTableModel->applySettings();
}

void OpenRGBSupportedDevicesPage::on_GetHardwareIDsButton_clicked()
{
    OpenRGBHardwareIDsDialog dialog(this);
    dialog.show();
}

void OpenRGBSupportedDevicesPage::on_ExportCSVButton_clicked()
{
    QString filename = QFileDialog::getSaveFileName(this,
                                                    tr("Export Supported Devices"),
                                                    "OpenRGB-supported-devices.csv",
                                                    tr("CSV files (*.csv)"));

    if(filename.isEmpty())
    {
        return;
    }

    if(!filename.endsWith(".csv", Qt::CaseInsensitive))
    {
        filename += ".csv";
    }

    QFile file(filename);

    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("Export Failed"), tr("Unable to write the selected CSV file."));
        return;
    }

    QTextStream stream(&file);

#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    stream.setCodec("UTF-8");
#endif

    stream << QChar(0xFEFF);

    WriteCSVRow(stream, QStringList()
        << tr("Name")
        << tr("Enabled")
        << tr("Detector Type")
        << tr("Subcategory")
        << tr("Transport")
        << tr("Vendor ID")
        << tr("Product ID")
        << tr("Interface")
        << tr("Usage Page")
        << tr("Usage")
        << tr("PCI Vendor ID")
        << tr("PCI Device ID")
        << tr("PCI Subsystem Vendor ID")
        << tr("PCI Subsystem Device ID")
        << tr("I2C Address")
        << tr("JEDEC ID")
        << tr("DIMM Type"));

    QSet<QString> exported_names;
    std::vector<SupportedDeviceInfo> supported_devices = ResourceManager::get()->GetSupportedDeviceInfo();

    for(const SupportedDeviceInfo& supported_device: supported_devices)
    {
        QString name = QString::fromStdString(supported_device.name);
        exported_names.insert(name);

        WriteCSVRow(stream, QStringList()
            << name
            << (detectorTableModel->detectorEnabled(supported_device.name) ? tr("Yes") : tr("No"))
            << QString::fromStdString(supported_device.detector_type)
            << QString::fromStdString(supported_device.subcategory)
            << QString::fromStdString(supported_device.transport)
            << QString::fromStdString(supported_device.vendor_id)
            << QString::fromStdString(supported_device.product_id)
            << QString::fromStdString(supported_device.interface)
            << QString::fromStdString(supported_device.usage_page)
            << QString::fromStdString(supported_device.usage)
            << QString::fromStdString(supported_device.pci_vendor_id)
            << QString::fromStdString(supported_device.pci_device_id)
            << QString::fromStdString(supported_device.pci_subsystem_vendor_id)
            << QString::fromStdString(supported_device.pci_subsystem_device_id)
            << QString::fromStdString(supported_device.i2c_address)
            << QString::fromStdString(supported_device.jedec_id)
            << QString::fromStdString(supported_device.dimm_type));
    }

    for(const std::string& detector_name: detectorTableModel->detectorNames())
    {
        QString name = QString::fromStdString(detector_name);

        if(exported_names.contains(name))
        {
            continue;
        }

        WriteCSVRow(stream, QStringList()
            << name
            << (detectorTableModel->detectorEnabled(detector_name) ? tr("Yes") : tr("No"))
            << tr("Settings")
            << tr("Stored detector setting")
            << ""
            << ""
            << ""
            << ""
            << ""
            << ""
            << ""
            << ""
            << ""
            << ""
            << ""
            << ""
            << "");
    }
}

void OpenRGBSupportedDevicesPage::on_Filter_textChanged(const QString &arg1)
{
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    detectorSortModel->setFilterRegularExpression(QRegularExpression(arg1 , QRegularExpression::CaseInsensitiveOption));
#else
    detectorSortModel->setFilterRegExp(QRegExp(arg1, Qt::CaseInsensitive));
#endif
}

void OpenRGBSupportedDevicesPage::on_ToggleAllCheckbox_toggled(const bool checked)
{
    detectorTableModel->toggleAll(checked, detectorSortModel);
}
